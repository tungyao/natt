#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>

#include "config/config.h"
#include "auth/jwt.h"
#include "service/user_service.h"
#include "service/device_service.h"
#include "service/network_service.h"
#include "service/peer_manager.h"
#include "nat/session_manager.h"
#include "nat/node_registry.h"
#include "nat/message_dispatcher.h"
#include "nat/heartbeat_monitor.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer : public std::enable_shared_from_this<HttpServer> {
public:
    HttpServer(net::io_context& ioc,
               const Config& config,
               UserService& user_svc,
               DeviceService& device_svc,
               NetworkService& network_svc,
               auth::JwtManager& jwt,
               PeerManager& peer_mgr);

    void start();

private:
    void do_accept();
    void handle_connection(tcp::socket socket);

    // REST handlers
    http::response<http::string_body> handle_rest_request(
        const http::request<http::string_body>& req);

    http::response<http::string_body> handle_register(const http::request<http::string_body>& req);
    http::response<http::string_body> handle_login(const http::request<http::string_body>& req);

    http::response<http::string_body> handle_create_device(const http::request<http::string_body>& req, int64_t user_id);
    http::response<http::string_body> handle_list_devices(const http::request<http::string_body>& req, int64_t user_id);
    http::response<http::string_body> handle_get_device(const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id);
    http::response<http::string_body> handle_delete_device(const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id);

    http::response<http::string_body> handle_create_network(const http::request<http::string_body>& req, int64_t user_id);
    http::response<http::string_body> handle_list_networks(const http::request<http::string_body>& req, int64_t user_id);
    http::response<http::string_body> handle_join_network(const http::request<http::string_body>& req, int64_t network_id);
    http::response<http::string_body> handle_leave_network(const http::request<http::string_body>& req, int64_t network_id, const std::string& node_id);
    http::response<http::string_body> handle_list_network_devices(const http::request<http::string_body>& req, int64_t network_id);

    http::response<http::string_body> handle_ws_token(const http::request<http::string_body>& req, int64_t user_id);

    http::response<http::string_body> handle_health(const http::request<http::string_body>& req);

    // Helpers
    http::response<http::string_body> make_json_response(http::status status, const nlohmann::json& body);
    http::response<http::string_body> make_error_response(http::status status, const std::string& message);
    std::optional<auth::JwtPayload> authenticate(const http::request<http::string_body>& req);

    // WebSocket handler
    void handle_websocket_upgrade(tcp::socket&& socket, http::request<http::string_body> req);

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    Config config_;

    UserService& user_svc_;
    DeviceService& device_svc_;
    NetworkService& network_svc_;
    auth::JwtManager& jwt_;
    PeerManager& peer_mgr_;

    // ── New NAT modules ──
    SessionManager session_mgr_;
    NodeRegistry node_registry_;
    MessageDispatcher msg_dispatcher_;
    std::shared_ptr<HeartbeatMonitor> heartbeat_monitor_;
};
