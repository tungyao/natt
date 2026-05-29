#include "client/UdpPuncher.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

UdpPuncher::UdpPuncher(net::io_context& ioc, uint16_t bind_port)
    : ioc_(ioc)
    , socket_(ioc, udp::endpoint(udp::v4(), bind_port))
    , local_port_(socket_.local_endpoint().port())
    , punch_timer_(ioc)
    , timeout_timer_(ioc)
    , relay_hb_timer_(ioc)
{
    spdlog::info("UdpPuncher: bound to port {}", local_port_);
}

UdpPuncher::~UdpPuncher() {
    stop();
}

void UdpPuncher::start() {
    do_receive();
}

void UdpPuncher::stop() {
    punching_ = false;
    boost::system::error_code ec;
    punch_timer_.cancel();
    timeout_timer_.cancel();
    relay_hb_timer_.cancel();
    peer_endpoint_known_ = false;
    socket_.close(ec);
}

void UdpPuncher::addTarget(const PunchTarget& target) {
    targets_.push_back(target);
    spdlog::debug("UdpPuncher: added target {}:{}", target.ip, target.port);
}

void UdpPuncher::startPunching(const std::string& my_node_id,
                                const std::string& target_node_id,
                                int interval_ms,
                                int duration_ms) {
    my_node_id_ = my_node_id;
    target_node_id_ = target_node_id;
    interval_ms_ = interval_ms;
    punching_ = true;
    success_ = false;
    peer_endpoint_known_ = false;
    start_time_ms_ = now_ms();

    spdlog::info("UdpPuncher: start punching -> node_id={}, {} targets, {}ms timeout",
                 target_node_id, targets_.size(), duration_ms);

    // Schedule punch stop after duration (separate timer from burst interval)
    timeout_timer_.expires_after(std::chrono::milliseconds(duration_ms));
    timeout_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return;
        self->punching_ = false;
        if (!self->success_) {
            spdlog::warn("UdpPuncher: timeout, P2P failed for node_id={}",
                         self->target_node_id_);
            if (self->punch_cb_) {
                PunchResult r;
                r.success = false;
                r.remote_node_id = self->target_node_id_;
                self->punch_cb_(r);
            }
        }
    });

    // Start the first punch burst
    do_send_punch();
}

void UdpPuncher::sendPunch(const std::string& my_node_id,
                            const std::string& target_node_id,
                            const PunchTarget& target) {
    nlohmann::json msg = {
        {"type", "punch"},
        {"from_node_id", my_node_id},
        {"to_node_id", target_node_id},
        {"timestamp", now_ms()}
    };
    auto payload = msg.dump();

    spdlog::debug("UdpPuncher: sending punch to {}:{}", target.ip, target.port);
    boost::system::error_code ec;
    udp::endpoint ep(net::ip::make_address(target.ip), target.port);
    socket_.send_to(net::buffer(payload), ep, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send punch error to {}:{}: {}",
                      target.ip, target.port, ec.message());
    } else {
        spdlog::debug("UdpPuncher: punch sent to {}:{}", target.ip, target.port);
    }
}

void UdpPuncher::sendAck(const std::string& my_node_id,
                          const std::string& target_node_id,
                          const udp::endpoint& target) {
    nlohmann::json msg = {
        {"type", "punch_ack"},
        {"from_node_id", my_node_id},
        {"to_node_id", target_node_id},
        {"timestamp", now_ms()}
    };
    auto payload = msg.dump();

    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), target, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send ack error to {}:{}: {}",
                      target.address().to_string(), target.port(), ec.message());
    }
}

void UdpPuncher::do_receive() {
    socket_.async_receive_from(
        net::buffer(recv_buffer_), remote_,
        [self = shared_from_this()](boost::system::error_code ec, std::size_t len) {
            self->handle_receive(ec, len);
        });
}

