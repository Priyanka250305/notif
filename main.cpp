#include <crow.h>
#include <nlohmann/json.hpp>
#include <sw/redis++/redis++.h>
#include <iostream>
#include <memory>
#include <string>
#include <cstdlib>

#include "ConnectionManager.h"
#include "NotificationService.h"
#include "MetricsCollector.h"
#include "RedisSubscriber.h"

using json = nlohmann::json;

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string get_env(const char* name, const char* default_val) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string(default_val);
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    // Config from environment variables (12-factor app style)
    const int         PORT         = std::stoi(get_env("PORT",        "8080"));
    const std::string REDIS_URL    = get_env("REDIS_URL",    "tcp://127.0.0.1:6379");
    const std::string WS_CHANNEL   = get_env("NOTIF_CHANNEL","notifications");
    const int         CONCURRENCY  = std::stoi(get_env("CONCURRENCY", "4"));

    std::cout << "=== Notification Server ===\n"
              << "Port:      " << PORT       << "\n"
              << "Redis:     " << REDIS_URL  << "\n"
              << "Channel:   " << WS_CHANNEL << "\n"
              << "Threads:   " << CONCURRENCY << "\n\n";

    // ── Redis setup ───────────────────────────────────────────────────────────
    std::shared_ptr<sw::redis::Redis> redis;
    try {
        redis = std::make_shared<sw::redis::Redis>(REDIS_URL);
        redis->ping();
        std::cout << "[Redis] Connected\n";
    } catch (const std::exception& e) {
        std::cerr << "[Redis] Connection FAILED: " << e.what()
                  << "\nStarting WITHOUT Redis (single-instance mode)\n\n";
    }

    // ── NotificationService ───────────────────────────────────────────────────
    auto notif_service = std::make_shared<NotificationService>(redis, WS_CHANNEL);

    // ── Redis Subscriber (background thread) ─────────────────────────────────
    std::unique_ptr<RedisSubscriber> subscriber;
    if (redis) {
        subscriber = std::make_unique<RedisSubscriber>(
            REDIS_URL,
            WS_CHANNEL,
            [&notif_service](const std::string& msg) {
                notif_service->deliver(msg);
            }
        );
        subscriber->start();
    }

    // ── Crow app ──────────────────────────────────────────────────────────────
    crow::SimpleApp app;

    // CORS middleware helper
    auto add_cors = [](crow::response& res) {
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, X-User-ID");
    };

    // ── GET /health ───────────────────────────────────────────────────────────
    CROW_ROUTE(app, "/health")
    ([&add_cors](const crow::request&, crow::response& res) {
        json body = {
            {"status", "ok"},
            {"service", "notification-server"}
        };
        res.code = 200;
        res.set_header("Content-Type", "application/json");
        add_cors(res);
        res.write(body.dump());
        res.end();
    });

    // ── OPTIONS (CORS preflight) ──────────────────────────────────────────────
    CROW_ROUTE(app, "/notify").methods(crow::HTTPMethod::OPTIONS)
    ([&add_cors](const crow::request&, crow::response& res) {
        res.code = 204;
        add_cors(res);
        res.end();
    });

    // ── POST /notify ──────────────────────────────────────────────────────────
    //
    // Body: { "user_id": "...", "message": "...", "type": "info" }
    //
    CROW_ROUTE(app, "/notify").methods(crow::HTTPMethod::POST)
    ([&notif_service, &add_cors](const crow::request& req, crow::response& res) {
        add_cors(res);
        res.set_header("Content-Type", "application/json");

        json body;
        try {
            body = json::parse(req.body);
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(json{{"error", "Invalid JSON"}}.dump());
            res.end();
            return;
        }

        std::string error_msg;
        if (notif_service->publish(body, error_msg)) {
            res.code = 200;
            res.write(json{{"status", "queued"}, {"ok", true}}.dump());
        } else {
            res.code = 400;
            res.write(json{{"error", error_msg}}.dump());
        }
        res.end();
    });

    // ── GET /metrics ──────────────────────────────────────────────────────────
    CROW_ROUTE(app, "/metrics")
    ([&add_cors](const crow::request&, crow::response& res) {
        add_cors(res);
        res.set_header("Content-Type", "application/json");
        res.code = 200;
        res.write(MetricsCollector::instance().to_json());
        res.end();
    });

    // ── GET /connections ─────────────────────────────────────────────────────
    CROW_ROUTE(app, "/connections")
    ([&add_cors](const crow::request&, crow::response& res) {
        add_cors(res);
        res.set_header("Content-Type", "application/json");

        auto users = ConnectionManager::instance().connected_users();
        json j = {
            {"count", users.size()},
            {"users", users}
        };
        res.code = 200;
        res.write(j.dump());
        res.end();
    });

    // ── WebSocket /ws?user_id=XXX ─────────────────────────────────────────────
    //
    // The client MUST pass ?user_id=<id> in the query string.
    //
    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([](crow::websocket::connection& conn) {
            // user_id is extracted in onopen — Crow gives us the URL params
            const std::string user_id = conn.get_request().url_params.get("user_id")
                                        ? conn.get_request().url_params.get("user_id")
                                        : "anonymous_" + std::to_string(
                                            reinterpret_cast<uintptr_t>(&conn));

            conn.userdata(new std::string(user_id));
            ConnectionManager::instance().add(user_id, &conn);

            // Send welcome
            json welcome = {
                {"type",    "connected"},
                {"user_id", user_id},
                {"message", "Connection established"}
            };
            conn.send_text(welcome.dump());
        })
        .onmessage([](crow::websocket::connection& conn,
                      const std::string& data,
                      bool /*is_binary*/) {
            // Echo + heartbeat support
            json msg;
            try {
                msg = json::parse(data);
            } catch (...) {
                msg = {{"raw", data}};
            }

            if (msg.value("type", "") == "ping") {
                conn.send_text(json{{"type", "pong"}}.dump());
                return;
            }

            // Echo back
            json echo = {
                {"type", "echo"},
                {"data", msg}
            };
            conn.send_text(echo.dump());
        })
        .onclose([](crow::websocket::connection& conn, const std::string& /*reason*/) {
            auto* user_id_ptr = static_cast<std::string*>(conn.userdata());
            if (user_id_ptr) {
                ConnectionManager::instance().remove(*user_id_ptr);
                delete user_id_ptr;
                conn.userdata(nullptr);
            }
        });

    // ── Start server ──────────────────────────────────────────────────────────
    std::cout << "\n[Server] Starting on port " << PORT << "...\n\n";
    app.port(PORT)
       .concurrency(CONCURRENCY)
       .run();

    // Cleanup
    if (subscriber) subscriber->stop();

    return 0;
}
