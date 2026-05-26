#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <array>
#include "relay/RelayRegistry.h"
#include <nlohmann/json.hpp>

namespace net = boost::asio;
using udp = net::ip::udp;

class RelayServer : public std::enable_shared_from_this<RelayServer> {
public:
    RelayServer(net::io_context& ioc, uint16_t port = 7000);
    ~RelayServer();

    // Start the receive loop
    void start();

    // Stop the server
    void stop();

    // Get reference to registry (for monitoring / stats)
    RelayRegistry& registry() { return registry_; }
    const RelayRegistry& registry() const { return registry_; }

private:
    // Start an async receive
    void do_receive();

    // Handle a received UDP datagram
    void handle_receive(boost::system::error_code ec, std::size_t bytes_recvd);

    // Message handlers
    void handle_register(const nlohmann::json& msg, const udp::endpoint& sender);
    void handle_relay_packet(const nlohmann::json& msg);
    void handle_heartbeat(const nlohmann::json& msg, const udp::endpoint& sender);

    // Send helpers
    void send_to(const std::string& payload, const udp::endpoint& dest);
    void send_error(const udp::endpoint& dest, const std::string& message);

    // Periodic cleanup
    void schedule_cleanup();
    void do_cleanup();

    net::io_context& ioc_;
    udp::socket socket_;
    uint16_t port_;

    // Receive buffer and sender endpoint
    std::array<char, 65536> recv_buffer_;
    udp::endpoint sender_;

    // Node registry
    RelayRegistry registry_;

    // Periodic cleanup timer
    net::steady_timer cleanup_timer_;

    // Constants
    static constexpr int CLEANUP_INTERVAL_SEC = 10;
    static constexpr int NODE_TIMEOUT_SEC = 30;
};