void UdpPuncher::handle_receive(boost::system::error_code ec, std::size_t bytes_recvd) {
    if (ec) {
        if (ec != net::error::operation_aborted) {
            spdlog::error("UdpPuncher recv error: {}", ec.message());
            do_receive();
        }
        return;
    }

    try {
        std::string_view data(recv_buffer_.data(), bytes_recvd);
        auto json = nlohmann::json::parse(data);
        auto type = json["type"].get<std::string>();
        auto from_node_id = json.value("from_node_id", std::string());
        auto to_node_id = json.value("to_node_id", std::string());
        auto timestamp = json.value("timestamp", int64_t(0));

        // Notify packet callback
        if (packet_cb_) {
            packet_cb_(from_node_id, type, timestamp, remote_);
        }

        if (type == "punch") {
            // Received a punch — reply with ack
            spdlog::info("UdpPuncher: <- punch from {} at {}:{}",
                         from_node_id,
                         remote_.address().to_string(),
                         remote_.port());

            if (from_node_id == target_node_id_) {
                peer_endpoint_ = remote_;
                peer_endpoint_known_ = true;
            }

            sendAck(my_node_id_, from_node_id, remote_);
            spdlog::info("UdpPuncher: -> ack to {} at {}:{}",
                         from_node_id,
                         remote_.address().to_string(),
                         remote_.port());

        } else if (type == "punch_ack") {
            // P2P success!
            if (!success_.exchange(true)) {
                ack_time_ms_ = now_ms();
                auto rtt = ack_time_ms_ - start_time_ms_;
                peer_endpoint_ = remote_;
                peer_endpoint_known_ = true;
                spdlog::info("═══════════════════════════════════════════");
                spdlog::info("  P2P SUCCESS with node_id={}", from_node_id);
                spdlog::info("  Remote addr: {}:{}",
                             remote_.address().to_string(), remote_.port());
                spdlog::info("  RTT: {}ms", rtt);
                spdlog::info("═══════════════════════════════════════════");

                punching_ = false;
                timeout_timer_.cancel();
                punch_timer_.cancel();

                if (punch_cb_) {
                    PunchResult r;
                    r.success = true;
                    r.remote_node_id = from_node_id;
                    r.remote_ip = remote_.address().to_string();
                    r.remote_port = remote_.port();
                    r.rtt_ms = rtt;
                    punch_cb_(r);
                }
            }

        } else if (type == "p2p_packet") {
            auto from_id = json.value("from_node_id", std::string());
            auto payload = json.value("payload", std::string());
            auto seq = json.value("seq", int64_t(0));

            if (from_id == target_node_id_) {
                peer_endpoint_ = remote_;
                peer_endpoint_known_ = true;
            }

            if (peer_packet_cb_) {
                peer_packet_cb_(from_id, payload, seq);
            }

        } else if (type == "relay_packet") {
            // Relay packet forwarded by the relay server
            auto from_id = json.value("from_node_id", std::string());
            auto payload = json.value("payload", std::string());
            auto seq = json.value("seq", int64_t(0));

            if (relay_packet_cb_) {
                relay_packet_cb_(from_id, payload, seq);
            }

        } else if (type == "relay_register_ok") {
            // Registration acknowledged by relay server
            if (relay_event_cb_) {
                nlohmann::json d;
                d["node_id"] = json.value("node_id", std::string());
                relay_event_cb_("relay_register_ok", d);
            }

        } else if (type == "error") {
            auto message = json.value("message", std::string());
            spdlog::warn("UdpPuncher: error from relay: {}", message);
            if (relay_event_cb_) {
                nlohmann::json d;
                d["message"] = message;
                relay_event_cb_("relay_error", d);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("UdpPuncher: invalid packet: {}", e.what());
    }

    do_receive();
}

bool UdpPuncher::sendPeerPacket(const std::string& from_node_id,
                                const std::string& to_node_id,
                                int64_t seq,
                                const std::string& payload_data) {
    if (!success_ || !peer_endpoint_known_) {
        return false;
    }

    nlohmann::json msg = {
        {"type", "p2p_packet"},
        {"from_node_id", from_node_id},
        {"to_node_id", to_node_id},
        {"seq", seq},
        {"payload", payload_data}
    };
    auto payload = msg.dump();

    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), peer_endpoint_, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send p2p_packet error to {}:{}: {}",
                      peer_endpoint_.address().to_string(),
                      peer_endpoint_.port(),
                      ec.message());
        return false;
    }

    spdlog::debug("UdpPuncher: -> p2p_packet seq={} to {}:{}",
                  seq,
                  peer_endpoint_.address().to_string(),
                  peer_endpoint_.port());
    return true;
}

