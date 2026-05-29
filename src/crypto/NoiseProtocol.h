#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class NoiseProtocol {
public:
    static constexpr std::size_t KEY_SIZE = 32;
    static constexpr std::size_t TAG_SIZE = 16;

    enum class Role {
        Initiator,
        Responder
    };

    struct StaticKeypair {
        std::array<std::uint8_t, KEY_SIZE> private_key{};
        std::array<std::uint8_t, KEY_SIZE> public_key{};
    };

    static std::optional<StaticKeypair> generateStaticKeypair();
    static std::optional<StaticKeypair> loadStaticKeypairBase64(const std::string& private_key_base64);
    static std::string encodeBase64(std::span<const std::uint8_t> data);
    static std::optional<std::vector<std::uint8_t>> decodeBase64(const std::string& data);

    NoiseProtocol(Role role,
                  const StaticKeypair& local_static,
                  const std::array<std::uint8_t, KEY_SIZE>& remote_static);

    std::optional<std::vector<std::uint8_t>> buildHandshakeMessage1();
    std::optional<std::vector<std::uint8_t>> handleHandshakeMessage1(std::span<const std::uint8_t> payload);
    bool handleHandshakeMessage2(std::span<const std::uint8_t> payload);

    bool handshakeComplete() const { return handshake_complete_; }

    std::optional<std::vector<std::uint8_t>> encrypt(std::uint64_t nonce,
                                                     std::span<const std::uint8_t> plaintext,
                                                     std::span<const std::uint8_t> ad = {});
    std::optional<std::vector<std::uint8_t>> decrypt(std::uint64_t nonce,
                                                     std::span<const std::uint8_t> ciphertext,
                                                     std::span<const std::uint8_t> ad = {}) const;

private:
    void initializeSymmetric();
    void mixHash(std::span<const std::uint8_t> data);
    bool mixKey(std::span<const std::uint8_t> ikm);
    bool split();

    static std::optional<StaticKeypair> buildStaticKeypairFromPrivate(
        const std::array<std::uint8_t, KEY_SIZE>& private_key);
    static std::optional<std::vector<std::uint8_t>> dh(
        const std::array<std::uint8_t, KEY_SIZE>& private_key,
        const std::array<std::uint8_t, KEY_SIZE>& public_key);
    static std::optional<std::vector<std::uint8_t>> hkdf(
        std::span<const std::uint8_t> chaining_key,
        std::span<const std::uint8_t> ikm,
        std::size_t output_len);
    static std::array<std::uint8_t, KEY_SIZE> sha256(std::span<const std::uint8_t> a,
                                                     std::span<const std::uint8_t> b = {});
    static std::optional<std::vector<std::uint8_t>> aeadEncrypt(
        std::span<const std::uint8_t> key,
        std::uint64_t nonce,
        std::span<const std::uint8_t> plaintext,
        std::span<const std::uint8_t> ad);
    static std::optional<std::vector<std::uint8_t>> aeadDecrypt(
        std::span<const std::uint8_t> key,
        std::uint64_t nonce,
        std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> ad);

    Role role_;
    StaticKeypair local_static_;
    std::array<std::uint8_t, KEY_SIZE> remote_static_{};
    std::optional<StaticKeypair> local_ephemeral_;
    std::array<std::uint8_t, KEY_SIZE> remote_ephemeral_{};

    std::array<std::uint8_t, KEY_SIZE> h_{};
    std::array<std::uint8_t, KEY_SIZE> ck_{};
    std::array<std::uint8_t, KEY_SIZE> send_key_{};
    std::array<std::uint8_t, KEY_SIZE> recv_key_{};

    bool msg1_sent_ = false;
    bool msg1_received_ = false;
    bool handshake_complete_ = false;
};
