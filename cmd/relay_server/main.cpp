#include <iostream>
#include <csignal>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/asio.hpp>

#include "relay/RelayServer.h"

namespace net = boost::asio;

static std::shared_ptr<RelayServer> g_relay_server;
static net::io_context* g_ioc = nullptr;

void signal_handler(int sig) {
    spdlog::info("RelayServer: received signal {}, shutting down...", sig);
    if (g_relay_server) {
        g_relay_server->stop();
    }
    if (g_ioc) {
        g_ioc->stop();
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 7000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "Usage: relay_server [--port PORT]\n"
                      << "  UDP Relay Server for NAT traversal fallback\n"
                      << "  Default port: 7000\n";
            return 0;
        }
    }

    // ── Logging ──
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("relay", console);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [relay] %v");
    spdlog::set_level(spdlog::level::info);

    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  UDP Relay Server starting on port {}", port);
    spdlog::info("═══════════════════════════════════════════");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        net::io_context ioc;
        g_ioc = &ioc;

        auto relay_server = std::make_shared<RelayServer>(ioc, port);
        g_relay_server = relay_server;

        relay_server->start();

        spdlog::info("RelayServer: running on UDP port {}...", port);
        ioc.run();

    } catch (const std::exception& e) {
        spdlog::critical("RelayServer: fatal error: {}", e.what());
        return 1;
    }

    spdlog::info("RelayServer: shutdown complete");
    return 0;
}
