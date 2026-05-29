#include "service/user_service.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include <functional>
#include <spdlog/spdlog.h>

UserService::UserService(UserRepo& user_repo, auth::JwtManager& jwt)
    : user_repo_(user_repo), jwt_(jwt) {}

// ── Password hashing using iterative HMAC-SHA256 ───────────

static std::array<uint8_t, 32> hmac_sha256_raw(const std::array<uint8_t, 32>& key,
                                                 const std::string& data) {
    // Simplified HMAC for internal use (key is exactly 32 bytes)
    // This avoids needing the full jwt.h SHA256 in user_service
    struct InnerSHA256 {
        // Same SHA256 as in jwt.cpp - inline for independence
        uint32_t h[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        uint8_t buf[64]{};
        size_t buflen = 0;
        uint64_t totsize = 0;

        static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        void process(const uint8_t* block) {
            totsize += 64;
            uint32_t w[64];
            for (int i = 0; i < 16; ++i)
                w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
            for (int i = 16; i < 64; ++i) {
                uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
                uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
                w[i] = w[i-16] + s0 + w[i-7] + s1;
            }
            uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
            static const uint32_t K[64] = {
                0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
                0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
                0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
                0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
                0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
                0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
                0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
                0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
            };
            for (int i = 0; i < 64; ++i) {
                uint32_t S1 = ror(e,6)^ror(e,11)^ror(e,25);
                uint32_t ch = (e&f)^((~e)&g);
                uint32_t t1 = hh + S1 + ch + K[i] + w[i];
                uint32_t S0 = ror(a,2)^ror(a,13)^ror(a,22);
                uint32_t maj = (a&b)^(a&c)^(b&c);
                uint32_t t2 = S0 + maj;
                hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
            }
            h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
        }

        void write(const uint8_t* data, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                buf[buflen++] = data[i];
                if (buflen == 64) { process(buf); buflen = 0; }
            }
        }

        std::array<uint8_t, 32> finish() {
            uint64_t bits = (totsize + buflen) * 8;
            buf[buflen++] = 0x80;
            if (buflen > 56) {
                while (buflen < 64) buf[buflen++] = 0;
                process(buf); buflen = 0;
            }
            while (buflen < 56) buf[buflen++] = 0;
            for (int i = 7; i >= 0; --i) { buf[56+i] = bits & 0xFF; bits >>= 8; }
            process(buf);
            std::array<uint8_t, 32> r;
            for (int i = 0; i < 8; ++i) {
                r[i*4] = h[i]>>24; r[i*4+1] = h[i]>>16; r[i*4+2] = h[i]>>8; r[i*4+3] = h[i];
            }
            return r;
        }
    };

    // HMAC: H((K ⊕ opad) || H((K ⊕ ipad) || m))
    std::array<uint8_t, 64> kpad{};
    std::memcpy(kpad.data(), key.data(), 32);

    std::array<uint8_t, 64> ipad;
    for (int i = 0; i < 64; ++i) ipad[i] = kpad[i] ^ 0x36;

    InnerSHA256 inner;
    inner.write(ipad.data(), 64);
    inner.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    auto inner_hash = inner.finish();

    std::array<uint8_t, 64> opad;
    for (int i = 0; i < 64; ++i) opad[i] = kpad[i] ^ 0x5c;

    InnerSHA256 outer;
    outer.write(opad.data(), 64);
    outer.write(inner_hash.data(), 32);
    return outer.finish();
}

static std::string bytes_to_hex(const std::array<uint8_t, 32>& bytes) {
    std::ostringstream oss;
    for (auto c : bytes) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return oss.str();
}

static std::array<uint8_t, 16> generate_salt() {
    std::random_device rd;
    std::array<uint8_t, 16> salt;
    for (auto& b : salt) b = rd();
    return salt;
}

std::string UserService::hash_password(const std::string& password) {
    auto salt = generate_salt();

    // Derive key: iterative HMAC (simplified PBKDF2-like)
    std::array<uint8_t, 32> derived{};
    std::string input = password;
    input.append(reinterpret_cast<const char*>(salt.data()), salt.size());

    // Iterate 10000 times using HMAC
    derived = hmac_sha256_raw(derived, input);
    for (int i = 0; i < 9999; ++i) {
        derived = hmac_sha256_raw(derived, std::string(derived.begin(), derived.end()));
    }

    // Format: $sha256$10000$salt_hex$hash_hex
    std::ostringstream oss;
    oss << "$sha256$10000$" << bytes_to_hex(
        *reinterpret_cast<std::array<uint8_t, 32>*>(salt.data())) << "$"
        << bytes_to_hex(derived);
    return oss.str();
}

bool UserService::verify_password(const std::string& password, const std::string& hash) {
    if (hash.substr(0, 8) != "$sha256$") return false;

    auto first = hash.find('$', 1);
    auto second = hash.find('$', first + 1);
    auto third = hash.find('$', second + 1);
    if (third == std::string::npos) return false;

    std::string salt_hex = hash.substr(second + 1, third - second - 1);
    // Salt is stored as 32 hex chars = 16 bytes, but stored in a 32-byte field
    // Let's extract the actual salt bytes
    std::array<uint8_t, 32> salt{};
    for (size_t i = 0; i < 16 && i*2+1 < salt_hex.size(); ++i) {
        char byte[3] = {salt_hex[i*2], salt_hex[i*2+1], 0};
        salt[i] = strtol(byte, nullptr, 16);
    }

    std::string expected_hex = hash.substr(third + 1);

    // Re-derive
    std::array<uint8_t, 32> derived{};
    std::string input = password;
    input.append(reinterpret_cast<const char*>(salt.data()), 16);

    derived = hmac_sha256_raw(derived, input);
    for (int i = 0; i < 9999; ++i) {
        derived = hmac_sha256_raw(derived, std::string(derived.begin(), derived.end()));
    }

    return bytes_to_hex(derived) == expected_hex;
}

std::string UserService::register_user(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {
        return "Username and password are required";
    }
    if (username.size() < 3 || username.size() > 64) {
        return "Username must be 3-64 characters";
    }
    if (password.size() < 6) {
        return "Password must be at least 6 characters";
    }

    auto existing = user_repo_.find_by_username(username);
    if (existing.has_value()) {
        return "Username already exists";
    }

    auto hash = hash_password(password);
    if (!user_repo_.create(username, hash)) {
        return "Failed to create user";
    }

    return ""; // success
}

std::string UserService::ensure_user(const std::string& username, const std::string& password) {
    auto existing = user_repo_.find_by_username(username);
    if (existing.has_value()) {
        return "";
    }
    return register_user(username, password);
}

std::optional<User> UserService::find_user_by_username(const std::string& username) {
    return user_repo_.find_by_username(username);
}

std::optional<AuthResult> UserService::login(const std::string& username, const std::string& password) {
    auto user = user_repo_.find_by_username(username);
    if (!user.has_value()) {
        return std::nullopt;
    }

    if (!verify_password(password, user->password_hash)) {
        return std::nullopt;
    }

    auto token = jwt_.generate_token(user->id, user->username);
    return AuthResult{
        .user_id = user->id,
        .username = user->username,
        .token = token
    };
}
