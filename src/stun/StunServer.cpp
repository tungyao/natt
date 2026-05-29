#include "stun/StunServer.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <utility>

StunServer::StunServer(net::io_context& ioc,
                       const std::string& listen_host,
                       uint16_t port)
    : ioc_(ioc)
    , socket_(ioc, udp::endpoint(net::ip::make_address(listen_host), port))
{
    spdlog::info("STUN Server listening on {}:{}", listen_host, port);
}

StunServer::~StunServer() {
    stop();
}

void StunServer::start() {
    do_receive();
}

void StunServer::stop() {
    boost::system::error_code ec;
    socket_.close(ec);
}

void StunServer::do_receive() {
    socket_.async_receive_from(
        net::buffer(recv_buffer_), remote_,
        [self = shared_from_this()](boost::system::error_code ec, std::size_t len) {
            self->handle_receive(ec, len);
        });
}

void StunServer::handle_receive(boost::system::error_code ec, std::size_t bytes_recvd) {
    if (ec) {
        if (ec != net::error::operation_aborted) {
            spdlog::error("STUN recv error: {}", ec.message());
            do_receive();
        }
        return;
    }

    std::string_view data(recv_buffer_.data(), bytes_recvd);
    const auto first_non_ws = std::find_if_not(data.begin(), data.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });

    if (first_non_ws == data.end()) {
        spdlog::debug("STUN ignoring empty datagram from {}:{}",
                      remote_.address().to_string(), remote_.port());
        do_receive();
        return;
    }

    if (*first_non_ws != '{') {
        spdlog::debug("STUN ignoring non-JSON datagram from {}:{}",
                      remote_.address().to_string(), remote_.port());
        do_receive();
        return;
    }

    try {
        auto json = nlohmann::json::parse(data);
        auto type = json["type"].get<std::string>();

        if (type == "stun_request") {
            auto node_id = json.value("node_id", std::string());
            send_response(remote_, node_id);
        } else {
            spdlog::debug("STUN ignoring unsupported type '{}' from {}:{}",
                          type, remote_.address().to_string(), remote_.port());
        }
    } catch (const std::exception& e) {
        spdlog::debug("STUN ignoring invalid JSON datagram from {}:{}: {}",
                      remote_.address().to_string(), remote_.port(), e.what());
    }

    do_receive();
}

void StunServer::send_response(udp::endpoint remote, const std::string& node_id) {
    auto public_ip = remote.address().to_string();
    auto public_port = remote.port();

    spdlog::info("STUN request from node_id={}, addr={}:{}",
                 node_id.empty() ? "?" : node_id, public_ip, public_port);

    nlohmann::json resp = {
        {"type", "stun_response"},
        {"public_ip", public_ip},
        {"public_port", public_port}
    };
    auto resp_str = resp.dump();

    socket_.async_send_to(
        net::buffer(resp_str), remote,
        [self = shared_from_this(), node_id, public_ip, public_port](
            boost::system::error_code ec, std::size_t len) {
            if (ec) {
                spdlog::error("STUN send error to {}: {}", node_id, ec.message());
            } else {
                spdlog::debug("STUN response sent to node_id={} ({}:{}), {} bytes",
                              node_id, public_ip, public_port, len);
            }
        });
}
