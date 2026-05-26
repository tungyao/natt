#include "service/device_service.h"
#include "util/uuid.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

DeviceService::DeviceService(DeviceRepo& device_repo)
    : device_repo_(device_repo) {}

std::pair<std::string, std::string> DeviceService::create_device(
    int64_t user_id, const std::string& device_name, const std::string& public_key)
{
    if (device_name.empty()) {
        return {"Device name is required", ""};
    }

    Device device;
    device.node_id = util::generate_uuid();
    device.user_id = user_id;
    device.device_name = device_name;
    device.public_key = public_key;

    if (!device_repo_.create(device)) {
        return {"Failed to create device", ""};
    }

    spdlog::info("Device created: node_id={}, name={}", device.node_id, device_name);
    return {"", device.node_id};
}

std::optional<Device> DeviceService::get_device(const std::string& node_id) {
    return device_repo_.find_by_node_id(node_id);
}

std::vector<Device> DeviceService::list_user_devices(int64_t user_id) {
    return device_repo_.find_by_user_id(user_id);
}

bool DeviceService::delete_device(const std::string& node_id, int64_t user_id) {
    auto device = device_repo_.find_by_node_id(node_id);
    if (!device.has_value()) return false;
    if (device->user_id != user_id) return false; // Not owner

    if (!device_repo_.remove(node_id)) return false;
    spdlog::info("Device deleted: node_id={}", node_id);
    return true;
}

void DeviceService::update_connection_info(const std::string& node_id,
                                            const std::string& public_ip, int public_port,
                                            const std::vector<std::string>& lan_ips)
{
    auto lan_json = nlohmann::json(lan_ips).dump();
    device_repo_.update_connection_info(node_id, public_ip, public_port, lan_json);
}

void DeviceService::update_heartbeat(const std::string& node_id) {
    device_repo_.update_heartbeat(node_id);
}

void DeviceService::set_offline(const std::string& node_id) {
    device_repo_.update_online_status(node_id, false);
}
