#include "service/network_service.h"
#include <spdlog/spdlog.h>

NetworkService::NetworkService(NetworkRepo& network_repo, DeviceRepo& device_repo, IpAllocator& ipam)
    : network_repo_(network_repo), device_repo_(device_repo), ipam_(ipam) {}

std::pair<std::string, int64_t> NetworkService::create_network(const std::string& name, int64_t owner_id) {
    if (name.empty()) {
        return {"Network name is required", 0};
    }

    if (!network_repo_.create(name, owner_id)) {
        return {"Failed to create network", 0};
    }

    // Find the network we just created (last one by this owner)
    auto networks = network_repo_.find_by_owner(owner_id);
    if (networks.empty()) {
        return {"Failed to find created network", 0};
    }

    auto& net = networks.back();
    spdlog::info("Network created: id={}, name={}, owner_id={}", net.id, name, owner_id);
    return {"", net.id};
}

std::optional<Network> NetworkService::get_network(int64_t network_id) {
    return network_repo_.find_by_id(network_id);
}

std::vector<Network> NetworkService::list_user_networks(int64_t owner_id) {
    return network_repo_.find_by_owner(owner_id);
}

std::string NetworkService::join_network(int64_t network_id, const std::string& node_id) {
    auto network = network_repo_.find_by_id(network_id);
    if (!network.has_value()) {
        return "Network not found";
    }

    auto device = device_repo_.find_by_node_id(node_id);
    if (!device.has_value()) {
        return "Device not found";
    }

    if (network_repo_.is_device_in_network(network_id, device->id)) {
        return "Device already in network";
    }

    if (!network_repo_.add_device(network_id, device->id)) {
        return "Failed to add device to network";
    }

    // Allocate virtual IP if device doesn't have one
    if (device->virtual_ip.empty()) {
        // Ensure IPAM has a pool for this network
        auto net_name = network->name;
        if (ipam_.getSubnet(net_name).empty()) {
            ipam_.addPool(net_name, network->subnet);
        }

        auto ip = ipam_.allocate(net_name);
        if (!ip.empty()) {
            device_repo_.update_virtual_ip(node_id, ip);
            spdlog::info("Device {} allocated virtual IP {} in network {}",
                         node_id, ip, network_id);
        }
    }

    spdlog::info("Device {} joined network {}", node_id, network_id);
    return ""; // success
}

std::string NetworkService::leave_network(int64_t network_id, const std::string& node_id) {
    auto device = device_repo_.find_by_node_id(node_id);
    if (!device.has_value()) {
        return "Device not found";
    }

    if (!network_repo_.remove_device(network_id, device->id)) {
        return "Failed to remove device from network";
    }

    // Free virtual IP
    if (!device->virtual_ip.empty()) {
        auto network = network_repo_.find_by_id(network_id);
        if (network.has_value()) {
            ipam_.free(network->name, device->virtual_ip);
        }
        device_repo_.update_virtual_ip(node_id, "");
        spdlog::info("Device {} freed virtual IP {}", node_id, device->virtual_ip);
    }

    spdlog::info("Device {} left network {}", node_id, network_id);
    return ""; // success
}

std::vector<Device> NetworkService::get_network_devices(int64_t network_id) {
    return network_repo_.get_network_devices(network_id);
}

std::vector<Device> NetworkService::get_network_devices_excluding(int64_t network_id, const std::string& exclude_node_id) {
    auto devices = network_repo_.get_network_devices(network_id);
    std::erase_if(devices, [&](const Device& d) {
        return d.node_id == exclude_node_id;
    });
    return devices;
}
