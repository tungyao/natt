#include "crypto/NoiseProtocol.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" const EVP_CIPHER* EVP_chacha20_poly1305(void);
struct ossl_lib_ctx_st;
extern "C" EVP_PKEY* EVP_PKEY_new_raw_private_key_ex(
    ossl_lib_ctx_st* libctx,
    const char* keytype,
    const char* propquery,
    const unsigned char* priv,
    size_t len);
extern "C" EVP_PKEY* EVP_PKEY_new_raw_public_key_ex(
    ossl_lib_ctx_st* libctx,
    const char* keytype,
    const char* propquery,
    const unsigned char* pub,
    size_t len);

namespace {

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

constexpr char kProtocolName[] = "Noise_KK_25519_ChaChaPoly_SHA256";

std::array<std::uint8_t, 12> build_nonce(std::uint64_t nonce) {
    std::array<std::uint8_t, 12> out{};
    for (int i = 0; i < 8; ++i) {
        out[4 + i] = static_cast<std::uint8_t>((nonce >> (i * 8)) & 0xffU);
    }
    return out;
}

std::array<std::uint8_t, NoiseProtocol::KEY_SIZE> sha256_bytes(
    std::span<const std::uint8_t> a,
    std::span<const std::uint8_t> b = {}) {
    EvpMdCtxPtr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    std::array<std::uint8_t, NoiseProtocol::KEY_SIZE> out{};
    unsigned int written = 0;

    if (!ctx ||
        EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) <= 0 ||
        EVP_DigestUpdate(ctx.get(), a.data(), a.size()) <= 0 ||
        (!b.empty() && EVP_DigestUpdate(ctx.get(), b.data(), b.size()) <= 0) ||
        EVP_DigestFinal_ex(ctx.get(), out.data(), &written) <= 0 ||
        written != out.size()) {
        spdlog::error("NoiseProtocol: SHA256 failed");
        out.fill(0);
    }

    return out;
}

std::array<std::uint8_t, NoiseProtocol::KEY_SIZE> hmac_sha256(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> data) {
    constexpr std::size_t block_size = 64;
    std::array<std::uint8_t, block_size> key_block{};
    if (key.size() > block_size) {
        auto hashed = sha256_bytes(key);
        std::copy(hashed.begin(), hashed.end(), key_block.begin());
    } else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::array<std::uint8_t, block_size> ipad{};
    std::array<std::uint8_t, block_size> opad{};
    for (std::size_t i = 0; i < block_size; ++i) {
        ipad[i] = static_cast<std::uint8_t>(key_block[i] ^ 0x36U);
        opad[i] = static_cast<std::uint8_t>(key_block[i] ^ 0x5cU);
    }

    std::vector<std::uint8_t> inner(ipad.begin(), ipad.end());
    inner.insert(inner.end(), data.begin(), data.end());
    auto inner_hash = sha256_bytes(inner);

    std::vector<std::uint8_t> outer(opad.begin(), opad.end());
    outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
    return sha256_bytes(outer);
}

} // namespace

std::optional<NoiseProtocol::StaticKeypair> NoiseProtocol::generateStaticKeypair() {
    std::array<std::uint8_t, KEY_SIZE> private_key{};
    if (RAND_bytes(private_key.data(), static_cast<int>(private_key.size())) != 1) {
        spdlog::error("NoiseProtocol: RAND_bytes failed");
        return std::nullopt;
    }
    return buildStaticKeypairFromPrivate(private_key);
}

std::optional<NoiseProtocol::StaticKeypair> NoiseProtocol::loadStaticKeypairBase64(
    const std::string& private_key_base64) {
    auto decoded = decodeBase64(private_key_base64);
    if (!decoded || decoded->size() != KEY_SIZE) {
        spdlog::error("NoiseProtocol: invalid static private key, expected 32 raw bytes in base64");
        return std::nullopt;
    }

    std::array<std::uint8_t, KEY_SIZE> private_key{};
    std::copy(decoded->begin(), decoded->end(), private_key.begin());
    return buildStaticKeypairFromPrivate(private_key);
}

