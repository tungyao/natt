#include "nat/session_manager.h"
#include "ws/ws_session.h"
#include <spdlog/spdlog.h>

void SessionManager::registerSession(const std::string& node_id, WsSessionPtr session) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    sessions_[node_id] = std::move(session);
    spdlog::info("SessionManager: registered node_id={}, total={}", node_id, sessions_.size());
}

void SessionManager::removeSession(const std::string& node_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    sessions_.erase(node_id);
    spdlog::info("SessionManager: removed node_id={}, total={}", node_id, sessions_.size());
}

WsSessionPtr SessionManager::getSession(const std::string& node_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = sessions_.find(node_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    return nullptr;
}

bool SessionManager::sendTo(const std::string& node_id, const nlohmann::json& msg) {
    auto session = getSession(node_id);
    if (session) {
        session->send_json(msg.dump());
        return true;
    }
    return false;
}

void SessionManager::broadcast(const nlohmann::json& msg) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto payload = msg.dump();
    for (auto& [id, session] : sessions_) {
        session->send_json(payload);
    }
    spdlog::debug("SessionManager: broadcast to {} sessions", sessions_.size());
}

void SessionManager::broadcastIf(
    const nlohmann::json& msg,
    const std::function<bool(const std::string&)>& filter)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto payload = msg.dump();
    int count = 0;
    for (auto& [id, session] : sessions_) {
        if (filter(id)) {
            session->send_json(payload);
            ++count;
        }
    }
    spdlog::debug("SessionManager: broadcastIf matched {} sessions", count);
}

size_t SessionManager::onlineCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return sessions_.size();
}

std::vector<std::string> SessionManager::listSessions() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, _] : sessions_) {
        ids.push_back(id);
    }
    return ids;
}

bool SessionManager::hasSession(const std::string& node_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return sessions_.find(node_id) != sessions_.end();
}
