#include "service/peer_manager.h"
#include "ws/ws_session.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

PeerManager::PeerManager() = default;

void PeerManager::register_session(const std::string& node_id, WsSessionPtr session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[node_id] = std::move(session);
    spdlog::info("Session registered: node_id={}, total online={}", node_id, sessions_.size());
}

void PeerManager::unregister_session(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(node_id);
    spdlog::info("Session unregistered: node_id={}, total online={}", node_id, sessions_.size());
}

WsSessionPtr PeerManager::get_session(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(node_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

bool PeerManager::is_online(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(node_id) != sessions_.end();
}

std::string PeerManager::handle_connect_peer(const std::string& requester_node_id,
                                              const std::string& target_node_id) {
    // Check both devices are online
    auto requester_session = get_session(requester_node_id);
    auto target_session = get_session(target_node_id);

    if (!requester_session) {
        return "Requester device is not online";
    }
    if (!target_session) {
        return "Target device is not online";
    }

    // Build punch_start message for requester (A)
    // We need the target device info from the target session
    auto target_device = target_session->get_device_info();
    if (!target_device.has_value()) {
        return "Target device info not available";
    }

    auto requester_device = requester_session->get_device_info();
    if (!requester_device.has_value()) {
        return "Requester device info not available";
    }

    // Send punch_start to requester (A) — "connect to target B"
    nlohmann::json msg_to_requester = {
        {"type", "punch_start"},
        {"direction", "outgoing"},
        {"peer", target_device->to_peer_json()}
    };
    requester_session->send_json(msg_to_requester.dump());

    // Send punch_start to target (B) — "incoming connection from A"
    nlohmann::json msg_to_target = {
        {"type", "punch_start"},
        {"direction", "incoming"},
        {"peer", requester_device->to_peer_json()}
    };
    target_session->send_json(msg_to_target.dump());

    spdlog::info("NAT signaling: {} → {}", requester_node_id, target_node_id);
    return ""; // success
}

void PeerManager::broadcast_peer_list(const std::string& changed_node_id,
                                       const std::function<std::vector<Device>(const std::string&)>& get_peers) {
    std::lock_guard<std::mutex> lock(mutex_);

    // For each online session, send updated peer list
    for (auto& [node_id, session] : sessions_) {
        auto peers = get_peers(node_id);
        nlohmann::json msg = {
            {"type", "peer_list"},
            {"peers", nlohmann::json::array()}
        };
        for (const auto& peer : peers) {
            msg["peers"].push_back(peer.to_peer_json());
        }
        session->send_json(msg.dump());
    }
}

size_t PeerManager::online_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}
