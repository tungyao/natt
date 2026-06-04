#include "crypto/CertGenerator.h"
#include <spdlog/spdlog.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <sstream>

namespace crypto {

namespace {

bool writePEM(const std::string& path, const std::string& pem) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        spdlog::error("CertGenerator: failed to write {}", path);
        return false;
    }
    file << pem;
    return file.good();
}

std::string bioToString(BIO* bio) {
    char* data;
    long len = BIO_get_mem_data(bio, &data);
    return std::string(data, static_cast<size_t>(len));
}

} // namespace

CertResult CertGenerator::generate(const std::string& cert_path,
                                   const std::string& key_path,
                                   const std::string& common_name,
                                   int valid_days) {
    CertResult result;

    // ── Generate EC key ──
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) {
        result.error = "Failed to create EVP_PKEY_CTX";
        return result;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        result.error = "Failed to generate EC key";
        return result;
    }
    EVP_PKEY_CTX_free(ctx);

    // ── Create X.509 certificate ──
    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        result.error = "Failed to create X509 object";
        return result;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * valid_days);
    X509_set_pubkey(cert, pkey);

    // ── Set subject name ──
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(common_name.c_str()),
                               -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("NATMesh"),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name); // self-signed

    // ── Add extensions ──
    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, cert, cert, nullptr, nullptr, 0);

    X509_EXTENSION* ext;

    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_basic_constraints,
                              const_cast<char*>("critical,CA:FALSE"));
    if (ext) X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext);

    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_key_usage,
                              const_cast<char*>("critical,digitalSignature,keyEncipherment"));
    if (ext) X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext);

    ext = X509V3_EXT_conf_nid(nullptr, &v3ctx, NID_ext_key_usage,
                              const_cast<char*>("serverAuth,clientAuth"));
    if (ext) X509_add_ext(cert, ext, -1); X509_EXTENSION_free(ext);

    // ── Sign ──
    if (!X509_sign(cert, pkey, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        result.error = "Failed to sign certificate";
        return result;
    }

    // ── Write to PEM strings ──
    BIO* bio_cert = BIO_new(BIO_s_mem());
    BIO* bio_key = BIO_new(BIO_s_mem());

    PEM_write_bio_X509(bio_cert, cert);
    PEM_write_bio_PrivateKey(bio_key, pkey, nullptr, nullptr, 0, nullptr, nullptr);

    result.cert_pem = bioToString(bio_cert);
    result.key_pem = bioToString(bio_key);

    // ── Write to files ──
    if (!cert_path.empty()) {
        if (!writePEM(cert_path, result.cert_pem)) {
            result.error = "Failed to write cert file: " + cert_path;
        }
    }
    if (!key_path.empty()) {
        if (!writePEM(key_path, result.key_pem)) {
            result.error = "Failed to write key file: " + key_path;
        }
    }

    BIO_free(bio_cert);
    BIO_free(bio_key);
    X509_free(cert);
    EVP_PKEY_free(pkey);

    return result;
}

std::optional<std::string> CertGenerator::loadCertPEM(const std::string& cert_path) {
    std::ifstream file(cert_path);
    if (!file) return std::nullopt;
    std::stringstream ss;
    ss << file.rdbuf();
    auto pem = ss.str();
    if (pem.empty()) return std::nullopt;
    return pem;
}

bool CertGenerator::certExists(const std::string& cert_path) {
    std::ifstream file(cert_path);
    return file.good();
}

bool CertGenerator::keyExists(const std::string& key_path) {
    std::ifstream file(key_path);
    return file.good();
}

} // namespace crypto
