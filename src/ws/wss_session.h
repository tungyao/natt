#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>
#include <queue>
#include <optional>
#include <functional>

#include "model/device.h"
#include "auth/jwt.h"
#include "ws/ws_session.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using ssl_stream = beast::ssl_stream<beast::tcp_stream>;

namespace beast_ws {

// WSS session — takes an already-handshaked SSL stream + parsed HTTP request.
// Does WebSocket accept + async messaging over the established SSL channel.
class WssSession : public ISession, public std::enable_shared_from_this<WssSession> {
public:
    // JWT-auth constructor (legacy)
    WssSession(std::shared_ptr<ssl_stream> ssl_sock,
               http::request<http::string_body> request,
               AuthCallback auth_cb,
               PostAuthCallback post_auth_cb,
               MessageHandler msg_handler,
               DisconnectHandler disconnect_handler,
               int heartbeat_timeout_sec);

    // Node-auth constructor (new protocol)
    WssSession(std::shared_ptr<ssl_stream> ssl_sock,
               http::request<http::string_body> request,
               NodeAuthCallback node_auth_cb,
               PostNodeAuthCallback post_node_auth_cb,
               MessageHandler msg_handler,
               DisconnectHandler disconnect_handler,
               int heartbeat_timeout_sec);

    ~WssSession();

    void start();
    void send_json(const std::string& json_str) override;
    void close(const std::string& reason = "server_close") override;

    void set_device_info(const Device& device) override;
    std::optional<Device> get_device_info() const override;
    const std::string& node_id() const override { return node_id_; }
    const std::string& network_id() const override { return network_id_; }
    bool is_authenticated() const override { return authenticated_; }
    void markStale() override { stale_ = true; }

private:
    void do_ws_accept();
    void do_read();
    void do_write();
    void check_heartbeat();
    void handle_message(const std::string& data);
    void handle_auth_message(const nlohmann::json& json);

    std::shared_ptr<ssl_stream> ssl_sock_;
    websocket::stream<ssl_stream&> ws_;

    net::steady_timer heartbeat_timer_;
    net::strand<net::any_io_executor> strand_;
    http::request<http::string_body> http_request_;

    beast::flat_buffer read_buffer_;
    std::queue<std::string> write_queue_;
    bool writing_ = false;

    AuthCallback auth_cb_;
    PostAuthCallback post_auth_cb_;
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
    bool stale_ = false;
};

} // namespace beast_ws