std::string NoiseProtocol::encodeBase64(std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return {};
    }

    std::string out((data.size() + 2) / 3 * 4, '\0');
    const auto written = static_cast<std::size_t>(EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()),
        data.data(),
        static_cast<int>(data.size())));
    out.resize(written);
    return out;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::decodeBase64(const std::string& data) {
    if (data.empty()) {
        return std::vector<std::uint8_t>{};
    }

    std::vector<std::uint8_t> out((data.size() + 3) / 4 * 3);
    const auto decoded = EVP_DecodeBlock(
        out.data(),
        reinterpret_cast<const unsigned char*>(data.data()),
        static_cast<int>(data.size()));
    if (decoded < 0) {
        return std::nullopt;
    }

    std::size_t trim = 0;
    if (!data.empty() && data.back() == '=') ++trim;
    if (data.size() > 1 && data[data.size() - 2] == '=') ++trim;
    out.resize(static_cast<std::size_t>(decoded) - trim);
    return out;
}

NoiseProtocol::NoiseProtocol(Role role,
                             const StaticKeypair& local_static,
                             const std::array<std::uint8_t, KEY_SIZE>& remote_static)
    : role_(role)
    , local_static_(local_static)
    , remote_static_(remote_static) {
    initializeSymmetric();
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::buildHandshakeMessage1() {
    if (role_ != Role::Initiator || msg1_sent_ || handshake_complete_) {
        return std::nullopt;
    }

    local_ephemeral_ = generateStaticKeypair();
    if (!local_ephemeral_) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> msg(local_ephemeral_->public_key.begin(), local_ephemeral_->public_key.end());
    mixHash(msg);

    auto es = dh(local_ephemeral_->private_key, remote_static_);
    auto ss = dh(local_static_.private_key, remote_static_);
    if (!es || !ss || !mixKey(*es) || !mixKey(*ss)) {
        return std::nullopt;
    }

    msg1_sent_ = true;
    return msg;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::handleHandshakeMessage1(
    std::span<const std::uint8_t> payload) {
    if (role_ != Role::Responder || handshake_complete_) {
        return std::nullopt;
    }
    if (payload.size() != KEY_SIZE) {
        spdlog::warn("NoiseProtocol: unexpected handshake message1 size={}", payload.size());
        return std::nullopt;
    }

    std::copy(payload.begin(), payload.end(), remote_ephemeral_.begin());
    msg1_received_ = true;
    mixHash(payload);

    auto es = dh(local_static_.private_key, remote_ephemeral_);
    auto ss = dh(local_static_.private_key, remote_static_);
    if (!es || !ss || !mixKey(*es) || !mixKey(*ss)) {
        return std::nullopt;
    }

    local_ephemeral_ = generateStaticKeypair();
    if (!local_ephemeral_) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> response(local_ephemeral_->public_key.begin(), local_ephemeral_->public_key.end());
    mixHash(response);

    auto ee = dh(local_ephemeral_->private_key, remote_ephemeral_);
    auto se = dh(local_static_.private_key, remote_ephemeral_);
    if (!ee || !se || !mixKey(*ee) || !mixKey(*se) || !split()) {
        return std::nullopt;
    }

    handshake_complete_ = true;
    return response;
}

bool NoiseProtocol::handleHandshakeMessage2(std::span<const std::uint8_t> payload) {
    if (role_ != Role::Initiator || !msg1_sent_ || handshake_complete_) {
        return false;
    }
    if (payload.size() != KEY_SIZE || !local_ephemeral_) {
        spdlog::warn("NoiseProtocol: unexpected handshake message2 size={}", payload.size());
        return false;
    }

    std::copy(payload.begin(), payload.end(), remote_ephemeral_.begin());
    mixHash(payload);

    auto ee = dh(local_ephemeral_->private_key, remote_ephemeral_);
    auto se = dh(local_ephemeral_->private_key, remote_static_);
    if (!ee || !se || !mixKey(*ee) || !mixKey(*se) || !split()) {
        return false;
    }

    handshake_complete_ = true;
    return true;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::encrypt(std::uint64_t nonce,
                                                                std::span<const std::uint8_t> plaintext,
                                                                std::span<const std::uint8_t> ad) {
    if (!handshake_complete_) {
        return std::nullopt;
    }
    return aeadEncrypt(send_key_, nonce, plaintext, ad);
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::decrypt(std::uint64_t nonce,
                                                                std::span<const std::uint8_t> ciphertext,
                                                                std::span<const std::uint8_t> ad) const {
    if (!handshake_complete_) {
        return std::nullopt;
    }
    return aeadDecrypt(recv_key_, nonce, ciphertext, ad);
}

void NoiseProtocol::initializeSymmetric() {
    const auto protocol_name_len = std::strlen(kProtocolName);
    if (protocol_name_len <= h_.size()) {
        std::fill(h_.begin(), h_.end(), 0);
        std::memcpy(h_.data(), kProtocolName, protocol_name_len);
    } else {
        h_ = sha256(
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(kProtocolName),
                protocol_name_len));
    }
    ck_ = h_;

    const auto& initiator_static =
        role_ == Role::Initiator ? local_static_.public_key : remote_static_;
    const auto& responder_static =
        role_ == Role::Initiator ? remote_static_ : local_static_.public_key;

    mixHash(initiator_static);
    mixHash(responder_static);
}

void NoiseProtocol::mixHash(std::span<const std::uint8_t> data) {
    h_ = sha256(h_, data);
}

bool NoiseProtocol::mixKey(std::span<const std::uint8_t> ikm) {
    auto derived = hkdf(ck_, ikm, KEY_SIZE * 2);
    if (!derived || derived->size() != KEY_SIZE * 2) {
        return false;
    }

    std::copy_n(derived->begin(), KEY_SIZE, ck_.begin());
    return true;
}

bool NoiseProtocol::split() {
    auto derived = hkdf(ck_, {}, KEY_SIZE * 2);
    if (!derived || derived->size() != KEY_SIZE * 2) {
        return false;
    }

    if (role_ == Role::Initiator) {
        std::copy_n(derived->begin(), KEY_SIZE, send_key_.begin());
        std::copy_n(derived->begin() + KEY_SIZE, KEY_SIZE, recv_key_.begin());
    } else {
        std::copy_n(derived->begin(), KEY_SIZE, recv_key_.begin());
        std::copy_n(derived->begin() + KEY_SIZE, KEY_SIZE, send_key_.begin());
    }
    return true;
}

std::optional<NoiseProtocol::StaticKeypair> NoiseProtocol::buildStaticKeypairFromPrivate(
    const std::array<std::uint8_t, KEY_SIZE>& private_key) {
    EvpPkeyPtr key(
        EVP_PKEY_new_raw_private_key_ex(nullptr, "X25519", nullptr, private_key.data(), private_key.size()),
        EVP_PKEY_free);
    if (!key) {
        spdlog::error("NoiseProtocol: EVP_PKEY_new_raw_private_key_ex failed");
        return std::nullopt;
    }

    StaticKeypair out;
    out.private_key = private_key;
    std::size_t public_len = out.public_key.size();
    if (EVP_PKEY_get_raw_public_key(key.get(), out.public_key.data(), &public_len) <= 0 ||
        public_len != out.public_key.size()) {
        spdlog::error("NoiseProtocol: EVP_PKEY_get_raw_public_key failed");
        return std::nullopt;
    }

    return out;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::dh(
    const std::array<std::uint8_t, KEY_SIZE>& private_key,
    const std::array<std::uint8_t, KEY_SIZE>& public_key) {
    EvpPkeyPtr local(
        EVP_PKEY_new_raw_private_key_ex(nullptr, "X25519", nullptr, private_key.data(), private_key.size()),
        EVP_PKEY_free);
    EvpPkeyPtr peer(
        EVP_PKEY_new_raw_public_key_ex(nullptr, "X25519", nullptr, public_key.data(), public_key.size()),
        EVP_PKEY_free);
    if (!local || !peer) {
        spdlog::error("NoiseProtocol: failed to create X25519 EVP_PKEY");
        return std::nullopt;
    }

    EvpPkeyCtxPtr ctx(EVP_PKEY_CTX_new(local.get(), nullptr), EVP_PKEY_CTX_free);
    if (!ctx ||
        EVP_PKEY_derive_init(ctx.get()) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) <= 0) {
        spdlog::error("NoiseProtocol: EVP_PKEY_derive_init/set_peer failed");
        return std::nullopt;
    }

    std::size_t secret_len = KEY_SIZE;
    std::vector<std::uint8_t> secret(secret_len);
    if (EVP_PKEY_derive(ctx.get(), secret.data(), &secret_len) <= 0) {
        spdlog::error("NoiseProtocol: EVP_PKEY_derive failed");
        return std::nullopt;
    }
    secret.resize(secret_len);
    return secret;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::hkdf(
    std::span<const std::uint8_t> chaining_key,
    std::span<const std::uint8_t> ikm,
    std::size_t output_len) {
    constexpr std::size_t hash_len = KEY_SIZE;
    auto prk = hmac_sha256(chaining_key, ikm);

    std::vector<std::uint8_t> out;
    out.reserve(output_len);
    std::vector<std::uint8_t> previous;

    for (std::uint8_t counter = 1; out.size() < output_len; ++counter) {
        std::vector<std::uint8_t> block(previous.begin(), previous.end());
        block.push_back(counter);
        auto step = hmac_sha256(prk, block);
        previous.assign(step.begin(), step.end());
        const auto need = std::min<std::size_t>(hash_len, output_len - out.size());
        out.insert(out.end(), previous.begin(), previous.begin() + need);
    }
    return out;
}

std::array<std::uint8_t, NoiseProtocol::KEY_SIZE> NoiseProtocol::sha256(
    std::span<const std::uint8_t> a,
    std::span<const std::uint8_t> b) {
    return sha256_bytes(a, b);
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::aeadEncrypt(
    std::span<const std::uint8_t> key,
    std::uint64_t nonce,
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> ad) {
    auto nonce_bytes = build_nonce(nonce);
    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        return std::nullopt;
    }

    if (EVP_EncryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, nonce_bytes.size(), nullptr) <= 0 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce_bytes.data()) <= 0) {
        return std::nullopt;
    }

    int out_len = 0;
    if (!ad.empty() && EVP_EncryptUpdate(ctx.get(), nullptr, &out_len, ad.data(), ad.size()) <= 0) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> ciphertext(plaintext.size() + TAG_SIZE);
    int total = 0;
    if (!plaintext.empty() &&
        EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len, plaintext.data(), plaintext.size()) <= 0) {
        return std::nullopt;
    }
    total += out_len;

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + total, &out_len) <= 0) {
        return std::nullopt;
    }
    total += out_len;

    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(),
            EVP_CTRL_AEAD_GET_TAG,
            TAG_SIZE,
            ciphertext.data() + total) <= 0) {
        return std::nullopt;
    }
    ciphertext.resize(static_cast<std::size_t>(total + TAG_SIZE));
    return ciphertext;
}

