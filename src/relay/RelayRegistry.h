#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <boost/asio.hpp>

namespace net = boost::asio;
using udp = net::ip::udp;

class RelayRegistry {
public:
    RelayRegistry() = default;

    // Register a node. Returns false if node_id already exists.
    bool registerNode(const std::string& node_id,
                      const std::string& network_id,
                      const udp::endpoint& endpoint);

    // Remove a node from all mappings.
    void removeNode(const std::string& node_id);

    // Find endpoint by node_id. Returns false if not found.
    bool findEndpoint(const std::string& node_id, udp::endpoint& out) const;

    // Find network_id by node_id. Returns false if not found.
    bool findNetworkId(const std::string& node_id, std::string& out) const;

    // Touch the last_seen timestamp for a node.
    void updateHeartbeat(const std::string& node_id);

    // Remove all nodes whose last_seen is older than timeout_seconds.
    // Returns count of removed nodes.
    int cleanupExpired(int timeout_seconds);

    // Check if a node is online (exists in registry).
    bool isOnline(const std::string& node_id) const;

    // Number of registered nodes.
    size_t size() const;

private:
    struct NodeEntry {
        udp::endpoint endpoint;
        std::string network_id;
        std::chrono::steady_clock::time_point last_seen;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, NodeEntry> nodes_;
};
