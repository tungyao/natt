#include "ws/wss_session.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace beast_ws {

// ── JWT-auth constructor (legacy) ────────────────────────────
WssSession::WssSession(std::shared_ptr<ssl_stream> ssl_sock,
                       http::request<http::string_body> request,
                       AuthCallback auth_cb,
                       PostAuthCallback post_auth_cb,
                       MessageHandler msg_handler,
                       DisconnectHandler disconnect_handler,
                       int heartbeat_timeout_sec)
    : ssl_sock_(std::move(ssl_sock))
    , ws_(*ssl_sock_)
    , heartbeat_timer_(ws_.get_executor())
    , strand_(net::make_strand(ws_.get_executor()))
    , http_request_(std::move(request))
    , auth_cb_(std::move(auth_cb))
    , post_auth_cb_(std::move(post_auth_cb))
    , msg_handler_(std::move(msg_handler))
    , disconnect_handler_(std::move(disconnect_handler))
    , heartbeat_timeout_sec_(heartbeat_timeout_sec)
    , last_heartbeat_(std::chrono::steady_clock::now())
    , use_node_auth_(false)
{
}

// ── Node-auth constructor (new protocol) ─────────────────────
WssSession::WssSession(std::shared_ptr<ssl_stream> ssl_sock,
                       http::request<http::string_body> request,
                       NodeAuthCallback node_auth_cb,
                       PostNodeAuthCallback post_node_auth_cb,
                       MessageHandler msg_handler,
                       DisconnectHandler disconnect_handler,
                       int heartbeat_timeout_sec)
    : ssl_sock_(std::move(ssl_sock))
    , ws_(*ssl_sock_)
    , heartbeat_timer_(ws_.get_executor())
    , strand_(net::make_strand(ws_.get_executor()))
    , http_request_(std::move(request))
    , node_auth_cb_(std::move(node_auth_cb))
    , post_node_auth_cb_(std::move(post_node_auth_cb))
    , msg_handler_(std::move(msg_handler))
    , disconnect_handler_(std::move(disconnect_handler))
    , heartbeat_timeout_sec_(heartbeat_timeout_sec)
    , last_heartbeat_(std::chrono::steady_clock::now())
    , use_node_auth_(true)
{
}

WssSession::~WssSession() {
    heartbeat_timer_.cancel();
}

void WssSession::start() {
    // SSL handshake was already done in HttpServer::handle_tls_connection.
    // Just do the WebSocket accept.
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(beast::http::field::server, "NATMesh-Server/0.1");
        }));

    ws_.async_accept(http_request_,
        [self = shared_from_this()](beast::error_code ec) {
            if (ec) {
                spdlog::error("WSS accept error: {}", ec.message());
                return;
            }
            self->do_read();
        });

    check_heartbeat();
}

void WssSession::send_json(const std::string& json_str) {
    net::post(strand_, [self = shared_from_this(), msg = json_str]() {
        self->write_queue_.push(msg);
        if (!self->writing_) {
            self->do_write();
        }
    });
}

void WssSession::close(const std::string& reason) {
    net::post(strand_, [self = shared_from_this(), reason]() {
        beast::error_code ec;
        websocket::close_reason cr;
        cr.code = websocket::close_code::normal;
        cr.reason = reason;
        self->ws_.close(cr, ec);
        if (ec) {
            spdlog::warn("WSS close error: {}", ec.message());
        }
    });
}

void WssSession::set_device_info(const Device& device) {
    device_info_ = device;
    node_id_ = device.node_id;
}

std::optional<Device> WssSession::get_device_info() const {
    return device_info_;
}

