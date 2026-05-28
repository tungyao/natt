#include "client/TestClient.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <sstream>
#include <chrono>

namespace {

std::string hex_encode(const std::vector<uint8_t>& data) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto b : data) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0f]);
    }
    return out;
}

} // namespace

TestClient::TestClient()
{
}

TestClient::~TestClient() {
    stop();
}

void TestClient::stop() {
    if (stopping_.exchange(true)) {
        return;
    }

    if (ws_) {
        ws_->setMessageCallback(nullptr);
        ws_->close();
        ws_->joinReader();
    }

    if (relay_test_timer_) {
        relay_test_timer_->cancel();
    }

    if (tun_) {
        tun_->close();
        tun_ready_ = false;
    }
    tun_starting_ = false;

    if (puncher_) {
        puncher_->setPunchCallback(nullptr);
        puncher_->setPacketCallback(nullptr);
        puncher_->setRelayPacketCallback(nullptr);
        puncher_->setRelayEventCallback(nullptr);
        puncher_->stop();
    }

    puncher_ioc_.stop();
    puncher_work_guard_.reset();
    if (puncher_thread_.joinable() &&
        puncher_thread_.get_id() != std::this_thread::get_id()) {
        puncher_thread_.join();
    }
}

void TestClient::ensure_io_thread() {
    if (puncher_thread_.joinable()) {
        return;
    }
    puncher_ioc_.restart();
    puncher_work_guard_ = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(
        puncher_ioc_.get_executor());
    puncher_thread_ = std::thread([this]() {
        puncher_ioc_.run();
    });
}

