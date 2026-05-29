#include "nat/node_registry.h"
#include <spdlog/spdlog.h>

void NodeRegistry::registerNode(const NodeInfo& info) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& node = nodes_[info.node_id];
    bool was_known = !node.node_id.empty();
    bool was_session_online = node.session_online;
    std::string existing_transport = node.transport_mode;
    int64_t existing_rtt = node.last_transport_rtt_ms;
    int existing_reconnects = node.reconnect_count;

    node = info;
    node.online = true;
    node.session_online = true;
    node.connection_state = "online";
    node.last_disconnect_reason.clear();
    node.last_seen = std::chrono::system_clock::now();
    node.transport_mode = existing_transport.empty() ? "unknown" : existing_transport;
    node.last_transport_rtt_ms = existing_rtt;
    node.reconnect_count = existing_reconnects + ((was_known && !was_session_online) ? 1 : 0);

    spdlog::info("NodeRegistry: registered node_id={}, network_id={}",
                 info.node_id, info.network_id);
}

void NodeRegistry::updateHeartbeat(const std::string& node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_seen = std::chrono::system_clock::now();
        it->second.online = true;
        if (it->second.connection_state != "online") {
            it->second.connection_state = it->second.session_online ? "online" : "grace";
        }
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

void NodeRegistry::updateSessionState(const std::string& node_id,
                                      bool session_online,
                                      const std::string& connection_state,
                                      const std::string& disconnect_reason) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return;
    }

    it->second.session_online = session_online;
    it->second.connection_state = connection_state;
    if (!disconnect_reason.empty()) {
        it->second.last_disconnect_reason = disconnect_reason;
    }
    if (session_online) {
        it->second.online = true;
        it->second.last_disconnect_reason.clear();
        it->second.last_seen = std::chrono::system_clock::now();
    }
}

void NodeRegistry::updateTransportState(const std::string& node_id,
                                        const std::string& transport_mode,
                                        int64_t rtt_ms) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return;
    }

    it->second.transport_mode = transport_mode;
    if (rtt_ms > 0) {
        it->second.last_transport_rtt_ms = rtt_ms;
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
        if (info.network_id == network_id && info.session_online) {
            result.push_back(info);
        }
    }
    return result;
}

std::vector<NodeInfo> NodeRegistry::listAllNodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<NodeInfo> result;
    result.reserve(nodes_.size());
    for (const auto& [id, info] : nodes_) {
        result.push_back(info);
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
    return it != nodes_.end() && it->second.session_online;
}

size_t NodeRegistry::onlineCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, info] : nodes_) {
        if (info.session_online) ++count;
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
