#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct Network {
    int64_t id = 0;
    std::string name;
    int64_t owner_id = 0;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"name", name},
            {"owner_id", owner_id},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};
