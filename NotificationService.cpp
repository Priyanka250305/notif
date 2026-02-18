#include "NotificationService.h"
#include "ConnectionManager.h"
#include "MetricsCollector.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <random>

// ── Notification helpers ─────────────────────────────────────────────────────

static std::string generate_id() {
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(rng);
    return oss.str();
}

Notification Notification::from_json(const json& j) {
    Notification n;
    n.user_id  = j.value("user_id",  "");
    n.message  = j.value("message",  "");
    n.type     = j.value("type",     "info");
    n.id       = j.value("id",       generate_id());
    n.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
    return n;
}

json Notification::to_json() const {
    return {
        {"id",        id},
        {"user_id",   user_id},
        {"message",   message},
        {"type",      type},
        {"timestamp", timestamp}
    };
}

// ── NotificationService ──────────────────────────────────────────────────────

NotificationService::NotificationService(std::shared_ptr<sw::redis::Redis> redis,
                                         const std::string& channel)
    : redis_(std::move(redis)), channel_(channel) {}

bool NotificationService::publish(const json& payload, std::string& error_msg) {
    if (!payload.contains("user_id") || !payload.contains("message")) {
        error_msg = "Missing required fields: user_id, message";
        return false;
    }

    Notification n = Notification::from_json(payload);

    if (n.user_id.empty() || n.message.empty()) {
        error_msg = "user_id and message must not be empty";
        return false;
    }

    // Publish serialized notification to Redis
    try {
        std::string serialized = n.to_json().dump();
        redis_->publish(channel_, serialized);
        MetricsCollector::instance().increment_redis_published();
        std::cout << "[NotificationService] Published to Redis: id=" << n.id
                  << " user=" << n.user_id << "\n";
        return true;
    } catch (const std::exception& e) {
        error_msg = std::string("Redis publish failed: ") + e.what();
        return false;
    }
}

bool NotificationService::already_processed(const std::string& notification_id) {
    // Check Redis for distributed dedup
    try {
        std::string key = "notif:processed:" + notification_id;
        auto val = redis_->get(key);
        if (val) return true;
    } catch (...) {
        // Fall back to in-memory check
    }

    std::lock_guard<std::mutex> lock(dedup_mutex_);
    return processed_ids_.count(notification_id) > 0;
}

void NotificationService::mark_processed(const std::string& notification_id) {
    try {
        std::string key = "notif:processed:" + notification_id;
        // Expire after 24h to avoid memory leak
        redis_->setex(key, 86400, "1");
    } catch (...) {}

    std::lock_guard<std::mutex> lock(dedup_mutex_);
    processed_ids_.insert(notification_id);
}

void NotificationService::deliver(const std::string& raw_message) {
    MetricsCollector::instance().increment_redis_received();

    json j;
    try {
        j = json::parse(raw_message);
    } catch (const std::exception& e) {
        std::cerr << "[NotificationService] Failed to parse message: " << e.what() << "\n";
        return;
    }

    Notification n = Notification::from_json(j);

    // Idempotency check
    if (already_processed(n.id)) {
        std::cout << "[NotificationService] Duplicate skipped: " << n.id << "\n";
        return;
    }

    mark_processed(n.id);

    // Deliver if user is connected on THIS instance
    auto& cm = ConnectionManager::instance();
    if (cm.is_connected(n.user_id)) {
        json delivery = {
            {"type",    "notification"},
            {"id",      n.id},
            {"message", n.message},
            {"notif_type", n.type},
            {"timestamp", n.timestamp}
        };
        bool sent = cm.send(n.user_id, delivery.dump());
        if (sent) {
            MetricsCollector::instance().increment_messages_sent();
            std::cout << "[NotificationService] Delivered: id=" << n.id
                      << " user=" << n.user_id << "\n";
        } else {
            MetricsCollector::instance().increment_failed_deliveries();
        }
    } else {
        std::cout << "[NotificationService] User not on this instance: " << n.user_id << "\n";
    }
}
