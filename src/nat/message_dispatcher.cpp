#include "nat/message_dispatcher.h"
#include <spdlog/spdlog.h>

void MessageDispatcher::registerHandler(const std::string& type, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[type] = std::move(handler);
    spdlog::debug("MessageDispatcher: registered handler for type='{}'", type);
}

void MessageDispatcher::unregisterHandler(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.erase(type);
    spdlog::debug("MessageDispatcher: unregistered handler for type='{}'", type);
}

bool MessageDispatcher::dispatch(WsSessionPtr session,
                                  const std::string& node_id,
                                  const nlohmann::json& msg) {
    // Extract type field
    auto it = msg.find("type");
    if (it == msg.end()) {
        spdlog::warn("MessageDispatcher: message from node_id={} has no 'type' field", node_id);
        return false;
    }

    std::string type = it->get<std::string>();

    Handler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto hit = handlers_.find(type);
        if (hit == handlers_.end()) {
            spdlog::warn("MessageDispatcher: no handler for type='{}' from node_id={}",
                         type, node_id);
            return false;
        }
        handler = hit->second;
    }

    handler(std::move(session), node_id, msg);
    return true;
}

bool MessageDispatcher::hasHandler(const std::string& type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handlers_.find(type) != handlers_.end();
}
