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

    // Register a new WebSocket connection for a user
    void add(const std::string& user_id, WebSocketConnection* conn);

    // Remove a user's connection
    void remove(const std::string& user_id);

    // Send a message to a specific user
    // Returns true if user was found and message sent
    bool send(const std::string& user_id, const std::string& message);

    // Broadcast to all connected users
    void broadcast(const std::string& message);

    // Check if user is connected on THIS instance
    bool is_connected(const std::string& user_id) const;

    // Get count of active connections
    size_t connection_count() const;

    // Get all connected user IDs
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
