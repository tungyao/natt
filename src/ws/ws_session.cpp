#include "ws/ws_session.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace beast_ws {

WsSession::WsSession(tcp::socket&& socket,
                     AuthCallback auth_cb,
                     PostAuthCallback post_auth_cb,
                     MessageHandler msg_handler,
                     DisconnectHandler disconnect_handler,
                     int heartbeat_timeout_sec)
    : ws_(std::move(socket))
    , heartbeat_timer_(ws_.get_executor())
    , strand_(net::make_strand(ws_.get_executor()))
    , auth_cb_(std::move(auth_cb))
    , post_auth_cb_(std::move(post_auth_cb))
    , msg_handler_(std::move(msg_handler))
    , disconnect_handler_(std::move(disconnect_handler))
    , heartbeat_timeout_sec_(heartbeat_timeout_sec)
    , last_heartbeat_(std::chrono::steady_clock::now())
{
}

WsSession::~WsSession() {
    if (!node_id_.empty() && disconnect_handler_) {
        disconnect_handler_(node_id_);
    }
}

void WsSession::start() {
    // Accept the WebSocket handshake
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(beast::http::field::server, "NATMesh-Server/0.1");
        }));

    ws_.async_accept(
        [self = shared_from_this()](beast::error_code ec) {
            if (!ec) {
                self->do_read();
            } else {
                spdlog::error("WS accept error: {}", ec.message());
            }
        });

    // Start heartbeat checker
    check_heartbeat();
}

void WsSession::send_json(const std::string& json_str) {
    net::post(strand_, [self = shared_from_this(), msg = json_str]() {
        self->write_queue_.push(msg);
        if (!self->writing_) {
            self->do_write();
        }
    });
}

void WsSession::close(const std::string& reason) {
    net::post(strand_, [self = shared_from_this(), reason]() {
        beast::error_code ec;
        websocket::close_reason cr;
        cr.code = websocket::close_code::normal;
        cr.reason = reason;
        self->ws_.close(cr, ec);
        if (ec) {
            spdlog::warn("WebSocket close error: {}", ec.message());
        }
    });
}

void WsSession::set_device_info(const Device& device) {
    device_info_ = device;
    node_id_ = device.node_id;
}

std::optional<Device> WsSession::get_device_info() const {
    return device_info_;
}

void WsSession::do_read() {
    ws_.async_read(read_buffer_,
        [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {
            if (ec) {
                if (ec != websocket::error::closed) {
                    spdlog::debug("WebSocket read error: {}", ec.message());
                }
                if (!self->node_id_.empty() && self->disconnect_handler_) {
                    self->disconnect_handler_(self->node_id_);
                }
                return;
            }

            auto data = beast::buffers_to_string(self->read_buffer_.data());
            self->read_buffer_.consume(self->read_buffer_.size());

            self->handle_message(data);
            self->do_read();
        });
}

void WsSession::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    writing_ = true;
    auto& msg = write_queue_.front();

    ws_.text(true);
    ws_.async_write(net::buffer(msg),
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (ec) {
                spdlog::error("WebSocket write error: {}", ec.message());
                if (!self->node_id_.empty() && self->disconnect_handler_) {
                    self->disconnect_handler_(self->node_id_);
                }
                return;
            }

            self->write_queue_.pop();
            self->do_write();
        });
}

void WsSession::check_heartbeat() {
    heartbeat_timer_.expires_after(std::chrono::seconds(heartbeat_timeout_sec_));
    heartbeat_timer_.async_wait(
        [self = shared_from_this()](beast::error_code ec) {
            if (ec) return; // cancelled

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - self->last_heartbeat_).count();

            if (elapsed >= self->heartbeat_timeout_sec_) {
                spdlog::warn("Heartbeat timeout for node_id={}", self->node_id_);
                if (!self->node_id_.empty() && self->disconnect_handler_) {
                    self->disconnect_handler_(self->node_id_);
                }
                self->close("heartbeat_timeout");
                return;
            }

            self->check_heartbeat();
        });
}

void WsSession::handle_message(const std::string& data) {
    try {
        auto json = nlohmann::json::parse(data);
        auto type = json["type"].get<std::string>();

        if (!authenticated_) {
            if (type == "auth") {
                auto token = json["token"].get<std::string>();
                auto payload = auth_cb_(token);
                if (payload.has_value()) {
                    authenticated_ = true;
                    last_heartbeat_ = std::chrono::steady_clock::now();

                    // Notify post-auth callback (for session registration)
                    if (post_auth_cb_) {
                        post_auth_cb_(shared_from_this(), payload->user_id, payload->username);
                    }

                    nlohmann::json response = {
                        {"type", "auth_ok"},
                        {"user_id", payload->user_id},
                        {"username", payload->username}
                    };
                    send_json(response.dump());
                } else {
                    nlohmann::json response = {
                        {"type", "error"},
                        {"message", "Invalid token"}
                    };
                    send_json(response.dump());
                }
            } else {
                nlohmann::json response = {
                    {"type", "error"},
                    {"message", "Authentication required"}
                };
                send_json(response.dump());
            }
            return;
        }

        if (type == "heartbeat") {
            last_heartbeat_ = std::chrono::steady_clock::now();
            nlohmann::json response = {{"type", "pong"}};
            send_json(response.dump());
            return;
        }

        // Delegate to message handler
        if (msg_handler_) {
            msg_handler_(shared_from_this(), node_id_, json);
        }

    } catch (const std::exception& e) {
        spdlog::warn("Invalid WebSocket message: {}", e.what());
        nlohmann::json response = {
            {"type", "error"},
            {"message", "Invalid message format"}
        };
        send_json(response.dump());
    }
}

} // namespace beast_ws