bool TestClient::run(const Options& opts) {
    opts_ = opts;

    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  TestClient starting");
    spdlog::info("  node_id:      {}", opts.node_id);
    spdlog::info("  network_id:   {}", opts.network_id);
    spdlog::info("  control:      {}", opts.control_url);
    spdlog::info("  stun:         {}", opts.stun_addr);
    spdlog::info("  udp_port:     {}", opts.udp_port);
    spdlog::info("  connect:      {}", opts.connect_node_id.empty() ? "(wait)" : opts.connect_node_id);
    spdlog::info("  relay:        {}", opts.relay_addr.empty() ? "(none)" : opts.relay_addr);
    spdlog::info("═══════════════════════════════════════════");

    // Step 1: Query STUN server
    {
        auto stun_host = opts.stun_addr;
        auto colon = stun_host.rfind(':');
        uint16_t stun_port = 3478;
        if (colon != std::string::npos) {
            stun_port = static_cast<uint16_t>(std::stoi(stun_host.substr(colon + 1)));
            stun_host = stun_host.substr(0, colon);
        }
        stun_result_ = queryStun(stun_host, stun_port, opts.udp_port);
        if (!stun_result_.success) {
            stun_result_.public_ip = "127.0.0.1";
            stun_result_.public_port = opts.udp_port;
            spdlog::warn("STUN failed, using local fallback: {}:{}",
                         stun_result_.public_ip, stun_result_.public_port);
        }
    }

    // Parse relay address if provided
    if (!opts.relay_addr.empty()) {
        relay_host_ = opts.relay_addr;
        uint16_t relay_default_port = 7000;
        auto colon = relay_host_.rfind(':');
        if (colon != std::string::npos) {
            relay_port_ = static_cast<uint16_t>(std::stoi(relay_host_.substr(colon + 1)));
            relay_host_ = relay_host_.substr(0, colon);
        } else {
            relay_port_ = relay_default_port;
        }
        relay_ep_ = udp::endpoint(net::ip::make_address(relay_host_), relay_port_);
        spdlog::info("Relay server: {}:{}", relay_host_, relay_port_);
    }

    // Step 2: Connect to control server via WebSocket (synchronous)
    ws_ = std::make_shared<WsClient>();

    // Parse control URL
    auto ctrl = opts.control_url;
    std::string ctrl_host = ctrl;
    std::string ctrl_port = "8080";
    auto colon = ctrl.rfind(':');
    if (colon != std::string::npos) {
        ctrl_port = ctrl.substr(colon + 1);
        ctrl_host = ctrl.substr(0, colon);
    }
    // Strip ws:// prefix
    if (ctrl_host.rfind("ws://", 0) == 0) {
        ctrl_host = ctrl_host.substr(5);
        auto slash = ctrl_host.find('/');
        if (slash != std::string::npos) {
            auto host_part = ctrl_host.substr(0, slash);
            auto port_colon = host_part.rfind(':');
            if (port_colon != std::string::npos) {
                ctrl_port = host_part.substr(port_colon + 1);
                ctrl_host = host_part.substr(0, port_colon);
            } else {
                ctrl_host = host_part;
            }
        }
    }

    if (!ws_->connect(ctrl_host, ctrl_port)) {
        spdlog::error("Failed to connect to control server");
        return false;
    }

    // Step 3: Auth (synchronous)
    {
        nlohmann::json auth_msg = {
            {"type", "auth"},
            {"node_id", opts.node_id},
            {"network_id", opts.network_id},
            {"public_key", opts.public_key}
        };
        if (!ws_->send(auth_msg)) {
            spdlog::error("Failed to send auth");
            return false;
        }
        spdlog::info("Sent auth: node_id={}, network_id={}", opts.node_id, opts.network_id);

        // Wait for auth_ok
        nlohmann::json resp;
        if (!ws_->receive(resp, 5000)) {
            spdlog::error("No auth response from server");
            return false;
        }
        if (resp["type"] != "auth_ok") {
            spdlog::error("Auth failed: {}", resp.dump());
            return false;
        }
        spdlog::info("Auth OK: node_id={}", resp.value("node_id", "?"));
    }

    // Step 4: Receive peer_list (sent by server after auth)
    {
        nlohmann::json peer_list;
        if (ws_->receive(peer_list, 3000) && peer_list["type"] == "peer_list") {
            spdlog::info("Peer list: {} peers online", peer_list["peers"].size());
            for (const auto& p : peer_list["peers"]) {
                spdlog::info("  peer: node_id={}, ip={}:{}",
                             p.value("node_id", "?"),
                             p.value("public_ip", "?"),
                             p.value("public_port", 0));
            }
        }
    }

    // Step 5: Start reader thread to receive async messages (punch_start, etc.)
    ws_->setMessageCallback([this](const std::string& type, const nlohmann::json& data) {
        if (stopping_) return;
        spdlog::info("WsClient reader: <- type={}", type);
        try {
            if (type == "punch_start") {
                on_punch_start(data);
            } else if (type == "virtual_ip_assigned") {
                on_virtual_ip_assigned(data);
            } else if (type == "tun_packet") {
                relay_to_tun("server", data.value("payload", std::string()), 0);
            } else if (type == "error") {
                spdlog::error("Server error: {}", data.value("message", ""));
            }
        } catch (const std::exception& e) {
            spdlog::error("Message handler error ({}): {}", type, e.what());
        }
    });
    ws_->startReader();

    // Step 6: Send update_addr with STUN results + local addresses
    {
        std::vector<std::string> local_addrs;
        if (!opts.local_addr.empty()) {
            std::stringstream ss(opts.local_addr);
            std::string addr;
            while (std::getline(ss, addr, ',')) {
                local_addrs.push_back(addr);
            }
        }
        std::string local_addr_str = "127.0.0.1:" + std::to_string(opts.udp_port);
        if (local_addrs.empty()) {
            local_addrs.push_back(local_addr_str);
        }

        nlohmann::json update = {
            {"type", "update_addr"},
            {"public_ip", stun_result_.public_ip},
            {"public_port", static_cast<int>(stun_result_.public_port)},
            {"local_addrs", nlohmann::json::array()}
        };
        for (const auto& addr : local_addrs) {
            update["local_addrs"].push_back(addr);
        }
        if (!ws_->send(update)) {
            spdlog::error("Failed to send update_addr");
            return false;
        }
        spdlog::info("Sent update_addr: public={}:{}",
                     update["public_ip"].get<std::string>(),
                     update["public_port"].get<int>());
    }

    // Step 7: If we have a target to connect, send connect_peer
    if (!opts.connect_node_id.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        nlohmann::json connect = {
            {"type", "connect_peer"},
            {"target_node_id", opts.connect_node_id}
        };
        if (!ws_->send(connect)) {
            spdlog::error("Failed to send connect_peer");
            return false;
        }
        spdlog::info("Sent connect_peer -> {}", opts.connect_node_id);
    }

    if (opts.enable_tun && opts.connect_node_id.empty()) {
        spdlog::info("TUN mode active — connected to server gateway, waiting for traffic...");
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    }

    // Step 8: Wait for punch_result (via on_punch_start -> on_punch_result)
    // The punch_done_ flag is set by on_punch_result callback
    // After that, relay mode may activate if configured.
    // Timeout after 30 seconds (allows relay mode to complete)
    auto wait_start = std::chrono::steady_clock::now();
    while (!punch_done_ && !stopping_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // If relay mode is active, punch_done_ stays true already
        if (mode_.load() == ClientMode::RELAY) {
            // Keep the loop alive so the puncher thread continues receiving
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - wait_start).count() > 30) {
            spdlog::warn("Timeout waiting for P2P result");
            break;
        }
    }

    // In relay mode, keep running to receive forwarded packets
    if (mode_.load() == ClientMode::RELAY && !stopping_) {
        spdlog::info("Relay mode active — waiting for relay messages (30s timeout)...");
        auto relay_start = std::chrono::steady_clock::now();
        while (!stopping_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - relay_start).count();
            if (elapsed > 30) {
                spdlog::info("Relay mode: 30s elapsed, shutting down");
                break;
            }
        }
    }

    return !stopping_ && (punch_success_ || mode_.load() == ClientMode::RELAY);
}

