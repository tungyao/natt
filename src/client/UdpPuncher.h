#pragma once

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>

namespace net = boost::asio;
using udp = net::ip::udp;

struct PunchTarget {
    std::string ip;
    uint16_t port;
};

struct PunchResult {
    bool success = false;
    std::string remote_node_id;
    std::string remote_ip;
    uint16_t remote_port = 0;
    int64_t rtt_ms = 0;
};

class UdpPuncher : public std::enable_shared_from_this<UdpPuncher> {
public:
    using PunchCallback = std::function<void(const PunchResult&)>;
    using PacketCallback = std::function<void(const std::string& from_node_id,
                                               const std::string& type,
                                               int64_t timestamp,
                                               const udp::endpoint& sender)>;

    UdpPuncher(net::io_context& ioc, uint16_t bind_port);
    ~UdpPuncher();

    // Start receiving
    void start();

    // Stop and close socket
    void stop();

    // Get the bound port
    uint16_t localPort() const { return local_port_; }

    // Add a target to punch at (public or local addr)
    void addTarget(const PunchTarget& target);

    // Start punching: send punch packets every interval_ms to all targets
    // for duration_ms. Calls on_punch_result when P2P succeeds or times out.
    void startPunching(const std::string& my_node_id,
                       const std::string& target_node_id,
                       int interval_ms = 100,
                       int duration_ms = 5000);

    // Send a single punch packet to a specific target
    void sendPunch(const std::string& my_node_id,
                   const std::string& target_node_id,
                   const PunchTarget& target);

    // Send punch_ack to a specific address
    void sendAck(const std::string& my_node_id,
                 const std::string& target_node_id,
                 const udp::endpoint& target);

    // Register callback for P2P result
    void setPunchCallback(PunchCallback cb) { punch_cb_ = std::move(cb); }

    // Register callback for received packets (for logging/response)
    void setPacketCallback(PacketCallback cb) { packet_cb_ = std::move(cb); }

    // Check if P2P was successful
    bool isSuccess() const { return success_; }

    // ── Relay mode ───────────────────────────────────────────
    using RelayPacketCallback = std::function<void(const std::string& from_node_id,
                                                     const std::string& payload,
                                                     int64_t seq)>;
    using RelayEventCallback = std::function<void(const std::string& event,
                                                    const nlohmann::json& data)>;

    // Send relay_register to the relay server
    void sendRelayRegister(const std::string& node_id,
                           const std::string& network_id,
                           const std::string& token,
                           const udp::endpoint& relay_ep);

    // Send relay_heartbeat to the relay server
    void sendRelayHeartbeat(const std::string& node_id,
                            const udp::endpoint& relay_ep);

    // Send a relay_packet (A -> B) through the relay server
    void sendRelayPacket(const std::string& from_node_id,
                         const std::string& to_node_id,
                         int64_t seq,
                         const std::string& payload,
                         const udp::endpoint& relay_ep);

    // Start periodic heartbeat timer for relay mode
    void startRelayHeartbeat(const std::string& node_id,
                             const udp::endpoint& relay_ep,
                             int interval_sec = 10);

    // Stop relay heartbeat timer
    void stopRelayHeartbeat();

    // Register callbacks for relay events
    void setRelayPacketCallback(RelayPacketCallback cb) { relay_packet_cb_ = std::move(cb); }
    void setRelayEventCallback(RelayEventCallback cb) { relay_event_cb_ = std::move(cb); }

private:
    void do_receive();
    void handle_receive(boost::system::error_code ec, std::size_t len);
    void do_send_punch();
    void do_send_to(const std::string& payload, const udp::endpoint& target);

    net::io_context& ioc_;
    udp::socket socket_;
    uint16_t local_port_;
    udp::endpoint remote_;

    std::string my_node_id_;
    std::string target_node_id_;
    std::vector<PunchTarget> targets_;
    size_t target_index_ = 0;

    std::atomic<bool> punching_{false};
    std::atomic<bool> success_{false};
    int interval_ms_ = 100;
    int64_t start_time_ms_ = 0;
    int64_t ack_time_ms_ = 0;

    net::steady_timer punch_timer_;    // per-burst interval timer
    net::steady_timer timeout_timer_;  // overall timeout

    PunchCallback punch_cb_;
    PacketCallback packet_cb_;

    // Relay mode members
    RelayPacketCallback relay_packet_cb_;
    RelayEventCallback relay_event_cb_;
    net::steady_timer relay_hb_timer_;

    static constexpr std::size_t MAX_BUF = 4096;
    std::array<char, MAX_BUF> recv_buffer_;
};
