#pragma once

#include "repository/network_repo.h"
#include "repository/device_repo.h"
#include "ipam/IpAllocator.h"
#include "model/network.h"
#include "model/device.h"
#include <string>
#include <optional>
#include <vector>

class NetworkService {
public:
    NetworkService(NetworkRepo& network_repo, DeviceRepo& device_repo, IpAllocator& ipam);

    // Returns (error, network_id) — network_id 0 on error
    std::pair<std::string, int64_t> create_network(const std::string& name, int64_t owner_id);
    std::optional<Network> get_network(int64_t network_id);
    std::vector<Network> list_user_networks(int64_t owner_id);

    std::string join_network(int64_t network_id, const std::string& node_id);
    std::string leave_network(int64_t network_id, const std::string& node_id);

    std::vector<Device> get_network_devices(int64_t network_id);
    std::vector<Device> get_network_devices_excluding(int64_t network_id, const std::string& exclude_node_id);

private:
    NetworkRepo& network_repo_;
    DeviceRepo& device_repo_;
    IpAllocator& ipam_;
};
