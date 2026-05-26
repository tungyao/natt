#include "relay/RelayServer.h"
#include <spdlog/spdlog.h>

RelayServer::RelayServer(net::io_context& ioc, uint16_t port)
    : ioc_(ioc)
    , socket_(ioc, udp::endpoint(udp::v4(), port))
    , port_(port)
    , cleanup_timer_(ioc)
{
    spdlog::info("RelayServer: listening on UDP port {}", port_);
}

RelayServer::~RelayServer() {
    stop();
}

void RelayServer::start() {
    do_receive();
    schedule_cleanup();
}

void RelayServer::stop() {
    boost::system::error_code ec;
    cleanup_timer_.cancel();
    socket_.close(ec);
    spdlog::info("RelayServer: stopped");
}

// ── Receive Loop ─────────────────────────────────────────────

void RelayServer::do_receive() {
    socket_.async_receive_from(
        net::buffer(recv_buffer_), sender_,
        [self = shared_from_this()](boost::system::error_code ec, std::size_t len) {
            self->handle_receive(ec, len);
        });
}

void RelayServer::handle_receive(boost::system::error_code ec, std::size_t bytes_recvd) {
    if (ec) {
        if (ec != net::error::operation_aborted) {
            spdlog::error("RelayServer: receive error: {}", ec.message());
            do_receive();
        }
        return;
    }

    try {
        std::string_view data(recv_buffer_.data(), bytes_recvd);
        auto msg = nlohmann::json::parse(data);
        auto type = msg.value("type", std::string());

        spdlog::debug("RelayServer: <- type={} from {}:{}",
                      type, sender_.address().to_string(), sender_.port());

        if (type == "relay_register") {
            handle_register(msg, sender_);
        } else if (type == "relay_packet") {
            handle_relay_packet(msg);
        } else if (type == "relay_heartbeat") {
            handle_heartbeat(msg, sender_);
        } else {
            spdlog::warn("RelayServer: unknown message type: {}", type);
            send_error(sender_, "unknown_type");
        }
    } catch (const std::exception& e) {
        spdlog::warn("RelayServer: invalid packet: {}", e.what());
        send_error(sender_, "invalid_message");
    }

    do_receive();
}

// ── Message Handlers ────────────────────────────────────────

void RelayServer::handle_register(const nlohmann::json& msg,
                                  const udp::endpoint& sender) {
    auto node_id = msg.value("node_id", std::string());
    auto network_id = msg.value("network_id", std::string());
    auto token = msg.value("token", std::string());

    if (node_id.empty() || network_id.empty()) {
        spdlog::warn("RelayServer: incomplete register from {}:{}",
                     sender.address().to_string(), sender.port());
        send_error(sender, "missing_fields");
        return;
    }

    // Simple token validation (extend for JWT later)
    if (token.empty()) {
        spdlog::warn("RelayServer: missing token from node {} ({})",
                     node_id, sender.address().to_string());
        send_error(sender, "missing_token");
        return;
    }

    registry_.registerNode(node_id, network_id, sender);

    // Send register_ok
    nlohmann::json ok = {
        {"type", "relay_register_ok"},
        {"node_id", node_id}
    };
    send_to(ok.dump(), sender);

    spdlog::info("RelayServer: registered node {} (network={}) at {}:{}",
                 node_id, network_id,
                 sender.address().to_string(), sender.port());
}

void RelayServer::handle_relay_packet(const nlohmann::json& msg) {
    auto from_node_id = msg.value("from_node_id", std::string());
    auto to_node_id = msg.value("to_node_id", std::string());
    auto payload = msg.value("payload", std::string());
    auto seq = msg.value("seq", int64_t(0));

    if (from_node_id.empty() || to_node_id.empty()) {
        spdlog::warn("RelayServer: relay_packet missing node ids");
        return;
    }

    // Verify from_node_id is registered
    if (!registry_.isOnline(from_node_id)) {
        spdlog::warn("RelayServer: from_node {} not registered", from_node_id);
        // Send error back to sender — but we don't know who sent it without looking up
        // by endpoint. The sender is in sender_ from the receive, but we need to get
        // the registered endpoint for from_node_id to send errors back.
        udp::endpoint from_ep;
        if (registry_.findEndpoint(from_node_id, from_ep)) {
            send_error(from_ep, "sender_not_registered");
        }
        return;
    }

    // Verify to_node_id is online
    udp::endpoint target_ep;
    if (!registry_.findEndpoint(to_node_id, target_ep)) {
        spdlog::warn("RelayServer: target node {} not found (offline)", to_node_id);
        udp::endpoint from_ep;
        if (registry_.findEndpoint(from_node_id, from_ep)) {
            send_error(from_ep, "target_offline");
        }
        return;
    }

    // Verify from and to belong to the same network_id
    std::string from_net, to_net;
    registry_.findNetworkId(from_node_id, from_net);
    registry_.findNetworkId(to_node_id, to_net);
    if (from_net != to_net) {
        spdlog::warn("RelayServer: network mismatch: {} ({}) -> {} ({})",
                     from_node_id, from_net, to_node_id, to_net);
        udp::endpoint from_ep;
        if (registry_.findEndpoint(from_node_id, from_ep)) {
            send_error(from_ep, "network_mismatch");
        }
        return;
    }

    // Forward the packet as-is to the target
    auto payload_str = msg.dump();
    send_to(payload_str, target_ep);

    spdlog::info("RelayServer: relayed packet seq={} from {} to {} (payload_len={})",
                 seq, from_node_id, to_node_id, payload.size());
}

void RelayServer::handle_heartbeat(const nlohmann::json& msg,
                                   const udp::endpoint& sender) {
    auto node_id = msg.value("node_id", std::string());

    if (node_id.empty()) {
        return;
    }

    // Check that the node is registered
    if (!registry_.isOnline(node_id)) {
        // Auto-register on heartbeat if not registered (optional)
        spdlog::warn("RelayServer: heartbeat from unregistered node {} ({}:{})",
                     node_id, sender.address().to_string(), sender.port());
        send_error(sender, "not_registered");
        return;
    }

    registry_.updateHeartbeat(node_id);
    spdlog::debug("RelayServer: heartbeat from node {}", node_id);
}

// ── Send Helpers ────────────────────────────────────────────

void RelayServer::send_to(const std::string& payload, const udp::endpoint& dest) {
    boost::system::error_code ec;
    socket_.send_to(net::buffer(payload), dest, 0, ec);
    if (ec) {
        spdlog::error("RelayServer: send error to {}:{}: {}",
                      dest.address().to_string(), dest.port(), ec.message());
    }
}

void RelayServer::send_error(const udp::endpoint& dest, const std::string& message) {
    nlohmann::json err = {
        {"type", "error"},
        {"message", message}
    };
    send_to(err.dump(), dest);
    spdlog::debug("RelayServer: sent error '{}' to {}:{}",
                  message, dest.address().to_string(), dest.port());
}

// ── Cleanup ─────────────────────────────────────────────────

void RelayServer::schedule_cleanup() {
    cleanup_timer_.expires_after(std::chrono::seconds(CLEANUP_INTERVAL_SEC));
    cleanup_timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec) {
            spdlog::debug("RelayServer: cleanup timer cancelled: {}", ec.message());
            return;
        }
        self->do_cleanup();
    });
}

void RelayServer::do_cleanup() {
    auto n_removed = registry_.cleanupExpired(NODE_TIMEOUT_SEC);
    if (n_removed > 0) {
        spdlog::info("RelayServer: {} nodes online after cleanup", registry_.size());
    }
    schedule_cleanup();
}
