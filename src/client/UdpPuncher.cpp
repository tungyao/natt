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
    , noise_retry_timer_(ioc)
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
    noise_retry_timer_.cancel();
    relay_hb_timer_.cancel();
    peer_endpoint_known_ = false;
    punch_path_ready_ = false;
    noise_ready_ = false;
    noise_enabled_ = false;
    noise_initiator_ = false;
    last_noise_stage_ = 0;
    handshake_retry_count_ = 0;
    last_noise_message_.clear();
    noise_.reset();
    socket_.close(ec);
}

void UdpPuncher::addTarget(const PunchTarget& target) {
    targets_.push_back(target);
    spdlog::debug("UdpPuncher: added target {}:{}", target.ip, target.port);
}

void UdpPuncher::configureNoise(NoiseProtocol::Role role,
                                const NoiseProtocol::StaticKeypair& local_static,
                                const std::array<std::uint8_t, NoiseProtocol::KEY_SIZE>& peer_static_public_key) {
    noise_.emplace(role, local_static, peer_static_public_key);
    noise_enabled_ = true;
    noise_initiator_ = role == NoiseProtocol::Role::Initiator;
    noise_ready_ = false;
    last_noise_stage_ = 0;
    handshake_retry_count_ = 0;
    last_noise_message_.clear();
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
    punch_path_ready_ = false;
    noise_ready_ = false;
    last_noise_stage_ = 0;
    handshake_retry_count_ = 0;
    last_noise_message_.clear();
    start_time_ms_ = now_ms();

    spdlog::info("UdpPuncher: start punching -> node_id={}, {} targets, {}ms timeout",
                 target_node_id, targets_.size(), duration_ms);

    armTimeout(std::chrono::milliseconds(duration_ms));

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

        const bool is_p2p_message =
            type == "punch" || type == "punch_ack" ||
            type == "noise_handshake" || type == "p2p_packet";
        if (is_p2p_message) {
            if (!to_node_id.empty() && to_node_id != my_node_id_) {
                spdlog::debug("UdpPuncher: ignoring {} for node_id={} (expected {})",
                              type, to_node_id, my_node_id_);
                do_receive();
                return;
            }
            if (!from_node_id.empty() && from_node_id == my_node_id_) {
                spdlog::debug("UdpPuncher: ignoring looped-back {} from self", type);
                do_receive();
                return;
            }
        }

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
                punch_path_ready_ = true;
            }

            sendAck(my_node_id_, from_node_id, remote_);
            spdlog::info("UdpPuncher: -> ack to {} at {}:{}",
                         from_node_id,
                         remote_.address().to_string(),
                         remote_.port());

        } else if (type == "punch_ack") {
            ack_time_ms_ = now_ms();
            peer_endpoint_ = remote_;
            peer_endpoint_known_ = true;
            punch_path_ready_ = true;

            if (noise_enabled_) {
                punching_ = false;
                punch_timer_.cancel();
                if (noise_initiator_) {
                    startNoiseHandshake();
                } else {
                    armTimeout(std::chrono::milliseconds(3000));
                }
            } else if (!success_.load()) {
                completeSecurePunch(from_node_id);
            }

        } else if (type == "noise_handshake") {
            if (!noise_enabled_ || !noise_) {
                spdlog::warn("UdpPuncher: received noise_handshake without local Noise state");
                do_receive();
                return;
            }

            const auto stage = json.value("stage", 0);
            const auto encoded = json.value("payload", std::string());
            auto payload = NoiseProtocol::decodeBase64(encoded);
            if (!payload) {
                spdlog::warn("UdpPuncher: invalid noise_handshake payload");
                do_receive();
                return;
            }

            peer_endpoint_ = remote_;
            peer_endpoint_known_ = true;
            punch_path_ready_ = true;

            if (stage == 1 && !noise_initiator_) {
                auto response = noise_->handleHandshakeMessage1(*payload);
                if (response) {
                    sendNoiseHandshake(2, *response);
                    completeSecurePunch(from_node_id);
                } else if (noise_->handshakeComplete() && !last_noise_message_.empty()) {
                    sendNoiseHandshake(last_noise_stage_, last_noise_message_);
                } else {
                    spdlog::warn("UdpPuncher: failed to process Noise message1 from {}", from_node_id);
                }
            } else if (stage == 2 && noise_initiator_) {
                if (noise_->handshakeComplete()) {
                    spdlog::debug("UdpPuncher: ignoring duplicate Noise message2 from {}", from_node_id);
                } else if (noise_->handleHandshakeMessage2(*payload)) {
                    completeSecurePunch(from_node_id);
                } else {
                    spdlog::warn("UdpPuncher: failed to process Noise message2 from {}", from_node_id);
                }
            } else {
                spdlog::debug("UdpPuncher: ignoring unexpected noise_handshake stage={} role={}",
                              stage, noise_initiator_ ? "initiator" : "responder");
            }

        } else if (type == "p2p_packet") {
            auto from_id = json.value("from_node_id", std::string());
            auto payload = json.value("payload", std::string());
            auto seq = json.value("seq", int64_t(0));

            if (from_id == target_node_id_) {
                peer_endpoint_ = remote_;
                peer_endpoint_known_ = true;
            }

            if (!noise_enabled_ || !noise_ || !noise_ready_) {
                spdlog::warn("UdpPuncher: dropping unready p2p_packet seq={} from {}", seq, from_id);
                do_receive();
                return;
            }
            if (seq < 0) {
                spdlog::warn("UdpPuncher: dropping negative seq from {}", from_id);
                do_receive();
                return;
            }

            auto ciphertext = NoiseProtocol::decodeBase64(payload);
            if (!ciphertext) {
                spdlog::warn("UdpPuncher: invalid p2p_packet base64 seq={} from {}", seq, from_id);
                do_receive();
                return;
            }

            auto ad = buildPacketAd(from_id, to_node_id, seq);
            auto plaintext = noise_->decrypt(
                static_cast<std::uint64_t>(seq),
                *ciphertext,
                std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(ad.data()),
                    ad.size()));
            if (!plaintext) {
                spdlog::warn("UdpPuncher: decrypt failed for p2p_packet seq={} from {}", seq, from_id);
                do_receive();
                return;
            }

            std::string cleartext(plaintext->begin(), plaintext->end());
            if (peer_packet_cb_) {
                peer_packet_cb_(from_id, cleartext, seq);
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
    if (!success_ || !peer_endpoint_known_ || !noise_enabled_ || !noise_ || !noise_ready_ || seq < 0) {
        return false;
    }

    auto ad = buildPacketAd(from_node_id, to_node_id, seq);
    auto ciphertext = noise_->encrypt(
        static_cast<std::uint64_t>(seq),
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(payload_data.data()),
            payload_data.size()),
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(ad.data()),
            ad.size()));
    if (!ciphertext) {
        spdlog::warn("UdpPuncher: encrypt failed for p2p_packet seq={}", seq);
        return false;
    }

    nlohmann::json msg = {
        {"type", "p2p_packet"},
        {"from_node_id", from_node_id},
        {"to_node_id", to_node_id},
        {"seq", seq},
        {"payload", NoiseProtocol::encodeBase64(*ciphertext)}
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
    auto buffer = std::make_shared<std::string>(payload);
    socket_.async_send_to(
        net::buffer(*buffer), target,
        [buffer](boost::system::error_code ec, std::size_t) {
            if (ec) {
                spdlog::error("UdpPuncher: async send error: {}", ec.message());
            }
        });
}

