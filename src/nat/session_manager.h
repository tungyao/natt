#pragma once

#include <string>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace beast_ws {
class ISession;
}
using WsSessionPtr = std::shared_ptr<beast_ws::ISession>;

class SessionManager {
public:
    SessionManager() = default;

    // Register a session for the given node_id
    void registerSession(const std::string& node_id, WsSessionPtr session);

    // Remove a session
    void removeSession(const std::string& node_id);

    // Get session by node_id (returns nullptr if not found)
    WsSessionPtr getSession(const std::string& node_id);

    // Send a JSON message to a specific node
    bool sendTo(const std::string& node_id, const nlohmann::json& msg);

    // Broadcast a JSON message to all connected sessions
    void broadcast(const nlohmann::json& msg);

    // Broadcast to all sessions matching a filter function
    void broadcastIf(const nlohmann::json& msg,
                     const std::function<bool(const std::string&)>& filter);

    // Get number of active sessions
    size_t onlineCount() const;

    // List all connected node IDs
    std::vector<std::string> listSessions() const;

    // Check if a node has an active session
    bool hasSession(const std::string& node_id) const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, WsSessionPtr> sessions_;
};
