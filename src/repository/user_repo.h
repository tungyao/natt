#pragma once

#include "repository/database.h"
#include "model/user.h"
#include <optional>

class UserRepo {
public:
    explicit UserRepo(Database& db);

    bool create(const std::string& username, const std::string& password_hash);
    std::optional<User> find_by_id(int64_t id);
    std::optional<User> find_by_username(const std::string& username);

private:
    Database& db_;
};
