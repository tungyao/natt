#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <queue>
#include <optional>
#include <functional>

#include "model/device.h"
#include "auth/jwt.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace beast_ws {

// Forward declarations
class WsSession;

// Abstract session interface — allows both WsSession and Wssession
// to be used interchangeably via shared_ptr<ISession>.
class ISession {
public:
    virtual ~ISession() = default;
    virtual void send_json(const std::string& json_str) = 0;
    virtual void close(const std::string& reason = "server_close") = 0;
    virtual void set_device_info(const Device& device) = 0;
    virtual std::optional<Device> get_device_info() const = 0;
    virtual const std::string& node_id() const = 0;
    virtual const std::string& network_id() const = 0;
    virtual bool is_authenticated() const = 0;
};

using SessionPtr = std::shared_ptr<ISession>;
using WsSessionPtr = SessionPtr;

// Auth result — either JWT-based or node_id-based
struct AuthResult {
    bool success = false;
    std::string error_msg;
    // JWT fields (legacy)
    int64_t user_id = 0;
    std::string username;
    // Node-based auth fields
    std::string node_id;
    std::string network_id;
    std::string public_key;
};

// Shared callback types for both WsSession and WssSession
using AuthCallback = std::function<std::optional<auth::JwtPayload>(const std::string& token)>;
using NodeAuthCallback = std::function<AuthResult(const std::string& node_id,
                                                    const std::string& network_id,
                                                    const std::string& public_key)>;
using PostAuthCallback = std::function<void(SessionPtr, int64_t user_id, const std::string& username)>;
using PostNodeAuthCallback = std::function<void(SessionPtr)>;
using MessageHandler = std::function<void(SessionPtr, const std::string& node_id, const nlohmann::json& msg)>;
using DisconnectHandler = std::function<void(const std::string& node_id)>;

class WsSession : public ISession, public std::enable_shared_from_this<WsSession> {
public:

    // Constructor with JWT auth (legacy)
    // Takes the parsed HTTP request so the WebSocket stream can complete the handshake.
    WsSession(tcp::socket&& socket,
              http::request<http::string_body> request,
              AuthCallback auth_cb,
              PostAuthCallback post_auth_cb,
              MessageHandler msg_handler,
              DisconnectHandler disconnect_handler,
              int heartbeat_timeout_sec);

    // Constructor with node_id-based auth (new protocol)
    WsSession(tcp::socket&& socket,
              http::request<http::string_body> request,
              NodeAuthCallback node_auth_cb,
              PostNodeAuthCallback post_node_auth_cb,
              MessageHandler msg_handler,
              DisconnectHandler disconnect_handler,
              int heartbeat_timeout_sec);

    ~WsSession();

    void start();
    void send_json(const std::string& json_str) override;
    void close(const std::string& reason = "server_close") override;

    // Set/get device info after auth
    void set_device_info(const Device& device) override;
    std::optional<Device> get_device_info() const override;
    const std::string& node_id() const override { return node_id_; }
    const std::string& network_id() const override { return network_id_; }
    bool is_authenticated() const override { return authenticated_; }

    // Store the HTTP request for async_accept
    http::request<http::string_body> http_request_;

private:
    void do_read();
    void do_write();
    void check_heartbeat();
    void handle_message(const std::string& data);
    void handle_auth_message(const nlohmann::json& json);

    websocket::stream<beast::tcp_stream> ws_;
    net::steady_timer heartbeat_timer_;
    net::strand<net::any_io_executor> strand_;

    beast::flat_buffer read_buffer_;
    std::queue<std::string> write_queue_;
    bool writing_ = false;

    // JWT auth (legacy)
    AuthCallback auth_cb_;
    PostAuthCallback post_auth_cb_;
    // Node-based auth (new protocol)
    NodeAuthCallback node_auth_cb_;
    PostNodeAuthCallback post_node_auth_cb_;

    MessageHandler msg_handler_;
    DisconnectHandler disconnect_handler_;

    std::string node_id_;
    std::string network_id_;
    std::string public_key_;
    std::optional<Device> device_info_;
    bool authenticated_ = false;
    int heartbeat_timeout_sec_;
    std::chrono::steady_clock::time_point last_heartbeat_;
    bool use_node_auth_ = false;
};

} // namespace beast_ws
