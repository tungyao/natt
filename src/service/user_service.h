#pragma once

#include "repository/user_repo.h"
#include "auth/jwt.h"
#include <string>
#include <optional>

struct AuthResult {
    int64_t user_id;
    std::string username;
    std::string token;
};

class UserService {
public:
    UserService(UserRepo& user_repo, auth::JwtManager& jwt);

    // Returns error string or empty on success
    std::string register_user(const std::string& username, const std::string& password);
    std::optional<AuthResult> login(const std::string& username, const std::string& password);

private:
    UserRepo& user_repo_;
    auth::JwtManager& jwt_;

    static std::string hash_password(const std::string& password);
    static bool verify_password(const std::string& password, const std::string& hash);
};
