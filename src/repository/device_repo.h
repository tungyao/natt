#pragma once

#include "repository/database.h"
#include "model/device.h"
#include <optional>
#include <vector>

class DeviceRepo {
public:
    explicit DeviceRepo(Database& db);

    bool create(const Device& device);
    std::optional<Device> find_by_node_id(const std::string& node_id);
    std::vector<Device> find_by_user_id(int64_t user_id);
    bool update_connection_info(const std::string& node_id,
                                const std::string& public_ip, int public_port,
                                const std::string& lan_ips_json);
    bool update_online_status(const std::string& node_id, bool online);
    bool update_heartbeat(const std::string& node_id);
    bool update_virtual_ip(const std::string& node_id, const std::string& virtual_ip);
    bool remove(const std::string& node_id);
    std::vector<Device> find_offline_timeout(int timeout_sec);

private:
    Database& db_;
};
