#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include "client/WsClient.h"
#include "client/UdpPuncher.h"
#include "tun/TunInterface.h"

namespace net = boost::asio;

struct StunResult {
    std::string public_ip;
    uint16_t public_port = 0;
    bool success = false;
};

enum class ClientMode {
    PUNCHING,
    P2P,
    RELAY,
    FAILED
};

class TestClient : public std::enable_shared_from_this<TestClient> {
public:
    struct Options {
        std::string node_id;
        std::string network_id;
        std::string public_key = "test-key";
        std::string control_url;      // e.g. "127.0.0.1:8080"
        std::string stun_addr;        // e.g. "127.0.0.1:3478"
        uint16_t udp_port = 0;        // 0 = auto bind
        std::string connect_node_id;  // empty = wait for incoming
        std::string local_addr;       // for local_addrs, separate by comma
        std::string relay_addr;       // e.g. "127.0.0.1:7000", empty = no relay fallback
        bool enable_tun = false;      // enable TUN virtual interface
        std::string tun_name = "nat%d";
        int tun_mtu = 1300;
    };

    explicit TestClient();
    ~TestClient();

    // Initialize and run the full flow (synchronous)
    bool run(const Options& opts);

    // Request shutdown from another thread (e.g. signal monitor)
    void stop();

    // Check result
    bool punchSuccess() const { return punch_success_; }

private:
    // STUN query (synchronous UDP)
    StunResult queryStun(const std::string& stun_host, uint16_t stun_port, uint16_t bind_port);

    // Handle punch_start from WS
    void on_punch_start(const nlohmann::json& data);

    // Handle punch result from UdpPuncher
    void on_punch_result(const PunchResult& result);

    // ── Relay mode ──
    void enter_relay_mode();
    void on_relay_packet(const std::string& from_node_id,
                         const std::string& payload,
                         int64_t seq);
    void on_relay_event(const std::string& event,
                        const nlohmann::json& data);
    void send_relay_test_message();
    void schedule_relay_test_message();

    // Heartbeat
    void start_heartbeat();
    void schedule_heartbeat();
    void maintain_control_connection();
    void report_transport_state(const std::string& mode, int64_t rtt_ms = 0);

    // Control channel
    bool connect_control_channel(bool reconnecting);
    bool send_update_addr();
    bool send_connect_peer_request();
    void install_ws_callbacks(const std::shared_ptr<WsClient>& ws);
    void log_peer_list(const nlohmann::json& peer_list) const;

    // ── TUN mode ──
    void on_virtual_ip_assigned(const nlohmann::json& data);
    void start_tun_bridge();
    void do_tun_read();
    void ensure_io_thread();
    void send_tun_packet_to_server(const std::vector<uint8_t>& packet);
    void tun_to_relay(const std::vector<uint8_t>& packet);
    void relay_to_tun(const std::string& from_node_id,
                      const std::string& payload,
                      int64_t seq);

    std::shared_ptr<WsClient> ws_;
    std::shared_ptr<UdpPuncher> puncher_;

    Options opts_;
    StunResult stun_result_;
    std::string ctrl_host_;
    std::string ctrl_port_ = "8080";
    std::string ctrl_path_ = "/api/v1/ws/nat";
    std::chrono::steady_clock::time_point next_reconnect_attempt_{};
    std::atomic<int> reported_transport_mode_{-1};

    std::atomic<bool> punch_success_{false};
    std::atomic<bool> punch_done_{false};
    std::atomic<bool> stopping_{false};

    // IoContext for UdpPuncher
    net::io_context puncher_ioc_;
    std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> puncher_work_guard_;
    std::thread puncher_thread_;

    // Relay mode state
    std::atomic<ClientMode> mode_{ClientMode::PUNCHING};
    std::string peer_node_id_;
    std::string relay_host_;
    uint16_t relay_port_ = 7000;
    udp::endpoint relay_ep_;
    int64_t relay_seq_ = 0;
    bool relay_registered_{false};
    std::unique_ptr<net::steady_timer> relay_test_timer_;

    // Heartbeat timer
    std::unique_ptr<net::steady_timer> heartbeat_timer_;

    // TUN bridge state
    std::shared_ptr<TunInterface> tun_;
    std::string virtual_ip_;
    std::string gateway_ip_;
    std::string subnet_;
    std::array<char, 1500> tun_read_buf_;
    std::atomic<bool> tun_ready_{false};
    std::atomic<bool> tun_starting_{false};
};