void WssSession::do_read() {
    ws_.async_read(read_buffer_,
        [self = shared_from_this()](beast::error_code ec, std::size_t bytes) {
            if (ec) {
                if (ec != websocket::error::closed) {
                    spdlog::debug("WSS read error: {}", ec.message());
                }
                if (ec != boost::asio::error::operation_aborted &&
                    ec != websocket::error::closed &&
                    !self->node_id_.empty() && self->disconnect_handler_) {
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

void WssSession::do_write() {
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
                spdlog::error("WSS write error: {}", ec.message());
                if (ec != boost::asio::error::operation_aborted &&
                    !self->node_id_.empty() && self->disconnect_handler_) {
                    self->disconnect_handler_(self->node_id_);
                }
                return;
            }

            self->write_queue_.pop();
            self->do_write();
        });
}

void WssSession::check_heartbeat() {
    heartbeat_timer_.expires_after(std::chrono::seconds(heartbeat_timeout_sec_));
    heartbeat_timer_.async_wait(
        [self = shared_from_this()](beast::error_code ec) {
            if (ec) return;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - self->last_heartbeat_).count();

            if (elapsed >= self->heartbeat_timeout_sec_) {
                spdlog::warn("WSS heartbeat timeout for node_id={}", self->node_id_);
                if (!self->node_id_.empty() && self->disconnect_handler_) {
                    self->disconnect_handler_(self->node_id_);
                }
                self->close("heartbeat_timeout");
                return;
            }

            self->check_heartbeat();
        });
}

void WssSession::handle_message(const std::string& data) {
    try {
        auto json = nlohmann::json::parse(data);
        auto type_it = json.find("type");
        if (type_it == json.end()) {
            nlohmann::json response = {
                {"type", "error"},
                {"message", "Missing 'type' field"}
            };
            send_json(response.dump());
            return;
        }

        auto type = type_it->get<std::string>();

        if (!authenticated_) {
            handle_auth_message(json);
            return;
        }

        if (type == "heartbeat") {
            last_heartbeat_ = std::chrono::steady_clock::now();
        }

        last_heartbeat_ = std::chrono::steady_clock::now();

        if (msg_handler_) {
            msg_handler_(shared_from_this(), node_id_, json);
        } else if (type == "heartbeat") {
            nlohmann::json response = {{"type", "pong"}};
            send_json(response.dump());
        }

    } catch (const std::exception& e) {
        spdlog::warn("Invalid WSS message: {}", e.what());
        nlohmann::json response = {
            {"type", "error"},
            {"message", "Invalid message format"}
        };
        send_json(response.dump());
    }
}

void WssSession::handle_auth_message(const nlohmann::json& json) {
    auto type = json["type"].get<std::string>();

    if (type == "auth") {
        if (use_node_auth_) {
            auto node_id = json.value("node_id", std::string());
            auto network_id = json.value("network_id", std::string());
            auto public_key = json.value("public_key", std::string());

            if (node_id.empty()) {
                nlohmann::json resp = {{"type", "error"}, {"message", "node_id is required"}};
                send_json(resp.dump());
                return;
            }

            if (!node_auth_cb_) {
                nlohmann::json resp = {{"type", "error"}, {"message", "Server does not support node auth"}};
                send_json(resp.dump());
                return;
            }

            auto result = node_auth_cb_(node_id, network_id, public_key);
            if (result.success) {
                authenticated_ = true;
                node_id_ = result.node_id;
                network_id_ = result.network_id;
                public_key_ = result.public_key;
                last_heartbeat_ = std::chrono::steady_clock::now();

                nlohmann::json resp = {
                    {"type", "auth_ok"},
                    {"node_id", node_id_},
                    {"network_id", network_id_}
                };
                send_json(resp.dump());
                spdlog::info("WSS node-auth: node_id={}, network_id={}",
                             node_id_, network_id_);

                if (post_node_auth_cb_) {
                    post_node_auth_cb_(shared_from_this());
                }
            } else {
                nlohmann::json resp = {{"type", "error"}, {"message", result.error_msg}};
                send_json(resp.dump());
            }
        } else {
            auto token = json["token"].get<std::string>();
            auto payload = auth_cb_(token);
            if (payload.has_value()) {
                authenticated_ = true;
                last_heartbeat_ = std::chrono::steady_clock::now();

                if (post_auth_cb_) {
                    post_auth_cb_(shared_from_this(), payload->user_id, payload->username);
                }

                nlohmann::json resp = {
                    {"type", "auth_ok"},
                    {"user_id", payload->user_id},
                    {"username", payload->username}
                };
                send_json(resp.dump());
            } else {
                nlohmann::json resp = {{"type", "error"}, {"message", "Invalid token"}};
                send_json(resp.dump());
            }
        }
    } else {
        nlohmann::json resp = {{"type", "error"}, {"message", "Authentication required"}};
        send_json(resp.dump());
    }
}

} // namespace beast_ws
