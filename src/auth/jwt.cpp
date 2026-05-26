#include "auth/jwt.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <array>
#include <optional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace auth {

// ═══════════════════════════════════════════════════════════════
//  Pure C++ SHA-256 implementation (FIPS 180-4)
//  No external crypto dependency needed.
// ═══════════════════════════════════════════════════════════════

class SHA256 {
public:
    SHA256() {
        reset();
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf_[buflen_++] = data[i];
            if (buflen_ == 64) {
                transform(buf_);
                buflen_ = 0;
            }
        }
    }

    void update(const std::string& data) {
        update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    std::array<uint8_t, 32> digest() {
        // Pad
        uint64_t bits = (totsize_ + buflen_) * 8;
        buf_[buflen_++] = 0x80;
        if (buflen_ > 56) {
            while (buflen_ < 64) buf_[buflen_++] = 0;
            transform(buf_);
            buflen_ = 0;
        }
        while (buflen_ < 56) buf_[buflen_++] = 0;
        for (int i = 7; i >= 0; --i) {
            buf_[56 + i] = bits & 0xFF;
            bits >>= 8;
        }
        transform(buf_);

        std::array<uint8_t, 32> result;
        for (int i = 0; i < 8; ++i) {
            result[i*4]   = (h_[i] >> 24) & 0xFF;
            result[i*4+1] = (h_[i] >> 16) & 0xFF;
            result[i*4+2] = (h_[i] >> 8) & 0xFF;
            result[i*4+3] = h_[i] & 0xFF;
        }
        return result;
    }

    std::string hex_digest() {
        auto d = digest();
        std::ostringstream oss;
        for (auto c : d) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        return oss.str();
    }

private:
    void reset() {
        h_[0] = 0x6a09e667;
        h_[1] = 0xbb67ae85;
        h_[2] = 0x3c6ef372;
        h_[3] = 0xa54ff53a;
        h_[4] = 0x510e527f;
        h_[5] = 0x9b05688c;
        h_[6] = 0x1f83d9ab;
        h_[7] = 0x5be0cd19;
        buflen_ = 0;
        totsize_ = 0;
    }

    void transform(const uint8_t* block) {
        totsize_ += 64;
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + K_[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    static uint32_t ror(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    uint32_t h_[8];
    uint8_t buf_[64];
    size_t buflen_ = 0;
    uint64_t totsize_ = 0;

    static const uint32_t K_[64];
};

const uint32_t SHA256::K_[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static std::string hmac_sha256(const std::string& key, const std::string& data) {
    // HMAC(K, m) = H((K' ⊕ opad) || H((K' ⊕ ipad) || m))
    std::array<uint8_t, 64> kpad;
    std::string key_copy = key;

    if (key.size() > 64) {
        SHA256 sha;
        sha.update(key);
        auto kd = sha.digest();
        key_copy = std::string(kd.begin(), kd.end());
    }

    // Pad key to 64 bytes
    std::fill(kpad.begin(), kpad.end(), 0);
    std::memcpy(kpad.data(), key_copy.data(), key_copy.size());

    // Inner: H((K ⊕ ipad) || m)
    std::array<uint8_t, 64> ipad;
    for (size_t i = 0; i < 64; ++i) ipad[i] = kpad[i] ^ 0x36;

    SHA256 inner;
    inner.update(ipad.data(), 64);
    inner.update(data);

    // Outer: H((K ⊕ opad) || inner_result)
    std::array<uint8_t, 64> opad;
    for (size_t i = 0; i < 64; ++i) opad[i] = kpad[i] ^ 0x5c;

    auto inner_hash = inner.digest();

    SHA256 outer;
    outer.update(opad.data(), 64);
    outer.update(std::string(inner_hash.begin(), inner_hash.end()));

    auto result = outer.digest();
    return std::string(result.begin(), result.end());
}

// ── Base64URL encode/decode ──────────────────────────────────

static const char BASE64_URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64_encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    unsigned char buf[3];
    for (size_t i = 0; i < input.size(); i += 3) {
        int n = std::min<size_t>(3, input.size() - i);
        buf[0] = input[i];
        buf[1] = n > 1 ? input[i + 1] : 0;
        buf[2] = n > 2 ? input[i + 2] : 0;
        out.push_back(BASE64_URL[buf[0] >> 2]);
        out.push_back(BASE64_URL[((buf[0] & 3) << 4) | (buf[1] >> 4)]);
        out.push_back(n > 1 ? BASE64_URL[((buf[1] & 15) << 2) | (buf[2] >> 6)] : '=');
        out.push_back(n > 2 ? BASE64_URL[buf[2] & 63] : '=');
    }
    return out;
}

static std::string base64_decode(const std::string& input) {
    // Decode URL-safe base64 (RFC 4648 §5)
    // Index = (unsigned char)c - 45, where input chars range from '-' (45) to 'z' (122)
    // Total range is 78 chars (indices 0-77)
    static const unsigned char D64[] = {
        // '-'  '.'  '/'  '0'  '1'  '2'  '3'  '4'  '5'  '6'  '7'  '8'  '9'  ':'  ';'  '<'
           62,  0,   0,   52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  0,   0,   0,
        // '='  '>'  '?'  '@'  'A'  'B'  'C'  'D'  'E'  'F'  'G'  'H'  'I'  'J'  'K'  'L'
           0,   0,   0,   0,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,
        // 'M'  'N'  'O'  'P'  'Q'  'R'  'S'  'T'  'U'  'V'  'W'  'X'  'Y'  'Z'  '['  '\'
           12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  0,   0,
        // ']'  '^'  '_'  '`'  'a'  'b'  'c'  'd'  'e'  'f'  'g'  'h'  'i'  'j'  'k'  'l'
           0,   0,   63,  0,   26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,
        // 'm'  'n'  'o'  'p'  'q'  'r'  's'  't'  'u'  'v'  'w'  'x'  'y'  'z'
           38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51
    };
    std::string out;
    out.reserve(input.size());
    unsigned char buf[4];
    for (size_t i = 0; i < input.size(); i += 4) {
        for (int j = 0; j < 4; ++j) {
            char c = input[i + j];
            buf[j] = (c == '=') ? 0 : D64[(unsigned char)c - 45];
        }
        out.push_back((buf[0] << 2) | (buf[1] >> 4));
        if (i + 2 < input.size() && input[i + 2] != '=') {
            out.push_back(((buf[1] & 15) << 4) | (buf[2] >> 2));
        }
        if (i + 3 < input.size() && input[i + 3] != '=') {
            out.push_back(((buf[2] & 3) << 6) | buf[3]);
        }
    }
    return out;
}

// ── JwtManager ─────────────────────────────────────────────

JwtManager::JwtManager(const JwtConfig& config)
    : config_(config) {}

std::string JwtManager::generate_token(int64_t user_id, const std::string& username) const {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::hours(config_.expiration_hours);
    auto exp_ts = std::chrono::duration_cast<std::chrono::seconds>(exp.time_since_epoch()).count();

    nlohmann::json header = {
        {"alg", "HS256"},
        {"typ", "JWT"}
    };
    nlohmann::json payload = {
        {"user_id", user_id},
        {"username", username},
        {"exp", exp_ts},
        {"iat", std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()}
    };

    auto strip_pad = [](std::string& s) {
        while (!s.empty() && s.back() == '=') s.pop_back();
    };

    std::string encoded_header = base64_encode(header.dump());
    std::string encoded_payload = base64_encode(payload.dump());
    strip_pad(encoded_header);
    strip_pad(encoded_payload);

    std::string signing_input = encoded_header + "." + encoded_payload;

    std::string signature = hmac_sha256(config_.secret, signing_input);
    std::string encoded_signature = base64_encode(signature);
    strip_pad(encoded_signature);

    return signing_input + "." + encoded_signature;
}

std::optional<JwtPayload> JwtManager::verify_token(const std::string& token) const {
    // Split
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    std::string encoded_header = token.substr(0, dot1);
    std::string encoded_payload = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string encoded_signature = token.substr(dot2 + 1);
    std::string signing_input = encoded_header + "." + encoded_payload;

    // Verify signature
    std::string expected_sig = hmac_sha256(config_.secret, signing_input);
    std::string encoded_expected = base64_encode(expected_sig);
    while (!encoded_expected.empty() && encoded_expected.back() == '=') {
        encoded_expected.pop_back();
    }
    if (encoded_signature != encoded_expected) {
        spdlog::warn("JWT signature mismatch");
        return std::nullopt;
    }

    // Parse payload (restore padding for decode)
    try {
        std::string padded_payload = encoded_payload;
        while (padded_payload.size() % 4) padded_payload.push_back('=');
        std::string payload_json = base64_decode(padded_payload);
        auto payload = nlohmann::json::parse(payload_json);

        // Check expiration
        auto exp = payload["exp"].get<int64_t>();
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now >= exp) {
            spdlog::warn("JWT expired");
            return std::nullopt;
        }

        JwtPayload result;
        result.user_id = payload["user_id"].get<int64_t>();
        result.username = payload["username"].get<std::string>();
        result.exp = exp;
        return result;
    } catch (const std::exception& e) {
        spdlog::warn("JWT parse error: {}", e.what());
        return std::nullopt;
    }
}

} // namespace auth
