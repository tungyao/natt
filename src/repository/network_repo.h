#pragma once

#include "repository/database.h"
#include "model/network.h"
#include "model/device.h"
#include <optional>
#include <vector>

class NetworkRepo {
public:
    explicit NetworkRepo(Database& db);

    bool create(const std::string& name, int64_t owner_id);
    std::optional<Network> find_by_id(int64_t id);
    std::vector<Network> find_by_owner(int64_t owner_id);
    bool add_device(int64_t network_id, int64_t device_id);
    bool remove_device(int64_t network_id, int64_t device_id);
    std::vector<Device> get_network_devices(int64_t network_id);
    bool is_device_in_network(int64_t network_id, int64_t device_id);
    bool update_subnet(int64_t network_id, const std::string& subnet);
    bool remove(int64_t network_id);

private:
    Database& db_;
};
