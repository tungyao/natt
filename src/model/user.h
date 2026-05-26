#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct User {
    int64_t id = 0;
    std::string username;
    std::string password_hash;
    std::string created_at;
    std::string updated_at;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"username", username},
            {"created_at", created_at},
            {"updated_at", updated_at}
        };
    }
};