// ── STUN ───────────────────────────────────────────────────

StunResult TestClient::queryStun(const std::string& stun_host, uint16_t stun_port, uint16_t bind_port) {
    StunResult result;

    try {
        net::io_context tmp_ioc;
        udp::socket sock(tmp_ioc);
        udp::endpoint stun_ep(net::ip::make_address(stun_host), stun_port);

        sock.open(udp::v4());
        sock.set_option(net::socket_base::reuse_address(true));
        if (bind_port != 0) {
            sock.bind(udp::endpoint(udp::v4(), bind_port));
        }

        nlohmann::json req = {
            {"type", "stun_request"},
            {"node_id", opts_.node_id}
        };
        auto req_str = req.dump();
        sock.send_to(net::buffer(req_str), stun_ep);

        std::array<char, 4096> buf;
        udp::endpoint sender;
        sock.non_blocking(true);

        auto start = std::chrono::steady_clock::now();
        bool got_response = false;

        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start).count() < 3000) {
            boost::system::error_code ec;
            auto len = sock.receive_from(net::buffer(buf), sender, 0, ec);
            if (!ec && len > 0) {
                auto json = nlohmann::json::parse(std::string_view(buf.data(), len));
                if (json["type"] == "stun_response") {
                    result.public_ip = json["public_ip"].get<std::string>();
                    result.public_port = json["public_port"].get<uint16_t>();
                    result.success = true;
                    got_response = true;
                    spdlog::info("STUN result: public addr = {}:{}",
                                 result.public_ip, result.public_port);
                    break;
                }
            } else if (ec != net::error::would_block) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!got_response) {
            spdlog::warn("STUN: no response from {}:{} within 3s", stun_host, stun_port);
        }

        sock.close();
    } catch (const std::exception& e) {
        spdlog::error("STUN query error: {}", e.what());
    }

    return result;
}

// ── Punch handlers ─────────────────────────────────────────

