#include "nat/node_registry.h"
#include <spdlog/spdlog.h>

void NodeRegistry::registerNode(const NodeInfo& info) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    nodes_[info.node_id] = info;
    nodes_[info.node_id].online = true;
    nodes_[info.node_id].last_seen = std::chrono::system_clock::now();
    spdlog::info("NodeRegistry: registered node_id={}, network_id={}",
                 info.node_id, info.network_id);
}

void NodeRegistry::updateHeartbeat(const std::string& node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_seen = std::chrono::system_clock::now();
        it->second.online = true;
    }
}

void NodeRegistry::updateAddress(const std::string& node_id,
                                  const std::string& public_ip,
                                  int public_port,
                                  const std::vector<std::string>& local_addrs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.public_ip = public_ip;
        it->second.public_port = public_port;
        it->second.local_addrs = local_addrs;
        it->second.last_seen = std::chrono::system_clock::now();
        spdlog::debug("NodeRegistry: updated address for node_id={}, ip={}:{}",
                      node_id, public_ip, public_port);
    }
}

std::optional<NodeInfo> NodeRegistry::findNode(const std::string& node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<NodeInfo> NodeRegistry::listNetworkNodes(const std::string& network_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<NodeInfo> result;
    for (const auto& [id, info] : nodes_) {
        if (info.network_id == network_id && info.online) {
            result.push_back(info);
        }
    }
    return result;
}

void NodeRegistry::removeNode(const std::string& node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    nodes_.erase(node_id);
    spdlog::info("NodeRegistry: removed node_id={}", node_id);
}

bool NodeRegistry::isOnline(const std::string& node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    return it != nodes_.end() && it->second.online;
}

size_t NodeRegistry::onlineCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, info] : nodes_) {
        if (info.online) ++count;
    }
    return count;
}

std::vector<std::string> NodeRegistry::findTimedOutNodes(int timeout_sec) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> result;
    auto now = std::chrono::system_clock::now();

    for (const auto& [id, info] : nodes_) {
        if (!info.online) continue;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - info.last_seen).count();
        if (elapsed >= timeout_sec) {
            result.push_back(id);
        }
    }
    return result;
}
