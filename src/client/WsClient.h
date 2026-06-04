#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
using ssl_stream = beast::ssl_stream<beast::tcp_stream>;

class WsClient : public std::enable_shared_from_this<WsClient> {
public:
    using MessageCallback = std::function<void(const std::string& type, const nlohmann::json& data)>;

    WsClient();
    ~WsClient();

    // Connect to WebSocket server (synchronous, blocking)
    bool connect(const std::string& host, const std::string& port, const std::string& path = "/api/v1/ws/nat");

    // Connect to WebSocket server over TLS (WSS).
    // If cert_pem is non-empty, it is used as the trusted CA cert to verify the server.
    // If cert_pem is empty, server certificate verification is skipped.
    bool connectSecure(const std::string& host, const std::string& port,
                       const std::string& path = "/api/v1/ws/nat",
                       const std::string& cert_pem = "");

    // Send a JSON message (synchronous, blocking)
    bool send(const nlohmann::json& msg);

    // Receive next message (blocking, for init phase only)
    bool receive(nlohmann::json& msg, int timeout_ms = 5000);

    // Close connection
    void close();

    // Wait for the background reader to exit after close()
    void joinReader();

    // Register message callback (called from the async ioc thread)
    void setMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

    // Check connection state
    bool isConnected() const { return connected_; }

    // Start async read/write loop on the given io_context.
    // After this is called, send() uses async_write on ioc.
    void startAsync(net::io_context& ioc);

private:
    void do_async_read();
    void do_flush_queue();

    // Must outlive ws_, because Beast stream destruction touches io_context services.
    net::io_context ioc_;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws_;
    std::unique_ptr<websocket::stream<ssl_stream>> wss_;
    bool use_ssl_ = false;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reader_running_{false};

    MessageCallback msg_cb_;

    // Async mode (used after startAsync)
    net::io_context* async_ioc_ = nullptr;
    beast::flat_buffer read_buf_;
    std::deque<std::string> write_queue_;
    std::mutex queue_mutex_;
    bool async_write_pending_ = false;

    // Internal thread for background reading (legacy synchronous mode)
    std::thread reader_thread_;
    std::mutex write_mutex_;
};
