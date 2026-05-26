#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include "client/WsClient.h"
#include "client/UdpPuncher.h"

namespace net = boost::asio;

struct StunResult {
    std::string public_ip;
    uint16_t public_port = 0;
    bool success = false;
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
    };

    explicit TestClient();
    ~TestClient();

    // Initialize and run the full flow (synchronous)
    bool run(const Options& opts);

    // Check result
    bool punchSuccess() const { return punch_success_; }

private:
    // STUN query (synchronous UDP)
    StunResult queryStun(const std::string& stun_host, uint16_t stun_port);

    // Handle punch_start from WS
    void on_punch_start(const nlohmann::json& data);

    // Handle punch result from UdpPuncher
    void on_punch_result(const PunchResult& result);

    std::shared_ptr<WsClient> ws_;
    std::shared_ptr<UdpPuncher> puncher_;

    Options opts_;
    StunResult stun_result_;

    std::atomic<bool> punch_success_{false};
    std::atomic<bool> punch_done_{false};

    // IoContext for UdpPuncher
    net::io_context puncher_ioc_;
    std::thread puncher_thread_;
};