void UdpPuncher::armTimeout(std::chrono::milliseconds timeout) {
    timeout_timer_.cancel();
    timeout_timer_.expires_after(timeout);
    timeout_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return;
        self->punching_ = false;
        if (!self->success_) {
            spdlog::warn("UdpPuncher: timeout, P2P failed for node_id={}",
                         self->target_node_id_);
            self->noise_retry_timer_.cancel();
            if (self->punch_cb_) {
                PunchResult r;
                r.success = false;
                r.remote_node_id = self->target_node_id_;
                self->punch_cb_(r);
            }
        }
    });
}

void UdpPuncher::completeSecurePunch(const std::string& remote_node_id) {
    if (success_.exchange(true)) {
        return;
    }

    noise_ready_ = noise_enabled_ ? (noise_ && noise_->handshakeComplete()) : true;
    ack_time_ms_ = now_ms();
    auto rtt = ack_time_ms_ - start_time_ms_;

    punching_ = false;
    timeout_timer_.cancel();
    punch_timer_.cancel();
    noise_retry_timer_.cancel();

    spdlog::info("═══════════════════════════════════════════");
    spdlog::info("  P2P SUCCESS with node_id={}", remote_node_id);
    spdlog::info("  Remote addr: {}:{}",
                 peer_endpoint_.address().to_string(), peer_endpoint_.port());
    spdlog::info("  RTT: {}ms", rtt);
    if (noise_enabled_) {
        spdlog::info("  Noise:       {}", noise_ready_ ? "ready" : "disabled");
    }
    spdlog::info("═══════════════════════════════════════════");

    if (punch_cb_) {
        PunchResult r;
        r.success = true;
        r.remote_node_id = remote_node_id;
        r.remote_ip = peer_endpoint_.address().to_string();
        r.remote_port = peer_endpoint_.port();
        r.rtt_ms = rtt;
        punch_cb_(r);
    }
}

