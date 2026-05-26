#include "relay/RelayRegistry.h"
#include <spdlog/spdlog.h>

bool RelayRegistry::registerNode(const std::string& node_id,
                                 const std::string& network_id,
                                 const udp::endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        spdlog::warn("RelayRegistry: node {} already registered, updating endpoint", node_id);
        it->second.endpoint = endpoint;
        it->second.last_seen = std::chrono::steady_clock::now();
        return false;
    }

    NodeEntry entry;
    entry.endpoint = endpoint;
    entry.network_id = network_id;
    entry.last_seen = std::chrono::steady_clock::now();
    nodes_[node_id] = std::move(entry);

    spdlog::info("RelayRegistry: registered node {} (network={}) at {}:{}",
                 node_id, network_id,
                 endpoint.address().to_string(), endpoint.port());
    return true;
}

void RelayRegistry::removeNode(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        spdlog::info("RelayRegistry: removing node {}", node_id);
        nodes_.erase(it);
    }
}

bool RelayRegistry::findEndpoint(const std::string& node_id,
                                 udp::endpoint& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    out = it->second.endpoint;
    return true;
}

bool RelayRegistry::findNetworkId(const std::string& node_id,
                                  std::string& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    out = it->second.network_id;
    return true;
}

void RelayRegistry::updateHeartbeat(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_seen = std::chrono::steady_clock::now();
        spdlog::debug("RelayRegistry: heartbeat from node {}", node_id);
    }
}

int RelayRegistry::cleanupExpired(int timeout_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    int removed = 0;

    for (auto it = nodes_.begin(); it != nodes_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_seen).count();
        if (elapsed >= timeout_seconds) {
            spdlog::info("RelayRegistry: removing expired node {} (idle {}s)",
                         it->first, static_cast<long>(elapsed));
            it = nodes_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        spdlog::info("RelayRegistry: cleanup removed {} expired nodes", removed);
    }
    return removed;
}

bool RelayRegistry::isOnline(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.find(node_id) != nodes_.end();
}

size_t RelayRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}
