#include <iostream>
#include <csignal>
#include <memory>

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "config/config.h"
#include "repository/database.h"
#include "repository/user_repo.h"
#include "repository/device_repo.h"
#include "repository/network_repo.h"
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
    // ── Parse config path ──
    std::string config_path = "config.yaml";
    if (argc > 1) {
        config_path = argv[1];
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

        // ── Services ──
        auto user_svc = std::make_unique<UserService>(*user_repo, *jwt);
        auto device_svc = std::make_unique<DeviceService>(*device_repo);
        auto network_svc = std::make_unique<NetworkService>(*network_repo, *device_repo);
        auto peer_mgr = std::make_unique<PeerManager>();

        // ── IO Context ──
        net::io_context ioc(config.server.workers);
        g_ioc = &ioc;

        // ── HTTP Server (also handles WebSocket) ──
        g_http_server = std::make_shared<HttpServer>(
            ioc, config,
            *user_svc, *device_svc, *network_svc,
            *jwt, *peer_mgr
        );
        g_http_server->start();

        spdlog::info("NATMesh Server started successfully");

        // ── Run ──
        ioc.run();

    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }

    spdlog::info("NATMesh Server stopped");
    return 0;
}
