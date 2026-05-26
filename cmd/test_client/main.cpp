#include <iostream>
#include <csignal>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "client/TestClient.h"

static std::shared_ptr<TestClient> g_client;

void signal_handler(int) {
    spdlog::info("Shutting down...");
}

int main(int argc, char* argv[]) {
    TestClient::Options opts;
    opts.node_id = "test-node";
    opts.network_id = "home";
    opts.control_url = "127.0.0.1:8080";
    opts.stun_addr = "127.0.0.1:3478";
    opts.udp_port = 0;
    opts.local_addr = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--node-id" && i + 1 < argc) {
            opts.node_id = argv[++i];
        } else if (arg == "--network-id" && i + 1 < argc) {
            opts.network_id = argv[++i];
        } else if (arg == "--control" && i + 1 < argc) {
            opts.control_url = argv[++i];
        } else if (arg == "--stun" && i + 1 < argc) {
            opts.stun_addr = argv[++i];
        } else if (arg == "--udp-port" && i + 1 < argc) {
            opts.udp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--connect" && i + 1 < argc) {
            opts.connect_node_id = argv[++i];
        } else if (arg == "--local-addr" && i + 1 < argc) {
            opts.local_addr = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: test_client \\\n"
                      << "  --node-id node-a \\\n"
                      << "  --network-id home \\\n"
                      << "  --control ws://127.0.0.1:8080/ws \\\n"
                      << "  --stun 127.0.0.1:3478 \\\n"
                      << "  --udp-port 40001 \\\n"
                      << "  --connect node-b \\\n"
                      << "  --local-addr 192.168.1.10:40001\n";
            return 0;
        }
    }

    // ── Logging ──
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("client", console);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    spdlog::info("NAT Test Client starting...");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Auto-assign UDP port if not specified
    if (opts.udp_port == 0) {
        net::io_context tmp_ioc;
        udp::socket tmp_sock(tmp_ioc);
        tmp_sock.open(udp::v4());
        tmp_sock.bind(udp::endpoint(udp::v4(), 0));
        opts.udp_port = tmp_sock.local_endpoint().port();
        tmp_sock.close();
        spdlog::info("Auto-assigned UDP port: {}", opts.udp_port);
    }

    try {
        g_client = std::make_shared<TestClient>();

        if (g_client->run(opts)) {
            spdlog::info("✓ P2P Hole Punch completed successfully!");
            return 0;
        } else {
            spdlog::warn("✗ P2P Hole Punch did not succeed");
            return 1;
        }

    } catch (const std::exception& e) {
        spdlog::critical("Fatal: {}", e.what());
        return 1;
    }
}