void TestClient::on_punch_start(const nlohmann::json& data) {
    if (stopping_) return;

    // If we already have a successful P2P connection, ignore duplicate punch_start
    if (punch_success_.load()) {
        spdlog::info("punch_start ignored: already connected via P2P");
        return;
    }

    auto peer_node_id = data["peer_node_id"].get<std::string>();
    auto peer_public_ip = data["peer_public_ip"].get<std::string>();
    auto peer_public_port = static_cast<uint16_t>(data["peer_public_port"].get<int>());
    auto direction = data.value("direction", std::string());

    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  punch_start ({})", direction);
    spdlog::info("  peer_node_id:  {}", peer_node_id);
    spdlog::info("  peer_public:   {}:{}", peer_public_ip, peer_public_port);
    spdlog::info("═══════════════════════════════════════════");

    // Collect punch targets first
    std::vector<PunchTarget> targets;
    targets.push_back({peer_public_ip, peer_public_port});
    // Store peer node id for relay fallback
    peer_node_id_ = peer_node_id;

    spdlog::info("Punch target (public): {}:{}", peer_public_ip, peer_public_port);

    if (data.contains("peer_local_addrs")) {
        for (const auto& addr : data["peer_local_addrs"]) {
            auto addr_str = addr.get<std::string>();
            auto colon = addr_str.rfind(':');
            if (colon != std::string::npos) {
                auto ip = addr_str.substr(0, colon);
                auto port = static_cast<uint16_t>(std::stoi(addr_str.substr(colon + 1)));
                targets.push_back({ip, port});
                spdlog::info("Punch target (local): {}:{}", ip, port);
            }
        }
    }

    // Stop old puncher before creating a new one to avoid port conflicts.
    // UdpPuncher::stop() closes the socket, freeing the bound port.
    if (puncher_) {
        puncher_->setPunchCallback(nullptr);
        puncher_->stop();
        puncher_.reset();
    }

    // Create UdpPuncher — constructor opens the UDP socket on bind_port
    auto bind_port = opts_.udp_port;

    try {
        puncher_ = std::make_shared<UdpPuncher>(puncher_ioc_, bind_port);
    } catch (const std::exception& e) {
        spdlog::error("UdpPuncher bind failed (port {}): {}", bind_port, e.what());
        return;
    }

    puncher_->setPunchCallback([this](const PunchResult& r) {
        if (stopping_) return;
        on_punch_result(r);
    });

    // Post ALL puncher operations first, then start the thread.
    // This ensures work is already queued when puncher_ioc_.run() starts.
    auto my_node_id = opts_.node_id;
    net::post(puncher_ioc_, [this, targets, my_node_id, peer_node_id]() {
        if (stopping_) return;
        puncher_->start();
        for (const auto& t : targets) {
            puncher_->addTarget(t);
        }
        puncher_->startPunching(my_node_id, peer_node_id, 100, 5000);
    });

    // Start puncher io_context thread (will pick up queued work)
    ensure_io_thread();
}

void TestClient::on_punch_result(const PunchResult& result) {
    if (stopping_) return;

    if (result.success) {
        mode_ = ClientMode::P2P;
        punch_success_ = true;
        spdlog::info("✓ P2P Hole Punch SUCCESS with {} at {}:{} (RTT={}ms)",
                     result.remote_node_id, result.remote_ip,
                     result.remote_port, result.rtt_ms);
        punch_done_ = true;
    } else {
        mode_ = ClientMode::FAILED;
        spdlog::warn("✗ P2P Hole Punch FAILED for {}", result.remote_node_id);

        if (!opts_.relay_addr.empty() && puncher_) {
            spdlog::info("→ Falling back to relay mode: {}", opts_.relay_addr);
            enter_relay_mode();
        } else {
            punch_done_ = true;
        }
    }
}

// ── Relay Mode ─────────────────────────────────────────────

void TestClient::enter_relay_mode() {
    if (stopping_) return;

    mode_ = ClientMode::RELAY;

    // Set up relay callbacks on the puncher (posted to puncher_ioc_)
    net::post(puncher_ioc_, [this]() {
        if (stopping_) return;
        if (opts_.enable_tun && tun_ready_) {
            // TUN mode: relay packets go to TUN interface
            puncher_->setRelayPacketCallback([this](const std::string& from,
                                                      const std::string& payload,
                                                      int64_t seq) {
                relay_to_tun(from, payload, seq);
            });
        } else {
            // Test message mode
            puncher_->setRelayPacketCallback([this](const std::string& from,
                                                      const std::string& payload,
                                                      int64_t seq) {
                on_relay_packet(from, payload, seq);
            });
        }
        puncher_->setRelayEventCallback([this](const std::string& event,
                                                  const nlohmann::json& data) {
            on_relay_event(event, data);
        });
    });

    // Send relay register (posted to puncher_ioc_ since sendRelayRegister calls socket_)
    net::post(puncher_ioc_, [this]() {
        if (stopping_) return;
        puncher_->sendRelayRegister(opts_.node_id, opts_.network_id, "test-token", relay_ep_);
    });

    // Start heartbeat
    net::post(puncher_ioc_, [this]() {
        if (stopping_) return;
        puncher_->startRelayHeartbeat(opts_.node_id, relay_ep_, 10);
    });

    // Schedule first test message after a short delay (for registration to complete)
    net::post(puncher_ioc_, [this]() {
        if (stopping_) return;
        relay_test_timer_ = std::make_unique<net::steady_timer>(puncher_ioc_);
        relay_test_timer_->expires_after(std::chrono::seconds(2));
        relay_test_timer_->async_wait([this](boost::system::error_code ec) {
            if (ec || stopping_) return;
            if (relay_registered_ && !peer_node_id_.empty()) {
                send_relay_test_message();
                schedule_relay_test_message();
            } else {
                // Wait for registration to complete, retry in 1s
                relay_test_timer_->expires_after(std::chrono::seconds(1));
                relay_test_timer_->async_wait([this](boost::system::error_code ec2) {
                    if (ec2 || stopping_) return;
                    if (relay_registered_ && !peer_node_id_.empty()) {
                        send_relay_test_message();
                        schedule_relay_test_message();
                    }
                });
            }
        });
    });

    // Mark punch_done_ so the wait loop in run() transitions to relay loop
    punch_done_ = true;
}

