#pragma once

#include <string>
#include <optional>
#include "config/config.h"

namespace auth {

struct JwtPayload {
    int64_t user_id = 0;
    std::string username;
    int64_t exp = 0;
};

class JwtManager {
public:
    explicit JwtManager(const JwtConfig& config);

    std::string generate_token(int64_t user_id, const std::string& username) const;
    std::optional<JwtPayload> verify_token(const std::string& token) const;

private:
    JwtConfig config_;
};

} // namespace auth
