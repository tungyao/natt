#pragma once

#include "model/device.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <memory>

// Forward declaration
namespace beast_ws {
class ISession;
}

using WsSessionPtr = std::shared_ptr<beast_ws::ISession>;

class PeerManager {
public:
    PeerManager();

    // Session management
    void register_session(const std::string& node_id, WsSessionPtr session);
    void unregister_session(const std::string& node_id);
    WsSessionPtr get_session(const std::string& node_id);

    // Check if node is online (has active session)
    bool is_online(const std::string& node_id);

    // NAT signaling: handle connect_peer request
    // Returns error string or empty on success
    std::string handle_connect_peer(const std::string& requester_node_id,
                                     const std::string& target_node_id);

    // Broadcast peer list update to all devices in the same network
    void broadcast_peer_list(const std::string& changed_node_id,
                             const std::function<std::vector<Device>(const std::string&)>& get_peers);

    // Get online count
    size_t online_count() const;

private:
    std::unordered_map<std::string, WsSessionPtr> sessions_;
    mutable std::mutex mutex_;
};
