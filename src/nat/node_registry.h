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
    bool online = false;
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

    // Find a single node by ID
    std::optional<NodeInfo> findNode(const std::string& node_id) const;

    // List all online nodes in a network
    std::vector<NodeInfo> listNetworkNodes(const std::string& network_id) const;

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
