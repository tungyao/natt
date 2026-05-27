#include "client/WsClient.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

WsClient::WsClient()
    : ioc_(1)
{
}

WsClient::~WsClient() {
    close();
    joinReader();
}

bool WsClient::connect(const std::string& host, const std::string& port, const std::string& path) {
    try {
        tcp::resolver resolver(ioc_);
        auto results = resolver.resolve(host, port);

        ws_ = std::make_unique<websocket::stream<beast::tcp_stream>>(ioc_);
        beast::get_lowest_layer(*ws_).connect(results);

        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "NATMesh-Client/0.1");
            }));

        ws_->handshake(host + ":" + port, path);
        connected_ = true;

        spdlog::info("WsClient: connected to ws://{}:{}{}", host, port, path);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("WsClient: connect failed: {}", e.what());
        connected_ = false;
        return false;
    }
}

bool WsClient::send(const nlohmann::json& msg) {
    if (!connected_ || !ws_) return false;

    try {
        auto payload = msg.dump();
        ws_->text(true);
        ws_->write(net::buffer(payload));
        return true;
    } catch (const std::exception& e) {
        spdlog::error("WsClient: send error: {}", e.what());
        connected_ = false;
        return false;
    }
}

bool WsClient::receive(nlohmann::json& msg, int timeout_ms) {
    if (!connected_ || !ws_) return false;

    try {
        beast::flat_buffer buf;
        ws_->read(buf);

        auto data = beast::buffers_to_string(buf.data());
        msg = nlohmann::json::parse(data);
        return true;
    } catch (const std::exception& e) {
        if (connected_ || reader_running_) {
            spdlog::error("WsClient: receive error: {}", e.what());
        }
        connected_ = false;
        return false;
    }
}

void WsClient::close() {
    reader_running_ = false;
    connected_ = false;

    // Close socket to interrupt any blocking reads in the reader thread
    if (ws_) {
        beast::error_code ec;
        auto& socket = beast::get_lowest_layer(*ws_).socket();
        socket.cancel(ec);
        socket.shutdown(tcp::socket::shutdown_both, ec);
        socket.close(ec);
    }
    ioc_.stop();
}

void WsClient::joinReader() {
    if (reader_thread_.joinable() &&
        reader_thread_.get_id() != std::this_thread::get_id()) {
        reader_thread_.join();
    }
}

void WsClient::startReader() {
    if (reader_running_.exchange(true)) return;

    reader_thread_ = std::thread([this]() {
        spdlog::debug("WsClient: reader thread started");
        while (reader_running_ && connected_) {
            try {
                nlohmann::json msg;
                if (receive(msg)) {
                    auto type = msg["type"].get<std::string>();
                    spdlog::debug("WsClient: reader <- type={}", type);
                    if (msg_cb_) {
                        msg_cb_(type, msg);
                    }
                } else {
                    break;
                }
            } catch (const std::exception& e) {
                if (reader_running_) {
                    spdlog::error("WsClient: reader error: {}", e.what());
                }
                break;
            }
        }
        spdlog::debug("WsClient: reader thread stopped");
    });
}
