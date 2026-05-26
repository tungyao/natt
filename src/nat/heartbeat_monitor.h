#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <functional>
#include <string>

namespace net = boost::asio;

class HeartbeatMonitor : public std::enable_shared_from_this<HeartbeatMonitor> {
public:
    using TimeoutCallback = std::function<void(const std::string& node_id)>;
    using OnlineCheck = std::function<std::vector<std::string>()>;

    HeartbeatMonitor(net::io_context& ioc,
                     int check_interval_sec,
                     int timeout_sec);

    // Start the periodic heartbeat check
    void start();

    // Stop the monitor
    void stop();

    // Set callback for when a node times out
    void setTimeoutCallback(TimeoutCallback cb);

    // Set function to get list of timed-out node IDs
    void setOnlineCheck(OnlineCheck check);

private:
    void do_check();

    net::io_context& ioc_;
    net::steady_timer timer_;
    int interval_sec_;
    int timeout_sec_;
    bool running_ = false;

    TimeoutCallback timeout_cb_;
    OnlineCheck online_check_;
};
