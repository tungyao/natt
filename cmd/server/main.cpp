#include <iostream>
#include <csignal>
#include <memory>

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "config/config.h"
#include "crypto/CertGenerator.h"
#include "repository/database.h"
#include "repository/user_repo.h"
#include "repository/device_repo.h"
#include "repository/network_repo.h"
#include "ipam/IpAllocator.h"
#include "auth/jwt.h"
#include "service/user_service.h"
#include "service/device_service.h"
#include "service/network_service.h"
#include "service/peer_manager.h"
#include "http/http_server.h"

namespace net = boost::asio;

static std::shared_ptr<HttpServer> g_http_server;
static net::io_context* g_ioc = nullptr;

void signal_handler(int sig) {
    spdlog::info("Signal {} received, shutting down...", sig);
    if (g_ioc) {
        g_ioc->stop();
    }
}

int main(int argc, char* argv[]) {
    // ── Parse CLI flags ──
    std::string config_path = "config.yaml";
    std::string cert_path, key_path;
    bool generate_cert = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--generate-cert") {
            generate_cert = true;
        } else if (arg == "--cert-file" && i + 1 < argc) {
            cert_path = argv[++i];
        } else if (arg == "--key-file" && i + 1 < argc) {
            key_path = argv[++i];
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 < argc) config_path = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: natmesh-server [options] [config.yaml]\n"
                      << "Options:\n"
                      << "  -c, --config <path>     Path to YAML config file (default: config.yaml)\n"
                      << "  --generate-cert         Generate a self-signed TLS certificate and exit\n"
                      << "  --cert-file <path>      Certificate output path (default: server.crt)\n"
                      << "  --key-file <path>       Private key output path (default: server.key)\n"
                      << "  --help                  Show this help\n";
            return 0;
        } else if (arg[0] != '-') {
            config_path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    // ── Generate certificate mode ──
    if (generate_cert) {
        auto cfg = Config::load(config_path);
        if (cert_path.empty()) cert_path = cfg.tls.cert_file;
        if (key_path.empty()) key_path = cfg.tls.key_file;

        auto result = crypto::CertGenerator::generate(cert_path, key_path);
        if (!result.error.empty()) {
            std::cerr << "Failed to generate certificate: " << result.error << "\n";
            return 1;
        }
        std::cout << "Self-signed certificate generated:\n"
                  << "  Cert: " << cert_path << "\n"
                  << "  Key:  " << key_path << "\n";
        return 0;
    }

    // ── Load config ──
    auto config = Config::load(config_path);

    // ── Setup logging ──
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("natmesh", console_sink);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern(config.logging.pattern);

    std::map<std::string, spdlog::level::level_enum> log_levels = {
        {"trace", spdlog::level::trace},
        {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"critical", spdlog::level::critical}
    };
    auto it = log_levels.find(config.logging.level);
    if (it != log_levels.end()) {
        spdlog::set_level(it->second);
    }

    spdlog::info("NATMesh Server v0.1.0 starting...");

    // ── Signal handling ──
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // ── Database ──
        auto database = std::make_unique<Database>(config.database.path);
        if (!database->initialize()) {
            spdlog::error("Failed to initialize database");
            return 1;
        }

        // ── Repositories ──
        auto user_repo = std::make_unique<UserRepo>(*database);
        auto device_repo = std::make_unique<DeviceRepo>(*database);
        auto network_repo = std::make_unique<NetworkRepo>(*database);

        // ── Auth ──
        auto jwt = std::make_unique<auth::JwtManager>(config.jwt);

        // ── IPAM ──
        auto ipam = std::make_unique<IpAllocator>();
        // Seed configured virtual networks. The .1 address is reserved as the server gateway.
        for (const auto& [name, cidr] : config.ipam.pools) {
            ipam->addPool(name, cidr);
        }

        // ── Services ──
        auto user_svc = std::make_unique<UserService>(*user_repo, *jwt);
        auto device_svc = std::make_unique<DeviceService>(*device_repo);
        auto network_svc = std::make_unique<NetworkService>(*network_repo, *device_repo, *ipam);
        auto peer_mgr = std::make_unique<PeerManager>();

        if (config.admin.enabled) {
            auto err = user_svc->ensure_user(config.admin.username, config.admin.password);
            if (!err.empty()) {
                spdlog::error("Failed to ensure bootstrap admin user '{}': {}",
                              config.admin.username, err);
                return 1;
            }
            spdlog::info("Bootstrap admin user ready: username={}", config.admin.username);
        }

        // ── IO Context ──
        net::io_context ioc(config.server.workers);
        g_ioc = &ioc;

        // ── HTTP Server (also handles WebSocket) ──
        g_http_server = std::make_shared<HttpServer>(
            ioc, config,
            *user_svc, *device_svc, *network_svc,
            *ipam, *jwt, *peer_mgr
        );
        g_http_server->start();

        spdlog::info("NATMesh Server started successfully");

        // ── Run ──
        ioc.run();
        g_http_server.reset();

    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }

    spdlog::info("NATMesh Server stopped");
    return 0;
}