std::optional<std::vector<std::uint8_t>> NoiseProtocol::aeadDecrypt(
    std::span<const std::uint8_t> key,
    std::uint64_t nonce,
    std::span<const std::uint8_t> ciphertext,
    std::span<const std::uint8_t> ad) {
    if (ciphertext.size() < TAG_SIZE) {
        return std::nullopt;
    }

    auto nonce_bytes = build_nonce(nonce);
    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) {
        return std::nullopt;
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, nonce_bytes.size(), nullptr) <= 0 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce_bytes.data()) <= 0) {
        return std::nullopt;
    }

    int out_len = 0;
    if (!ad.empty() && EVP_DecryptUpdate(ctx.get(), nullptr, &out_len, ad.data(), ad.size()) <= 0) {
        return std::nullopt;
    }

    const auto plaintext_len = ciphertext.size() - TAG_SIZE;
    std::vector<std::uint8_t> plaintext(plaintext_len);
    int total = 0;
    if (plaintext_len > 0 &&
        EVP_DecryptUpdate(
            ctx.get(),
            plaintext.data(),
            &out_len,
            ciphertext.data(),
            static_cast<int>(plaintext_len)) <= 0) {
        return std::nullopt;
    }
    total += out_len;

    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(),
            EVP_CTRL_AEAD_SET_TAG,
            TAG_SIZE,
            const_cast<std::uint8_t*>(ciphertext.data() + plaintext_len)) <= 0) {
        return std::nullopt;
    }

    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total, &out_len) <= 0) {
        return std::nullopt;
    }
    total += out_len;
    plaintext.resize(static_cast<std::size_t>(total));
    return plaintext;
}
