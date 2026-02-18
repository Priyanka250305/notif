#pragma once

#include <crow.h>
#include <unordered_map>
#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <atomic>

using WebSocketConnection = crow::websocket::connection;

class ConnectionManager {
public:
    static ConnectionManager& instance() {
        static ConnectionManager inst;
        return inst;
    }
    void add(const std::string& user_id, WebSocketConnection* conn);

    void remove(const std::string& user_id);

    bool send(const std::string& user_id, const std::string& message);

    void broadcast(const std::string& message);

    bool is_connected(const std::string& user_id) const;
    size_t connection_count() const;
    std::vector<std::string> connected_users() const;

private:
    ConnectionManager() = default;
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, WebSocketConnection*> connections_;
    std::atomic<size_t> total_sent_{0};
    std::atomic<size_t> total_failed_{0};

    friend class MetricsCollector;
};
