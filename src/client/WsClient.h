#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WsClient : public std::enable_shared_from_this<WsClient> {
public:
    using MessageCallback = std::function<void(const std::string& type, const nlohmann::json& data)>;

    WsClient();
    ~WsClient();

    // Connect to WebSocket server (synchronous, blocking)
    bool connect(const std::string& host, const std::string& port, const std::string& path = "/api/v1/ws/nat");

    // Send a JSON message (synchronous, blocking)
    bool send(const nlohmann::json& msg);

    // Receive next message (blocking, thread-safe via internal io_context)
    bool receive(nlohmann::json& msg, int timeout_ms = 5000);

    // Close connection
    void close();

    // Wait for the background reader to exit after close()
    void joinReader();

    // Register message callback (called from reader thread)
    void setMessageCallback(MessageCallback cb) { msg_cb_ = std::move(cb); }

    // Check connection state
    bool isConnected() const { return connected_; }

    // Start reader thread that calls the message callback
    void startReader();

private:
    void reader_thread_func();

    // Must outlive ws_, because Beast stream destruction touches io_context services.
    net::io_context ioc_;
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reader_running_{false};

    MessageCallback msg_cb_;

    // Internal thread for background reading
    std::thread reader_thread_;
    std::mutex read_mutex_;
    std::mutex write_mutex_;
};
