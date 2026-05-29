#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "config/config.h"
#include "auth/jwt.h"
#include "ipam/IpAllocator.h"
#include "service/user_service.h"
#include "service/device_service.h"
#include "service/network_service.h"
#include "service/peer_manager.h"
#include "tun/TunInterface.h"
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
               IpAllocator& ipam,
               auth::JwtManager& jwt,
               PeerManager& peer_mgr);
    ~HttpServer();

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
    http::response<http::string_body> handle_admin_overview(const http::request<http::string_body>& req, int64_t user_id);
    http::response<http::string_body> handle_admin_device_detail(const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id);

    http::response<http::string_body> handle_health(const http::request<http::string_body>& req);

    // Helpers
    http::response<http::string_body> make_json_response(http::status status, const nlohmann::json& body);
    http::response<http::string_body> make_html_response(http::status status, const std::string& body);
    http::response<http::string_body> make_error_response(http::status status, const std::string& message);
    std::optional<auth::JwtPayload> authenticate(const http::request<http::string_body>& req);

    // WebSocket handler
    void handle_websocket_upgrade(tcp::socket&& socket, http::request<http::string_body> req);
    NodeInfo assign_virtual_ip(const std::string& node_id,
                               const std::string& network_id,
                               const std::string& public_key);
    bool ensure_tun_gateway(const std::string& network_id);
    void do_tun_read(const std::string& network_id);
    void handle_tun_packet(const std::string& node_id, const nlohmann::json& msg);
    void forward_tun_packet(const std::string& network_id,
                            const std::vector<uint8_t>& packet);
    void ensure_node_auth_persisted(const NodeInfo& info);

    // Auto-connect: send punch_start to a pair of nodes
    void signal_punch_start(const NodeInfo& requester,
                            const NodeInfo& target);

    net::io_context& ioc_;
    tcp::acceptor acceptor_;
    Config config_;

    UserService& user_svc_;
    DeviceService& device_svc_;
    NetworkService& network_svc_;
    IpAllocator& ipam_;
    auth::JwtManager& jwt_;
    PeerManager& peer_mgr_;

    // ── New NAT modules ──
    SessionManager session_mgr_;
    NodeRegistry node_registry_;
    MessageDispatcher msg_dispatcher_;
    std::shared_ptr<HeartbeatMonitor> heartbeat_monitor_;
    std::atomic<bool> shutting_down_{false};

    struct TunState {
        std::shared_ptr<TunInterface> tun;
        std::array<char, 2000> buffer;
    };
    std::mutex tun_mutex_;
    std::unordered_map<std::string, std::shared_ptr<TunState>> tun_by_network_;
};
