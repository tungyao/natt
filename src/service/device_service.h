#pragma once

#include "repository/device_repo.h"
#include "model/device.h"
#include <string>
#include <optional>
#include <vector>

class DeviceService {
public:
    explicit DeviceService(DeviceRepo& device_repo);

    // Returns (error_message, node_id) — node_id empty on error
    std::pair<std::string, std::string> create_device(int64_t user_id,
                                                       const std::string& device_name,
                                                       const std::string& public_key);

    std::optional<Device> get_device(const std::string& node_id);
    std::vector<Device> list_user_devices(int64_t user_id);
    bool delete_device(const std::string& node_id, int64_t user_id);

    void update_connection_info(const std::string& node_id,
                                 const std::string& public_ip, int public_port,
                                 const std::vector<std::string>& lan_ips);
    void update_heartbeat(const std::string& node_id);
    void set_offline(const std::string& node_id);

private:
    DeviceRepo& device_repo_;
};
