#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>

namespace net = boost::asio;
using udp = net::ip::udp;

class StunServer : public std::enable_shared_from_this<StunServer> {
public:
    StunServer(net::io_context& ioc, const std::string& listen_host, uint16_t port);
    ~StunServer();

    void start();
    void stop();

private:
    void do_receive();
    void handle_receive(boost::system::error_code ec, std::size_t bytes_recvd);
    void send_response(udp::endpoint remote, const std::string& node_id);

    net::io_context& ioc_;
    udp::socket socket_;
    udp::endpoint remote_;

    static constexpr std::size_t MAX_BUF = 4096;
    std::array<char, MAX_BUF> recv_buffer_;
};
