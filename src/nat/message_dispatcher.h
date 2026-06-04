#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <nlohmann/json.hpp>

namespace beast_ws {
class ISession;
}
using WsSessionPtr = std::shared_ptr<beast_ws::ISession>;

class MessageDispatcher {
public:
    using Handler = std::function<void(WsSessionPtr, const std::string& node_id, const nlohmann::json&)>;

    MessageDispatcher() = default;

    // Register a handler for a specific message type
    void registerHandler(const std::string& type, Handler handler);

    // Remove a handler
    void unregisterHandler(const std::string& type);

    // Dispatch a message to the appropriate handler
    // Returns true if a handler was found and called
    bool dispatch(WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg);

    // Check if a handler exists for the given type
    bool hasHandler(const std::string& type) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Handler> handlers_;
};
