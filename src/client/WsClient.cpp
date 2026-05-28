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

    auto payload = msg.dump();

    // If async mode is active, queue the write on ioc_ (the stream's executor)
    if (reader_running_) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            write_queue_.push_back(std::move(payload));
        }
        net::post(ioc_, [self = shared_from_this()]() {
            self->do_flush_queue();
        });
        return true;
    }

    // Synchronous mode (init phase)
    try {
        std::lock_guard<std::mutex> lock(write_mutex_);
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

void WsClient::startAsync(net::io_context& ioc) {
    if (reader_running_.exchange(true)) return;
    if (!connected_ || !ws_) {
        reader_running_ = false;
        return;
    }
    async_ioc_ = &ioc;

    read_buf_.clear();
    do_async_read();

    // Run ioc_ (the stream's executor) on the reader thread
    // to drive async_read/async_write completions.
    reader_thread_ = std::thread([self = shared_from_this()]() {
        spdlog::debug("WsClient: async reader thread started");
        self->ioc_.run();
        spdlog::debug("WsClient: async reader thread stopped");
    });
}

void WsClient::do_async_read() {
    if (!reader_running_ || !connected_ || !ws_) return;

    ws_->async_read(read_buf_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
            if (!self->reader_running_ || !self->connected_) return;

            if (ec) {
                if (ec != net::error::operation_aborted &&
                    ec != beast::websocket::error::closed) {
                    spdlog::error("WsClient: async read error: {}", ec.message());
                }
                self->connected_ = false;
                return;
            }

            // Flush any pending writes before processing the incoming message
            self->do_flush_queue();

            try {
                auto data = beast::buffers_to_string(self->read_buf_.data());
                auto msg = nlohmann::json::parse(data);
                auto type = msg["type"].get<std::string>();
                spdlog::debug("WsClient: async <- type={}", type);
                if (self->msg_cb_) {
                    // Dispatch callback to the application ioc (puncher_ioc_).
                    // Re-check msg_cb_ inside the post to avoid use-after-free
                    // if stop() clears it before the handler runs.
                    net::post(*self->async_ioc_, [self, type, msg]() {
                        if (self->msg_cb_) {
                            self->msg_cb_(type, msg);
                        }
                    });
                }
            } catch (const std::exception& e) {
                spdlog::error("WsClient: message handler error: {}", e.what());
            }

            self->read_buf_.consume(self->read_buf_.size());
            self->do_async_read();
        });
}

void WsClient::do_flush_queue() {
    if (!connected_ || !ws_ || async_write_pending_) return;

    std::string payload;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (write_queue_.empty()) return;
        payload = std::move(write_queue_.front());
        write_queue_.pop_front();
    }

    async_write_pending_ = true;
    auto buf = std::make_shared<std::string>(std::move(payload));
    ws_->text(true);
    ws_->async_write(net::buffer(*buf),
        [self = shared_from_this(), buf](beast::error_code ec, std::size_t) {
            self->async_write_pending_ = false;
            if (ec) {
                spdlog::error("WsClient: async write error: {}", ec.message());
                self->connected_ = false;
                return;
            }
            self->do_flush_queue();
        });
}
