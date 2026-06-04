#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>

#include "client/TestClient.h"

struct CoreConfig {
    std::string node_id;
    std::string network_id;
    std::string control_url;
    std::string stun_addr;
    uint16_t udp_port = 0;
    std::string connect_node_id;
    std::string local_addr;
    std::string relay_addr;
    std::string noise_private_key;
    bool use_ssl = false;
    std::string cert_file;
    bool enable_tun = false;
    std::string tun_name = "nat%d";
    int tun_mtu = 1300;
};

struct CoreCallbacks {
    std::function<void(const std::string& line)> on_log;
    std::function<void(const std::string& state)> on_state_change;
    std::function<void(const std::string& node_id,
                       const std::string& ip, uint16_t port)> on_peer_online;
    std::function<void()> on_punch_success;
    std::function<void(const std::string& msg)> on_error;
    std::function<void(const std::string& virtual_ip,
                       const std::string& gateway_ip,
                       const std::string& subnet)> on_virtual_ip_assigned;
};

class CoreClient {
public:
    explicit CoreClient(CoreCallbacks cbs);
    ~CoreClient();

    bool start(const CoreConfig& config);
    void stop();
    bool isRunning() const;
    ClientMode mode() const;
    bool punchSuccess() const;
    void setConnectTarget(const std::string& node_id);

private:
    void workerFunc();
    void monitorFunc();

    CoreCallbacks cbs_;
    CoreConfig config_;
    std::unique_ptr<TestClient> client_;
    std::thread worker_thread_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> stopped_{false};
};