void UdpPuncher::startNoiseHandshake() {
    if (!noise_enabled_ || !noise_ || !noise_initiator_ || !peer_endpoint_known_ || success_) {
        return;
    }

    if (last_noise_stage_ == 1 && !last_noise_message_.empty()) {
        sendNoiseHandshake(last_noise_stage_, last_noise_message_);
        return;
    }

    auto msg1 = noise_->buildHandshakeMessage1();
    if (!msg1) {
        spdlog::warn("UdpPuncher: failed to build Noise handshake message1");
        return;
    }

    sendNoiseHandshake(1, *msg1);
    armTimeout(std::chrono::milliseconds(3000));
    scheduleNoiseRetry();
}

void UdpPuncher::sendNoiseHandshake(int stage, std::span<const std::uint8_t> payload) {
    if (!peer_endpoint_known_) {
        return;
    }

    last_noise_stage_ = stage;
    last_noise_message_.assign(payload.begin(), payload.end());

    nlohmann::json msg = {
        {"type", "noise_handshake"},
        {"from_node_id", my_node_id_},
        {"to_node_id", target_node_id_},
        {"stage", stage},
        {"payload", NoiseProtocol::encodeBase64(payload)}
    };
    do_send_to(msg.dump(), peer_endpoint_);
    spdlog::debug("UdpPuncher: -> noise_handshake stage={} to {}:{}",
                  stage,
                  peer_endpoint_.address().to_string(),
                  peer_endpoint_.port());
}

void UdpPuncher::scheduleNoiseRetry() {
    if (!noise_initiator_ || success_ || !peer_endpoint_known_ || last_noise_stage_ != 1 || last_noise_message_.empty()) {
        return;
    }

    noise_retry_timer_.cancel();
    noise_retry_timer_.expires_after(std::chrono::milliseconds(400));
    noise_retry_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec || self->success_ || !self->noise_initiator_) {
            return;
        }
        ++self->handshake_retry_count_;
        self->sendNoiseHandshake(self->last_noise_stage_, self->last_noise_message_);
        self->scheduleNoiseRetry();
    });
}

std::string UdpPuncher::buildPacketAd(const std::string& from_node_id,
                                      const std::string& to_node_id,
                                      int64_t seq) {
    return from_node_id + "|" + to_node_id + "|" + std::to_string(seq);
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
