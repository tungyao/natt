#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct Device {
    int64_t id = 0;
    std::string node_id;
    int64_t user_id = 0;
    std::string device_name;
    std::string public_key;
    std::string public_ip;
    int public_port = 0;
    std::vector<std::string> lan_ips;
    bool online = false;
    std::string last_heartbeat;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"node_id", node_id},
            {"device_name", device_name},
            {"public_key", public_key},
            {"public_ip", public_ip},
            {"public_port", public_port},
            {"lan_ips", lan_ips},
            {"online", online},
            {"last_heartbeat", last_heartbeat},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }

    nlohmann::json to_peer_json() const {
        return {
            {"node_id", node_id},
            {"device_name", device_name},
            {"public_key", public_key},
            {"public_ip", public_ip},
            {"public_port", public_port},
            {"lan_ips", lan_ips},
            {"online", online}
        };
    }
};
