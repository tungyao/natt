#include <webview.h>
#include "natcore/CoreClient.h"

#include <memory>
#include <thread>
#include <chrono>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "embedded_html.h"

static std::string escapeJS(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\"': out += "\\\""; break;
            default:   out += c;
        }
    }
    return out;
}

int main(int argc, char* argv[]) {
    // Initialize spdlog so TestClient / CoreClient logs flow properly
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("gui", console);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);

    webview::webview w(false, nullptr);
    w.set_title("NATT - NAT Mesh Client");
    w.set_size(960, 720, WEBVIEW_HINT_NONE);

    w.set_html(embedded_html());

    CoreCallbacks cbs;

    cbs.on_log = [&w](const std::string& line) {
        w.eval("addLog('" + escapeJS(line) + "')");
    };

    cbs.on_state_change = [&w](const std::string& state) {
        w.eval("setState('" + escapeJS(state) + "')");
    };

    cbs.on_peer_online = [&w](const std::string& node_id,
                               const std::string& ip, uint16_t port) {
        std::string js = "addPeer('" + escapeJS(node_id) + "','" +
                         escapeJS(ip) + "'," + std::to_string(port) + ")";
        w.eval(js);
    };

    cbs.on_punch_success = [&w]() {
        w.eval("setPunchSuccess()");
    };

    cbs.on_error = [&w](const std::string& msg) {
        w.eval("setError('" + escapeJS(msg) + "')");
    };

    cbs.on_virtual_ip_assigned = [&w](const std::string& vip,
                                       const std::string& gw,
                                       const std::string& subnet) {
        std::string js = "setTunInfo('" + escapeJS(vip) + "','" +
                         escapeJS(gw) + "','" + escapeJS(subnet) + "')";
        w.eval(js);
    };

    auto client = std::make_shared<CoreClient>(std::move(cbs));

    w.bind("startClient", [client](const std::string& arg) -> std::string {
        try {
            auto json = nlohmann::json::parse(arg);
            CoreConfig config;
            config.node_id          = json.value("node_id", "test-node");
            config.network_id       = json.value("network_id", "home");
            config.control_url      = json.value("control_url", "127.0.0.1:8080");
            config.stun_addr        = json.value("stun_addr", "127.0.0.1:3478");
            config.udp_port         = json.value("udp_port", 0);
            config.connect_node_id  = json.value("connect_node_id", "");
            config.local_addr       = json.value("local_addr", "");
            config.relay_addr       = json.value("relay_addr", "");
            config.noise_private_key = json.value("noise_private_key", "");
            config.use_ssl          = json.value("use_ssl", false);
            config.cert_file        = json.value("cert_file", "");
            config.enable_tun       = json.value("enable_tun", false);
            config.tun_name         = json.value("tun_name", "nat%d");
            config.tun_mtu          = json.value("tun_mtu", 1300);
            if (!client->start(config)) {
                return R"({"ok":false,"error":"CoreClient failed to start"})";
            }
            return R"({"ok":true})";
        } catch (const std::exception& e) {
            return R"({"ok":false,"error":")" + escapeJS(e.what()) + R"("})";
        }
    });

    w.bind("stopClient", [client](const std::string&) -> std::string {
        client->stop();
        return R"({"ok":true})";
    });

    w.bind("getStatus", [client](const std::string&) -> std::string {
        nlohmann::json j;
        j["running"] = client->isRunning();
        j["punchSuccess"] = client->punchSuccess();
        j["mode"] = client->isRunning()
            ? (client->punchSuccess() ? "p2p" : "connecting")
            : "stopped";
        return j.dump();
    });

    w.run();
    return 0;
}
