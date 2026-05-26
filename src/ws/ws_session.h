#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <queue>
#include <optional>

#include "model/device.h"
#include "auth/jwt.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace beast_ws {

// Forward declarations
class WsSession;
using WsSessionPtr = std::shared_ptr<WsSession>;

class WsSession : public std::enable_shared_from_this<WsSession> {
public:
    // Callback types
    using AuthCallback = std::function<std::optional<auth::JwtPayload>(const std::string& token)>;
    using PostAuthCallback = std::function<void(WsSessionPtr, int64_t user_id, const std::string& username)>;
    using MessageHandler = std::function<void(WsSessionPtr, const std::string& node_id, const nlohmann::json& msg)>;
    using DisconnectHandler = std::function<void(const std::string& node_id)>;

    WsSession(tcp::socket&& socket,
              AuthCallback auth_cb,
              PostAuthCallback post_auth_cb,
              MessageHandler msg_handler,
              DisconnectHandler disconnect_handler,
              int heartbeat_timeout_sec);
    ~WsSession();

    void start();
    void send_json(const std::string& json_str);
    void close(const std::string& reason = "server_close");

    // Set/get device info after auth
    void set_device_info(const Device& device);
    std::optional<Device> get_device_info() const;
    const std::string& node_id() const { return node_id_; }

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void do_write();
    void on_write(beast::error_code ec, std::size_t bytes_transferred);
    void check_heartbeat();
    void handle_message(const std::string& data);

    websocket::stream<beast::tcp_stream> ws_;
    net::steady_timer heartbeat_timer_;
    net::strand<net::any_io_executor> strand_;

    beast::flat_buffer read_buffer_;
    std::queue<std::string> write_queue_;
    bool writing_ = false;

    AuthCallback auth_cb_;
    PostAuthCallback post_auth_cb_;
    MessageHandler msg_handler_;
    DisconnectHandler disconnect_handler_;

    std::string node_id_;
    std::optional<Device> device_info_;
    bool authenticated_ = false;
    int heartbeat_timeout_sec_;
    std::chrono::steady_clock::time_point last_heartbeat_;
};

} // namespace beast_ws
