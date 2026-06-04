#include "natcore/CoreClient.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <utility>

class CoreLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit CoreLogSink(std::function<void(const std::string&)> cb)
        : cb_(std::move(cb)) {
        this->set_pattern("%+");
    }
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (!cb_) return;
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        cb_(fmt::to_string(formatted));
    }
    void flush_() override {}
private:
    std::function<void(const std::string&)> cb_;
};

// ── CoreClient ─────────────────────────────────────────────────

CoreClient::CoreClient(CoreCallbacks cbs)
    : cbs_(std::move(cbs)) {}

CoreClient::~CoreClient() {
    stop();
}

bool CoreClient::start(const CoreConfig& config) {
    if (running_.load()) {
        if (cbs_.on_error) cbs_.on_error("CoreClient already running");
        return false;
    }

    config_ = config;
    stop_requested_ = false;
    stopped_ = false;

    // Install custom spdlog sink to forward logs to on_log callback
    if (cbs_.on_log) {
        auto old_logger = spdlog::default_logger();
        auto old_sinks = old_logger->sinks();
        std::vector<spdlog::sink_ptr> new_sinks;
        new_sinks.reserve(old_sinks.size() + 1);
        for (auto& s : old_sinks) {
            new_sinks.push_back(s);
        }
        new_sinks.push_back(std::make_shared<CoreLogSink>(cbs_.on_log));
        auto new_logger = std::make_shared<spdlog::logger>("core", new_sinks.begin(), new_sinks.end());
        spdlog::set_default_logger(new_logger);
    }

    running_ = true;
    worker_thread_ = std::thread(&CoreClient::workerFunc, this);
    monitor_thread_ = std::thread(&CoreClient::monitorFunc, this);
    return true;
}

void CoreClient::stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    stop_requested_ = true;
    running_ = false;

    if (client_) {
        client_->stop();
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    client_.reset();
}

bool CoreClient::isRunning() const {
    return running_.load();
}

ClientMode CoreClient::mode() const {
    return client_ ? client_->mode() : ClientMode::FAILED;
}

bool CoreClient::punchSuccess() const {
    return client_ && client_->punchSuccess();
}

void CoreClient::setConnectTarget(const std::string& node_id) {
    if (client_) {
        TestClient::Options opts;
        opts.connect_node_id = node_id;
        // Just store and use when restarting
        config_.connect_node_id = node_id;
    }
}

void CoreClient::workerFunc() {
    TestClient::Options opts;
    opts.node_id = config_.node_id;
    opts.network_id = config_.network_id;
    opts.control_url = config_.control_url;
    opts.stun_addr = config_.stun_addr;
    opts.udp_port = config_.udp_port;
    opts.connect_node_id = config_.connect_node_id;
    opts.local_addr = config_.local_addr;
    opts.relay_addr = config_.relay_addr;
    opts.noise_private_key = config_.noise_private_key;
    opts.use_ssl = config_.use_ssl;
    opts.cert_file = config_.cert_file;
    opts.enable_tun = config_.enable_tun;
    opts.tun_name = config_.tun_name;
    opts.tun_mtu = config_.tun_mtu;

    client_ = std::make_unique<TestClient>();

    try {
        client_->run(opts);
    } catch (const std::exception& e) {
        if (cbs_.on_error) cbs_.on_error(std::string("CoreClient error: ") + e.what());
    }

    running_ = false;
}

void CoreClient::monitorFunc() {
    ClientMode last_mode = ClientMode::PUNCHING;
    bool last_punch_success = false;
    std::string last_virtual_ip;
    std::string last_peer_id;

    while (!stop_requested_ && running_) {
        if (client_) {
            auto current_mode = client_->mode();
            if (current_mode != last_mode) {
                last_mode = current_mode;
                std::string state_str;
                switch (current_mode) {
                    case ClientMode::PUNCHING: state_str = "punching"; break;
                    case ClientMode::P2P:       state_str = "p2p"; break;
                    case ClientMode::RELAY:     state_str = "relay"; break;
                    case ClientMode::FAILED:    state_str = "failed"; break;
                }
                if (cbs_.on_state_change) cbs_.on_state_change(state_str);
            }

            bool success = client_->punchSuccess();
            if (success && !last_punch_success) {
                last_punch_success = true;
                if (cbs_.on_punch_success) cbs_.on_punch_success();
                auto pid = client_->peerNodeId();
                if (!pid.empty() && pid != last_peer_id) {
                    last_peer_id = pid;
                    if (cbs_.on_peer_online) cbs_.on_peer_online(pid, "", 0);
                }
            }

            if (current_mode == ClientMode::FAILED && last_mode == ClientMode::FAILED) {
                // already handled above
            }

            // Check TUN info
            std::string vip = client_->virtualIp();
            if (!vip.empty() && vip != last_virtual_ip) {
                last_virtual_ip = vip;
                if (cbs_.on_virtual_ip_assigned) {
                    cbs_.on_virtual_ip_assigned(vip, client_->gatewayIp(), client_->subnet());
                }
            }

            // Check if mode just changed to FAILED — fire on_error
            if (current_mode == ClientMode::FAILED && last_mode != ClientMode::FAILED) {
                if (cbs_.on_error) cbs_.on_error("NAT traversal failed");
            }
            last_mode = current_mode;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
