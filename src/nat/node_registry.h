#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

struct NodeInfo {
    std::string node_id;
    std::string network_id;
    std::string public_key;
    std::string public_ip;
    int public_port = 0;
    std::vector<std::string> local_addrs;
    std::string virtual_ip;
    std::string gateway_ip;
    std::string subnet;
    bool online = false;
    bool session_online = false;
    std::string connection_state = "offline";
    std::string transport_mode = "unknown";
    int64_t last_transport_rtt_ms = 0;
    int reconnect_count = 0;
    std::string last_disconnect_reason;
    std::chrono::system_clock::time_point last_seen;

    nlohmann::json to_punch_json() const {
        nlohmann::json j;
        j["peer_node_id"] = node_id;
        j["peer_public_ip"] = public_ip;
        j["peer_public_port"] = public_port;
        j["peer_local_addrs"] = nlohmann::json::array();
        for (const auto& addr : local_addrs) {
            j["peer_local_addrs"].push_back(addr);
        }
        j["peer_public_key"] = public_key;
        j["peer_virtual_ip"] = virtual_ip;
        return j;
    }

    nlohmann::json to_peer_json() const {
        nlohmann::json j;
        j["node_id"] = node_id;
        j["public_ip"] = public_ip;
        j["public_port"] = public_port;
        j["local_addrs"] = nlohmann::json::array();
        for (const auto& addr : local_addrs) {
            j["local_addrs"].push_back(addr);
        }
        j["public_key"] = public_key;
        j["virtual_ip"] = virtual_ip;
        j["gateway_ip"] = gateway_ip;
        j["subnet"] = subnet;
        j["online"] = online;
        return j;
    }
};

class NodeRegistry {
public:
    NodeRegistry() = default;

    // Register a new node (or update existing)
    void registerNode(const NodeInfo& info);

    // Update heartbeat timestamp
    void updateHeartbeat(const std::string& node_id);

    // Update public/local addresses
    void updateAddress(const std::string& node_id,
                       const std::string& public_ip,
                       int public_port,
                       const std::vector<std::string>& local_addrs);

    // Update session/connection state
    void updateSessionState(const std::string& node_id,
                            bool session_online,
                            const std::string& connection_state,
                            const std::string& disconnect_reason = "");

    // Update transport state
    void updateTransportState(const std::string& node_id,
                              const std::string& transport_mode,
                              int64_t rtt_ms = 0);

    // Find a single node by ID
    std::optional<NodeInfo> findNode(const std::string& node_id) const;

    // List all online nodes in a network
    std::vector<NodeInfo> listNetworkNodes(const std::string& network_id) const;

    // List all known online nodes
    std::vector<NodeInfo> listAllNodes() const;

    // Remove a node (offline)
    void removeNode(const std::string& node_id);

    // Check if node exists and is online
    bool isOnline(const std::string& node_id) const;

    // Get count of online nodes
    size_t onlineCount() const;

    // Find nodes that have timed out (last_seen older than timeout_sec)
    std::vector<std::string> findTimedOutNodes(int timeout_sec) const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, NodeInfo> nodes_;
};
