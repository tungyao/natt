#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <fstream>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <yaml-cpp/yaml.h>

#include "client/TestClient.h"

static std::shared_ptr<TestClient> g_client;
static volatile std::sig_atomic_t g_stop_requested = 0;

void signal_handler(int) {
    g_stop_requested = 1;
}

static void apply_yaml_config(TestClient::Options& opts, const std::string& path) {
    YAML::Node config = YAML::LoadFile(path);

    if (config["node_id"])              opts.node_id = config["node_id"].as<std::string>();
    if (config["network_id"])           opts.network_id = config["network_id"].as<std::string>();
    if (config["control"])              opts.control_url = config["control"].as<std::string>();
    if (config["stun"])                 opts.stun_addr = config["stun"].as<std::string>();
    if (config["udp_port"])             opts.udp_port = config["udp_port"].as<uint16_t>();
    if (config["connect"])              opts.connect_node_id = config["connect"].as<std::string>();
    if (config["local_addr"])           opts.local_addr = config["local_addr"].as<std::string>();
    if (config["noise_private_key"])    opts.noise_private_key = config["noise_private_key"].as<std::string>();
    if (config["relay"])                opts.relay_addr = config["relay"].as<std::string>();
    if (config["wss"])                  opts.use_ssl = config["wss"].as<bool>();
    if (config["cert_file"])            opts.cert_file = config["cert_file"].as<std::string>();

    if (config["tun"]) {
        auto tun = config["tun"];
        if (tun["enable"])              opts.enable_tun = tun["enable"].as<bool>();
        if (tun["name"])                opts.tun_name = tun["name"].as<std::string>();
        if (tun["mtu"])                 opts.tun_mtu = tun["mtu"].as<int>();
    }
}

int main(int argc, char* argv[]) {
    TestClient::Options opts;
    opts.node_id = "test-node";
    opts.network_id = "home";
    opts.control_url = "127.0.0.1:8080";
    opts.stun_addr = "127.0.0.1:3478";
    opts.udp_port = 0;
    opts.local_addr = "";

    // First pass: load YAML config if specified
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            apply_yaml_config(opts, argv[++i]);
            break;
        }
    }

    // Second pass: CLI args override config file + defaults
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" || arg == "-c") {
            ++i; // skip value, already consumed
        } else if (arg == "--node-id" && i + 1 < argc) {
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
        } else if (arg == "--relay" && i + 1 < argc) {
            opts.relay_addr = argv[++i];
        } else if (arg == "--noise-private-key" && i + 1 < argc) {
            opts.noise_private_key = argv[++i];
        } else if (arg == "--wss") {
            opts.use_ssl = true;
        } else if (arg == "--cert-file" && i + 1 < argc) {
            opts.cert_file = argv[++i];
        } else if (arg == "--tun") {
            opts.enable_tun = true;
        } else if (arg == "--tun-name" && i + 1 < argc) {
            opts.tun_name = argv[++i];
        } else if (arg == "--tun-mtu" && i + 1 < argc) {
            opts.tun_mtu = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << R"(NAT traversal test client

Usage: test_client [options]

Options:
  -c, --config <file>         Path to YAML config file
  --node-id <id>              Node identifier (default: test-node)
  --network-id <id>           Network identifier (default: home)
  --control <addr>            Control server address (default: 127.0.0.1:8080)
                             (use wss:// prefix for TLS, e.g. wss://host:443/ws)
  --stun <addr>               STUN server address (default: 127.0.0.1:3478)
  --udp-port <port>           UDP listen port (0 = auto-assign, default: 0)
  --connect <node-id>         Peer node ID to connect to
  --local-addr <ip:port>      Local address to advertise
  --noise-private-key <key>   Base64-encoded X25519 private key
  --relay <addr>              Relay server address
  --wss                       Force WSS (TLS) even if control URL has no wss://
  --cert-file <file>          CA certificate PEM file for server verification
  --tun                       Enable TUN interface
  --tun-name <name>           TUN interface name
  --tun-mtu <mtu>             TUN interface MTU (default: 1500)
  --help                      Show this help message and exit

YAML config file format:
  node_id: "node-a"
  network_id: "home"
  control: "wss://127.0.0.1:443/ws"
  stun: "127.0.0.1:3478"
  udp_port: 40001
  connect: "node-b"
  local_addr: "192.168.1.10:40001"
  noise_private_key: "<base64-key>"
  relay: "127.0.0.1:7000"
  wss: true
  cert_file: "/path/to/ca.pem"
  tun:
    enable: false
    name: "nat%d"
    mtu: 1300

CLI flags override values from the config file.
)";
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

    std::atomic<bool> monitor_done{false};
    std::thread signal_monitor;
    auto join_signal_monitor = [&]() {
        monitor_done = true;
        if (signal_monitor.joinable()) {
            signal_monitor.join();
        }
    };

    try {
        g_client = std::make_shared<TestClient>();

        signal_monitor = std::thread([&monitor_done]() {
            bool logged = false;
            while (!monitor_done.load()) {
                if (g_stop_requested) {
                    if (!logged) {
                        spdlog::info("Shutting down...");
                        logged = true;
                    }
                    if (g_client) {
                        g_client->stop();
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        auto result = g_client->run(opts);
        join_signal_monitor();

        if (g_stop_requested) {
            g_client->stop();
            g_client.reset();
            return 130;
        }

        if (result) {
            if (g_client->punchSuccess()) {
                spdlog::info("✓ P2P Hole Punch completed successfully!");
            } else {
                spdlog::info("✓ Relay fallback completed successfully!");
            }
            g_client.reset();
            return 0;
        } else {
            spdlog::warn("✗ NAT traversal did not succeed");
            g_client.reset();
            return 1;
        }

    } catch (const std::exception& e) {
        join_signal_monitor();
        if (g_client) {
            g_client->stop();
            g_client.reset();
        }
        spdlog::critical("Fatal: {}", e.what());
        return 1;
    }
}