void TestClient::on_relay_packet(const std::string& from_node_id,
                                 const std::string& payload,
                                 int64_t seq) {
    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  RELAY_MESSAGE_RECEIVED");
    spdlog::info("  from_node_id: {}", from_node_id);
    spdlog::info("  seq:          {}", seq);
    spdlog::info("  payload:      {}", payload);
    spdlog::info("═══════════════════════════════════════════");
}

void TestClient::on_relay_event(const std::string& event,
                                const nlohmann::json& data) {
    if (event == "relay_register_ok") {
        relay_registered_ = true;
        spdlog::info("✓ Relay registration OK: node_id={}", data.value("node_id", "?"));
    } else if (event == "relay_error") {
        spdlog::warn("✗ Relay error: {}", data.value("message", "?"));
    }
}

void TestClient::send_relay_test_message() {
    if (stopping_) return;
    if (!relay_registered_ || peer_node_id_.empty()) return;

    ++relay_seq_;
    std::string test_payload = "hello-from-" + opts_.node_id + "-seq=" + std::to_string(relay_seq_);
    auto from_id = opts_.node_id;
    auto to_id = peer_node_id_;
    auto seq = relay_seq_;
    auto relay_ep = relay_ep_;

    spdlog::info("Relay test: sending seq={} to {} via relay", seq, to_id);

    net::post(puncher_ioc_, [this, from_id, to_id, seq, test_payload, relay_ep]() {
        if (stopping_) return;
        puncher_->sendRelayPacket(from_id, to_id, seq, test_payload, relay_ep);
    });
}

void TestClient::schedule_relay_test_message() {
    if (stopping_) return;
    if (!relay_test_timer_) return;
    relay_test_timer_->expires_after(std::chrono::seconds(5));
    relay_test_timer_->async_wait([this](boost::system::error_code ec) {
        if (ec || stopping_) return;
        send_relay_test_message();
        schedule_relay_test_message();
    });
}

// ── TUN Bridge ─────────────────────────────────────────────

void TestClient::on_virtual_ip_assigned(const nlohmann::json& data) {
    if (stopping_) return;

    virtual_ip_ = data.value("virtual_ip", std::string());
    gateway_ip_ = data.value("gateway_ip", std::string());
    subnet_ = data.value("subnet", "10.0.0.0/16");

    if (virtual_ip_.empty()) {
        spdlog::warn("Received empty virtual_ip");
        return;
    }

    if ((tun_ready_ && tun_) || tun_starting_) {
        spdlog::debug("Virtual IP assignment already applied: {}", virtual_ip_);
        return;
    }

    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  Virtual IP assigned: {} / {}", virtual_ip_, subnet_);
    if (!gateway_ip_.empty()) {
        spdlog::info("  Server gateway: {}", gateway_ip_);
    }
    spdlog::info("═══════════════════════════════════════════");

    if (opts_.enable_tun) {
        start_tun_bridge();
    }
}

