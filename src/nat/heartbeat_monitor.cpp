#include "nat/heartbeat_monitor.h"
#include <spdlog/spdlog.h>

HeartbeatMonitor::HeartbeatMonitor(net::io_context& ioc,
                                     int check_interval_sec,
                                     int timeout_sec)
    : ioc_(ioc)
    , timer_(ioc)
    , interval_sec_(check_interval_sec)
    , timeout_sec_(timeout_sec)
{
}

void HeartbeatMonitor::start() {
    running_ = true;
    do_check();
    spdlog::info("HeartbeatMonitor: started (interval={}s, timeout={}s)",
                 interval_sec_, timeout_sec_);
}

void HeartbeatMonitor::stop() {
    running_ = false;
    boost::system::error_code ec;
    timer_.cancel(ec);
    spdlog::info("HeartbeatMonitor: stopped");
}

void HeartbeatMonitor::setTimeoutCallback(TimeoutCallback cb) {
    timeout_cb_ = std::move(cb);
}

void HeartbeatMonitor::setOnlineCheck(OnlineCheck check) {
    online_check_ = std::move(check);
}

void HeartbeatMonitor::do_check() {
    if (!running_) return;

    // Get timed-out nodes from the check function
    if (online_check_) {
        auto timed_out = online_check_();
        if (!timed_out.empty()) {
            spdlog::warn("HeartbeatMonitor: {} node(s) timed out", timed_out.size());
            for (const auto& node_id : timed_out) {
                spdlog::warn("HeartbeatMonitor: node_id={} heartbeat timeout", node_id);
                if (timeout_cb_) {
                    timeout_cb_(node_id);
                }
            }
        }
    }

    // Schedule next check
    timer_.expires_after(std::chrono::seconds(interval_sec_));
    timer_.async_wait([self = shared_from_this()](boost::system::error_code ec) {
        if (ec) return; // cancelled
        self->do_check();
    });
}
