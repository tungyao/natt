#pragma once

#include <string>
#include <optional>

namespace crypto {

struct CertResult {
    std::string cert_pem;
    std::string key_pem;
    std::string error;
};

class CertGenerator {
public:
    // Generate a self-signed ECDSA certificate (prime256v1).
    // If cert_path/key_path are non-empty, PEM files are written to disk.
    // Returns the PEM strings (and optionally error).
    static CertResult generate(const std::string& cert_path = {},
                               const std::string& key_path = {},
                               const std::string& common_name = "NATMesh Server",
                               int valid_days = 3650);

    // Load cert PEM from file (for admin panel display)
    static std::optional<std::string> loadCertPEM(const std::string& cert_path);

    // Check if cert file exists and is valid
    static bool certExists(const std::string& cert_path);
    static bool keyExists(const std::string& key_path);
};

} // namespace crypto
