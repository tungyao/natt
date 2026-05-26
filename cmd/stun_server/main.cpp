#include <iostream>
#include <csignal>
#include <memory>

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "stun/StunServer.h"

namespace net = boost::asio;

static std::shared_ptr<StunServer> g_stun;
static net::io_context* g_ioc = nullptr;

void signal_handler(int sig) {
    spdlog::info("Signal {} received, shutting down...", sig);
    if (g_stun) g_stun->stop();
    if (g_ioc) g_ioc->stop();
}

int main(int argc, char* argv[]) {
    // ── Parse args ──
    std::string listen_host = "0.0.0.0";
    uint16_t port = 3478;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) {
            listen_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "Usage: stun_server [--listen 0.0.0.0] [--port 3478]\n";
            return 0;
        }
    }

    // ── Logging ──
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("stun", console);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    spdlog::info("STUN Server starting...");

    // ── Signals ──
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        net::io_context ioc(1);
        g_ioc = &ioc;

        g_stun = std::make_shared<StunServer>(ioc, listen_host, port);
        g_stun->start();

        spdlog::info("STUN Server started on {}:{}", listen_host, port);
        ioc.run();

    } catch (const std::exception& e) {
        spdlog::critical("Fatal: {}", e.what());
        return 1;
    }

    spdlog::info("STUN Server stopped");
    return 0;
}