void TestClient::start_tun_bridge() {
    if (virtual_ip_.empty()) {
        spdlog::warn("TUN bridge: no virtual IP, cannot start");
        return;
    }
    if (tun_starting_.exchange(true) || tun_ready_) {
        return;
    }

    ensure_io_thread();

    // Parse subnet prefix
    int prefix = 16;
    auto slash = subnet_.find('/');
    if (slash != std::string::npos) {
        prefix = std::stoi(subnet_.substr(slash + 1));
    }

    // Create TUN interface (runs on puncher_ioc_)
    net::post(puncher_ioc_, [this, prefix]() {
        if (stopping_) return;
        tun_ = TunInterface::create(puncher_ioc_);
        if (!tun_->open(opts_.tun_name, virtual_ip_, prefix, opts_.tun_mtu)) {
            spdlog::error("TUN bridge: failed to create TUN interface");
            tun_.reset();
            tun_starting_ = false;
            return;
        }

        tun_ready_ = true;
        tun_starting_ = false;
        spdlog::info("TUN bridge: {} configured with {}/{}",
                     tun_->interfaceName(), virtual_ip_, prefix);

        // Add route for the virtual subnet
        tun_->addRoute(subnet_);

        // Start reading from TUN (async loop on puncher_ioc_)
        do_tun_read();
    });
}

void TestClient::do_tun_read() {
    if (stopping_) return;
    if (!tun_ready_ || !tun_) return;

    tun_->asyncRead(net::buffer(tun_read_buf_),
        [this](boost::system::error_code ec, std::size_t len) {
            if (stopping_) return;
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    spdlog::error("TUN bridge: read error: {}", ec.message());
                }
                return;
            }

            // Got an IP packet from TUN — forward via relay
            std::vector<uint8_t> packet(
                tun_read_buf_.data(),
                tun_read_buf_.data() + len);

            if (mode_.load() == ClientMode::RELAY) {
                tun_to_relay(packet);
            } else {
                send_tun_packet_to_server(packet);
            }

            // Continue reading
            do_tun_read();
        });
}

void TestClient::send_tun_packet_to_server(const std::vector<uint8_t>& packet) {
    if (stopping_ || !ws_ || packet.empty()) return;

    nlohmann::json msg = {
        {"type", "tun_packet"},
        {"payload", hex_encode(packet)}
    };
    if (!ws_->send(msg)) {
        spdlog::warn("TUN bridge: failed to send packet to server");
    }
}

void TestClient::tun_to_relay(const std::vector<uint8_t>& packet) {
    if (stopping_) return;
    if (!relay_registered_ || peer_node_id_.empty()) {
        // No peer to send to yet
        return;
    }

    // Encode the raw IP packet as base64 and send via relay
    // For MVP: hex-encode the packet as payload
    std::string payload_hex;
    static const char hex_chars[] = "0123456789abcdef";
    for (uint8_t byte : packet) {
        payload_hex += hex_chars[byte >> 4];
        payload_hex += hex_chars[byte & 0x0F];
    }

    ++relay_seq_;
    auto from_id = opts_.node_id;
    auto to_id = peer_node_id_;
    auto seq = relay_seq_;
    auto relay_ep = relay_ep_;

    net::post(puncher_ioc_, [this, from_id, to_id, seq, payload_hex, relay_ep]() {
        if (stopping_) return;
        puncher_->sendRelayPacket(from_id, to_id, seq, payload_hex, relay_ep);
        spdlog::debug("TUN bridge: relayed IP packet seq={} ({} bytes)", seq, payload_hex.size() / 2);
    });
}

void TestClient::relay_to_tun(const std::string& from_node_id,
                              const std::string& payload,
                              int64_t seq) {
    if (stopping_) return;
    if (!tun_ready_ || !tun_) return;

    // Decode hex payload back to raw IP packet
    std::vector<uint8_t> packet;
    packet.reserve(payload.size() / 2);
    for (size_t i = 0; i < payload.size(); i += 2) {
        auto h = payload[i];
        auto l = payload[i + 1];
        uint8_t byte = 0;
        if (h >= '0' && h <= '9') byte |= (h - '0') << 4;
        else if (h >= 'a' && h <= 'f') byte |= (h - 'a' + 10) << 4;
        else if (h >= 'A' && h <= 'F') byte |= (h - 'A' + 10) << 4;
        if (l >= '0' && l <= '9') byte |= (l - '0');
        else if (l >= 'a' && l <= 'f') byte |= (l - 'a' + 10);
        else if (l >= 'A' && l <= 'F') byte |= (l - 'A' + 10);
        packet.push_back(byte);
    }

    // Write to TUN (async on puncher_ioc_)
    net::post(puncher_ioc_, [this, packet = std::move(packet), seq]() {
        if (stopping_) return;
        if (!tun_ready_ || !tun_) return;
        tun_->write(packet);
        spdlog::debug("TUN bridge: wrote {} bytes from relay seq={}", packet.size(), seq);
    });
}