void UdpPuncher::do_send_punch() {
    if (!punching_ || success_ || targets_.empty()) return;

    spdlog::debug("UdpPuncher: do_send_punch to {} targets", targets_.size());

    // Send punch to all targets
    for (const auto& target : targets_) {
        sendPunch(my_node_id_, target_node_id_, target);
    }

    // Schedule next burst
    punch_timer_.expires_after(std::chrono::milliseconds(interval_ms_));
    punch_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec) {
            spdlog::debug("UdpPuncher: punch timer cancelled: {}", ec.message());
            return;
        }
        self->do_send_punch();
    });
}

void UdpPuncher::do_send_to(const std::string& payload, const udp::endpoint& target) {
    socket_.async_send_to(
        net::buffer(payload), target,
        [](boost::system::error_code ec, std::size_t) {
            if (ec) {
                spdlog::error("UdpPuncher: async send error: {}", ec.message());
            }
        });
}

// ── Relay Mode ──────────────────────────────────────────────

void UdpPuncher::sendRelayRegister(const std::string& node_id,
                                   const std::string& network_id,
                                   const std::string& token,
                                   const udp::endpoint& relay_ep) {
    nlohmann::json msg = {
        {"type", "relay_register"},
        {"node_id", node_id},
        {"network_id", network_id},
        {"token", token}
    };
    auto payload = msg.dump();

    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), relay_ep, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send relay_register error: {}", ec.message());
    } else {
        spdlog::info("UdpPuncher: -> relay_register node_id={} network_id={}",
                     node_id, network_id);
    }
}

void UdpPuncher::sendRelayHeartbeat(const std::string& node_id,
                                    const udp::endpoint& relay_ep) {
    nlohmann::json msg = {
        {"type", "relay_heartbeat"},
        {"node_id", node_id}
    };
    auto payload = msg.dump();

    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), relay_ep, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send relay_heartbeat error: {}", ec.message());
    } else {
        spdlog::debug("UdpPuncher: -> relay_heartbeat node_id={}", node_id);
    }
}

void UdpPuncher::sendRelayPacket(const std::string& from_node_id,
                                 const std::string& to_node_id,
                                 int64_t seq,
                                 const std::string& payload_data,
                                 const udp::endpoint& relay_ep) {
    nlohmann::json msg = {
        {"type", "relay_packet"},
        {"from_node_id", from_node_id},
        {"to_node_id", to_node_id},
        {"seq", seq},
        {"payload", payload_data}
    };
    auto payload = msg.dump();

    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), relay_ep, 0, ec);
    if (ec) {
        spdlog::error("UdpPuncher: send relay_packet error: {}", ec.message());
    } else {
        spdlog::info("UdpPuncher: -> relay_packet seq={} from={} to={}",
                     seq, from_node_id, to_node_id);
    }
}

void UdpPuncher::startRelayHeartbeat(const std::string& node_id,
                                     const udp::endpoint& relay_ep,
                                     int interval_sec) {
    // Cancel any existing heartbeat timer
    stopRelayHeartbeat();

    auto self = shared_from_this();
    relay_hb_timer_.expires_after(std::chrono::seconds(interval_sec));
    relay_hb_timer_.async_wait([self, node_id, relay_ep, interval_sec](boost::system::error_code ec) {
        if (ec) {
            spdlog::debug("UdpPuncher: relay heartbeat timer cancelled: {}", ec.message());
            return;
        }
        self->sendRelayHeartbeat(node_id, relay_ep);
        self->startRelayHeartbeat(node_id, relay_ep, interval_sec);
    });

    spdlog::info("UdpPuncher: relay heartbeat started (interval={}s)", interval_sec);
}

void UdpPuncher::stopRelayHeartbeat() {
    relay_hb_timer_.cancel();
}
