#include "http/http_server.h"
#include "ws/ws_session.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>
#include <algorithm>

namespace {

const char* kAdminHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>NATMesh Admin</title>
  <style>
    :root { color-scheme: dark; --bg:#0f172a; --panel:#111827; --muted:#94a3b8; --line:#1f2937; --text:#e5e7eb; --accent:#22c55e; --warn:#f59e0b; }
    * { box-sizing:border-box; }
    body { margin:0; font:14px/1.4 system-ui,sans-serif; background:var(--bg); color:var(--text); }
    .app { max-width:1280px; margin:0 auto; padding:24px; }
    .bar, .grid, .list, .detail, .login { display:grid; gap:16px; }
    .bar { grid-template-columns:repeat(4,minmax(0,1fr)); margin-bottom:16px; }
    .grid { grid-template-columns:1.35fr 1fr; align-items:start; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:16px; }
    .panel h2, .panel h3 { margin:0 0 12px; font-size:14px; }
    .metric { min-height:92px; }
    .metric .v { font-size:28px; font-weight:700; margin-top:8px; }
    .muted { color:var(--muted); }
    .row { display:flex; gap:12px; align-items:center; justify-content:space-between; }
    .status { display:inline-flex; align-items:center; gap:6px; }
    .dot { width:8px; height:8px; border-radius:50%; background:var(--muted); }
    .online .dot { background:var(--accent); }
    .offline .dot { background:var(--warn); }
    table { width:100%; border-collapse:collapse; }
    th, td { text-align:left; padding:10px 8px; border-top:1px solid var(--line); vertical-align:top; }
    th { color:var(--muted); font-weight:600; font-size:12px; }
    button, input { font:inherit; }
    button { border:1px solid var(--line); background:#0b1220; color:var(--text); border-radius:6px; padding:8px 12px; cursor:pointer; }
    button.primary { background:#1d4ed8; border-color:#1d4ed8; }
    input { width:100%; padding:10px 12px; border-radius:6px; border:1px solid var(--line); background:#0b1220; color:var(--text); }
    .login { max-width:360px; margin:72px auto; }
    .hidden { display:none; }
    .kv { display:grid; grid-template-columns:120px 1fr; gap:8px 12px; }
    .chips { display:flex; flex-wrap:wrap; gap:8px; }
    .chip { border:1px solid var(--line); border-radius:999px; padding:4px 10px; color:var(--muted); }
    .section { margin-top:12px; }
    @media (max-width: 960px) { .bar, .grid { grid-template-columns:1fr; } }
  </style>
</head>
<body>
  <div class="app">
    <div id="login" class="login panel">
      <h1 style="margin:0;font-size:20px;">NATMesh Admin</h1>
      <div class="muted">Sign in with an existing API user.</div>
      <input id="username" placeholder="Username">
      <input id="password" type="password" placeholder="Password">
      <button id="loginBtn" class="primary">Sign In</button>
      <div id="loginError" class="muted"></div>
    </div>

    <div id="dashboard" class="hidden">
      <div class="row" style="margin-bottom:16px;">
        <div>
          <h1 style="margin:0;font-size:20px;">NATMesh Admin</h1>
          <div id="summaryText" class="muted"></div>
        </div>
        <div class="row">
          <button id="refreshBtn">Refresh</button>
          <button id="logoutBtn">Logout</button>
        </div>
      </div>

      <div class="bar">
        <div class="panel metric"><div class="muted">Devices</div><div id="mDevices" class="v">0</div></div>
        <div class="panel metric"><div class="muted">Online Devices</div><div id="mOnline" class="v">0</div></div>
        <div class="panel metric"><div class="muted">Networks</div><div id="mNetworks" class="v">0</div></div>
        <div class="panel metric"><div class="muted">Runtime Peers</div><div id="mPeers" class="v">0</div></div>
      </div>

      <div class="grid">
        <div class="list">
          <div class="panel">
            <h2>Devices</h2>
            <table>
              <thead><tr><th>Node</th><th>Status</th><th>Transport</th><th>Public</th><th>Virtual</th><th>Networks</th></tr></thead>
              <tbody id="deviceRows"></tbody>
            </table>
          </div>
          <div class="panel">
            <h2>Networks</h2>
            <table>
              <thead><tr><th>Name</th><th>Subnet</th><th>Devices</th></tr></thead>
              <tbody id="networkRows"></tbody>
            </table>
          </div>
        </div>
        <div class="detail">
          <div class="panel">
            <h2>Node Detail</h2>
            <div id="detailEmpty" class="muted">Select a device to inspect runtime and membership state.</div>
            <div id="detailBody" class="hidden">
              <div class="kv" id="detailKv"></div>
              <div class="section">
                <h3>Networks</h3>
                <div id="detailNetworks" class="chips"></div>
              </div>
              <div class="section">
                <h3>LAN Addresses</h3>
                <div id="detailLan" class="chips"></div>
              </div>
            </div>
          </div>
          <div class="panel">
            <h2>Online Peers</h2>
            <table>
              <thead><tr><th>Node</th><th>Public</th><th>Virtual</th></tr></thead>
              <tbody id="peerRows"></tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
  </div>
  <script>
    const tokenKey = "natmesh_admin_token";
    const els = {
      login: document.getElementById("login"),
      dashboard: document.getElementById("dashboard"),
      loginBtn: document.getElementById("loginBtn"),
      logoutBtn: document.getElementById("logoutBtn"),
      refreshBtn: document.getElementById("refreshBtn"),
      username: document.getElementById("username"),
      password: document.getElementById("password"),
      loginError: document.getElementById("loginError"),
      summaryText: document.getElementById("summaryText"),
      mDevices: document.getElementById("mDevices"),
      mOnline: document.getElementById("mOnline"),
      mNetworks: document.getElementById("mNetworks"),
      mPeers: document.getElementById("mPeers"),
      deviceRows: document.getElementById("deviceRows"),
      networkRows: document.getElementById("networkRows"),
      peerRows: document.getElementById("peerRows"),
      detailEmpty: document.getElementById("detailEmpty"),
      detailBody: document.getElementById("detailBody"),
      detailKv: document.getElementById("detailKv"),
      detailNetworks: document.getElementById("detailNetworks"),
      detailLan: document.getElementById("detailLan")
    };
    let authToken = localStorage.getItem(tokenKey) || "";
    let currentOverview = null;

    function setAuthed(authed) {
      els.login.classList.toggle("hidden", authed);
      els.dashboard.classList.toggle("hidden", !authed);
    }

    function fmtPublic(ip, port) { return ip ? `${ip}:${port || 0}` : "-"; }
    function chip(text) { return `<span class="chip">${text}</span>`; }

    async function api(path, options = {}) {
      const headers = new Headers(options.headers || {});
      if (authToken) headers.set("Authorization", `Bearer ${authToken}`);
      if (options.body && !headers.has("Content-Type")) headers.set("Content-Type", "application/json");
      const resp = await fetch(path, { ...options, headers });
      if (!resp.ok) {
        const text = await resp.text();
        throw new Error(text || `HTTP ${resp.status}`);
      }
      return resp.json();
    }

    async function login() {
      els.loginError.textContent = "";
      const body = JSON.stringify({ username: els.username.value, password: els.password.value });
      try {
        const resp = await api("/api/v1/auth/login", { method: "POST", body });
        authToken = resp.token;
        localStorage.setItem(tokenKey, authToken);
        setAuthed(true);
        await loadOverview();
      } catch (err) {
        els.loginError.textContent = "Sign in failed";
      }
    }

    async function loadOverview() {
      try {
        const data = await api("/api/v1/admin/overview");
        currentOverview = data;
        renderOverview(data);
      } catch (err) {
        if (String(err.message).includes("401")) {
          logout();
          return;
        }
        els.summaryText.textContent = "Failed to load overview";
      }
    }

    function renderOverview(data) {
      const devices = data.devices || [];
      const networks = data.networks || [];
      const peers = data.online_peers || [];
      const memberships = data.device_networks || {};

      els.summaryText.textContent = `User ${data.user.user_id} · refreshed ${new Date().toLocaleTimeString()}`;
      els.mDevices.textContent = String(devices.length);
      els.mOnline.textContent = String(devices.filter(d => d.connection_state === "online").length);
      els.mNetworks.textContent = String(networks.length);
      els.mPeers.textContent = String(peers.length);

      els.deviceRows.innerHTML = devices.map(d => {
        const nets = (memberships[d.node_id] || []).map(n => n.name).join(", ") || "-";
        const state = d.connection_state || (d.online ? "online" : "offline");
        return `<tr data-node="${d.node_id}">
          <td><button data-node="${d.node_id}">${d.device_name || d.node_id}</button><div class="muted">${d.node_id}</div></td>
          <td><span class="status ${state === "online" ? "online" : "offline"}"><span class="dot"></span>${state}</span></td>
          <td>${d.transport_mode || "unknown"}</td>
          <td>${fmtPublic(d.public_ip, d.public_port)}</td>
          <td>${d.virtual_ip || "-"}</td>
          <td>${nets}</td>
        </tr>`;
      }).join("");

      els.networkRows.innerHTML = networks.map(n => {
        const count = (data.network_devices[n.id] || []).length;
        return `<tr><td>${n.name}</td><td>${n.subnet || "-"}</td><td>${count}</td></tr>`;
      }).join("");

      els.peerRows.innerHTML = peers.map(p => `
        <tr><td>${p.node_id}</td><td>${fmtPublic(p.public_ip, p.public_port)}</td><td>${p.virtual_ip || "-"}</td></tr>
      `).join("");

      els.deviceRows.querySelectorAll("button[data-node]").forEach(btn => {
        btn.addEventListener("click", () => loadDeviceDetail(btn.dataset.node));
      });
    }

    async function loadDeviceDetail(nodeId) {
      try {
        const data = await api(`/api/v1/admin/devices/${encodeURIComponent(nodeId)}`);
        renderDeviceDetail(data);
      } catch (err) {
        els.detailEmpty.textContent = "Failed to load node detail";
        els.detailEmpty.classList.remove("hidden");
        els.detailBody.classList.add("hidden");
      }
    }

    function renderDeviceDetail(data) {
      els.detailEmpty.classList.add("hidden");
      els.detailBody.classList.remove("hidden");
      const d = data.device;
      const state = d.connection_state || (d.online ? "online" : "offline");
      const rows = [
        ["Node ID", d.node_id],
        ["Name", d.device_name || "-"],
        ["Owner user", d.owner_user_id || "-"],
        ["Status", state],
        ["Transport", d.transport_mode || "unknown"],
        ["Public key", d.public_key || "-"],
        ["Public", fmtPublic(d.public_ip, d.public_port)],
        ["Virtual", d.virtual_ip || "-"],
        ["Gateway", data.runtime?.gateway_ip || "-"],
        ["Subnet", data.runtime?.subnet || "-"],
        ["Last heartbeat", d.last_heartbeat || "-"],
        ["Transport RTT", data.runtime?.last_transport_rtt_ms ? `${data.runtime.last_transport_rtt_ms} ms` : "-"],
        ["Reconnects", data.runtime?.reconnect_count ?? 0],
        ["Disconnect reason", data.runtime?.last_disconnect_reason || "-"],
        ["WS session", data.runtime?.ws_online ? "online" : "offline"],
        ["Registry state", data.runtime?.registry_online ? "online" : "offline"],
        ["Created", d.created_at || "-"],
        ["Updated", d.updated_at || "-"]
      ];
      els.detailKv.innerHTML = rows.map(([k, v]) => `<div class="muted">${k}</div><div>${v}</div>`).join("");
      els.detailNetworks.innerHTML = (data.networks || []).map(n => chip(`${n.name} (${n.subnet || "-"})`)).join("") || chip("No networks");
      const lan = (d.lan_ips || []).length ? d.lan_ips : (data.runtime?.runtime_local_addrs || []);
      els.detailLan.innerHTML = lan.map(chip).join("") || chip("No LAN addresses");
    }

    function logout() {
      authToken = "";
      localStorage.removeItem(tokenKey);
      setAuthed(false);
    }

    els.loginBtn.addEventListener("click", login);
    els.logoutBtn.addEventListener("click", logout);
    els.refreshBtn.addEventListener("click", loadOverview);
    [els.username, els.password].forEach(el => el.addEventListener("keydown", ev => {
      if (ev.key === "Enter") login();
    }));

    if (authToken) {
      setAuthed(true);
      loadOverview();
    } else {
      setAuthed(false);
    }
  </script>
</body>
</html>)HTML";

std::string hex_encode(const std::vector<uint8_t>& data) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (auto b : data) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0f]);
    }
    return out;
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        return 0;
    };

    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

std::string ipv4_dst(const std::vector<uint8_t>& packet) {
    if (packet.size() < 20 || (packet[0] >> 4) != 4) return {};
    return std::to_string(packet[16]) + "." +
           std::to_string(packet[17]) + "." +
           std::to_string(packet[18]) + "." +
           std::to_string(packet[19]);
}

} // namespace

HttpServer::HttpServer(net::io_context& ioc,
                       const Config& config,
                       UserService& user_svc,
                       DeviceService& device_svc,
                       NetworkService& network_svc,
                       IpAllocator& ipam,
                       auth::JwtManager& jwt,
                       PeerManager& peer_mgr)
    : ioc_(ioc)
    , acceptor_(ioc, tcp::endpoint(net::ip::make_address(config.server.host), config.server.port))
    , config_(config)
    , user_svc_(user_svc)
    , device_svc_(device_svc)
    , network_svc_(network_svc)
    , ipam_(ipam)
    , jwt_(jwt)
    , peer_mgr_(peer_mgr)
{
    spdlog::info("HTTP server listening on {}:{}", config.server.host, config.server.port);

    // ── Initialize HeartbeatMonitor ──
    heartbeat_monitor_ = std::make_shared<HeartbeatMonitor>(
        ioc,
        config.websocket.heartbeat_interval_sec,
        config.websocket.heartbeat_timeout_sec
    );

    // Set up heartbeat timeout callback
    heartbeat_monitor_->setTimeoutCallback(
        [this](const std::string& node_id) {
            spdlog::warn("Heartbeat timeout, removing node_id={}", node_id);
            auto node = node_registry_.findNode(node_id);
            node_registry_.removeNode(node_id);
            session_mgr_.removeSession(node_id);
            device_svc_.set_offline(node_id);

            // Notify network peers about the offline node
            if (node.has_value()) {
                auto peers = node_registry_.listNetworkNodes(node->network_id);
                nlohmann::json peer_list = {{"type", "peer_list"}, {"peers", nlohmann::json::array()}};
                for (const auto& p : peers) {
                    if (p.node_id != node_id) {
                        peer_list["peers"].push_back(p.to_peer_json());
                    }
                }
                session_mgr_.broadcastIf(peer_list, [&](const std::string& id) {
                    return id != node_id && node_registry_.isOnline(id);
                });
            }
        }
    );

    // Set up online check for heartbeat monitor
    heartbeat_monitor_->setOnlineCheck(
        [this]() -> std::vector<std::string> {
            return node_registry_.findTimedOutNodes(config_.websocket.heartbeat_timeout_sec);
        }
    );

    // ── Register MessageDispatchers ──

    // update_addr: update node's public/local addresses
    msg_dispatcher_.registerHandler("update_addr",
        [this](WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
            auto public_ip = msg.value("public_ip", std::string());
            auto public_port = msg.value("public_port", 0);
            std::vector<std::string> local_addrs;
            if (msg.contains("local_addrs")) {
                local_addrs = msg["local_addrs"].get<std::vector<std::string>>();
            }

            node_registry_.updateAddress(node_id, public_ip, public_port, local_addrs);
            device_svc_.update_connection_info(node_id, public_ip, public_port, local_addrs);

            spdlog::info("update_addr: node_id={}, ip={}:{}, {} local addrs",
                         node_id, public_ip, public_port, local_addrs.size());

            auto node = node_registry_.findNode(node_id);
            if (node.has_value() && !node->virtual_ip.empty()) {
                nlohmann::json vip_msg = {
                    {"type", "virtual_ip_assigned"},
                    {"virtual_ip", node->virtual_ip},
                    {"gateway_ip", node->gateway_ip},
                    {"subnet", node->subnet}
                };
                session->send_json(vip_msg.dump());
                spdlog::info("Sent virtual IP {} to node {} (gateway {}, subnet {})",
                             node->virtual_ip, node_id, node->gateway_ip, node->subnet);
            }

            // Auto-connect: if this node has a valid public address, initiate
            // punch_start to all online peers in the same network that also have
            // valid addresses. Use node_id ordering to avoid duplicate connections:
            // - If this node has the smaller node_id → this node initiates (outgoing)
            // - If peer has the smaller node_id → peer should have initiated when it
            //   joined, but this node wasn't online yet. So peer initiates now.
            if (node.has_value() && !public_ip.empty() && public_port != 0) {
                auto peers = node_registry_.listNetworkNodes(node->network_id);
                for (const auto& peer : peers) {
                    if (peer.node_id == node_id) continue;
                    if (peer.public_ip.empty() || peer.public_port == 0) continue;
                    if (node_id < peer.node_id) {
                        // This node has smaller id → this node initiates
                        signal_punch_start(*node, peer);
                    } else {
                        // Peer has smaller id → peer should have initiated but
                        // couldn't (this node wasn't online). Peer initiates now.
                        signal_punch_start(peer, *node);
                    }
                }
            }
        }
    );

    // heartbeat: update last seen
    msg_dispatcher_.registerHandler("heartbeat",
        [this](WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
            node_registry_.updateHeartbeat(node_id);
            device_svc_.update_heartbeat(node_id);
            session->send_json(nlohmann::json{{"type", "pong"}}.dump());
        }
    );

    msg_dispatcher_.registerHandler("transport_status",
        [this](WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
            auto mode = msg.value("mode", std::string("unknown"));
            auto rtt_ms = msg.value("rtt_ms", int64_t(0));
            node_registry_.updateTransportState(node_id, mode, rtt_ms);
        }
    );

    // connect_peer: NAT hole punching signaling
    msg_dispatcher_.registerHandler("connect_peer",
        [this](WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
            auto target_node_id = msg["target_node_id"].get<std::string>();

            // Lookup target node info
            auto target_node = node_registry_.findNode(target_node_id);
            if (!target_node.has_value()) {
                nlohmann::json err = {
                    {"type", "error"},
                    {"message", "Target device is not online"}
                };
                session->send_json(err.dump());
                return;
            }

            // Lookup requester node info
            auto requester_node = node_registry_.findNode(node_id);
            if (!requester_node.has_value()) {
                nlohmann::json err = {
                    {"type", "error"},
                    {"message", "Your device info not found"}
                };
                session->send_json(err.dump());
                return;
            }

            signal_punch_start(*requester_node, *target_node);
            spdlog::info("NAT signaling: connect_peer {} → {}", node_id, target_node_id);
        }
    );
}

HttpServer::~HttpServer() {
    shutting_down_ = true;
}

NodeInfo HttpServer::assign_virtual_ip(const std::string& node_id,
                                       const std::string& network_id,
                                       const std::string& public_key) {
    auto network = network_id.empty() ? std::string("default") : network_id;
    if (ipam_.getSubnet(network).empty()) {
        auto it = config_.ipam.pools.find(network);
        std::string cidr;
        if (it != config_.ipam.pools.end()) {
            cidr = it->second;
        } else if (auto def = config_.ipam.pools.find("default"); def != config_.ipam.pools.end()) {
            cidr = def->second;
        } else {
            cidr = "10.0.0.0/24";
        }
        ipam_.addPool(network, cidr);
    }

    auto existing = node_registry_.findNode(node_id);
    auto virtual_ip = existing && !existing->virtual_ip.empty()
        ? existing->virtual_ip
        : ipam_.allocate(network);

    NodeInfo info;
    info.node_id = node_id;
    info.network_id = network;
    info.public_key = public_key;
    info.virtual_ip = virtual_ip;
    info.gateway_ip = ipam_.gatewayIp(network);
    info.subnet = ipam_.getSubnet(network);
    info.online = true;
    info.last_seen = std::chrono::system_clock::now();
    return info;
}

void HttpServer::ensure_node_auth_persisted(const NodeInfo& info) {
    if (!config_.admin.enabled) {
        return;
    }

    auto admin_user = user_svc_.find_user_by_username(config_.admin.username);
    if (!admin_user.has_value()) {
        spdlog::warn("Node auth persistence skipped: bootstrap admin user '{}' not found",
                     config_.admin.username);
        return;
    }

    auto err = device_svc_.ensure_device_for_user(
        info.node_id,
        admin_user->id,
        info.node_id,
        info.public_key
    );
    if (!err.empty()) {
        spdlog::warn("Failed to persist node-auth device {}: {}", info.node_id, err);
        return;
    }

    device_svc_.update_virtual_ip(info.node_id, info.virtual_ip);
    device_svc_.update_connection_info(info.node_id, info.public_ip, info.public_port, info.local_addrs);
    device_svc_.update_heartbeat(info.node_id);

    auto [network_err, network_id] = network_svc_.ensure_network(info.network_id, admin_user->id);
    if (!network_err.empty()) {
        spdlog::warn("Failed to persist node-auth network {}: {}", info.network_id, network_err);
        return;
    }

    auto join_err = network_svc_.join_network(network_id, info.node_id);
    if (!join_err.empty() && join_err != "Device already in network") {
        spdlog::warn("Failed to attach node {} to network {}: {}", info.node_id, info.network_id, join_err);
    }
}

bool HttpServer::ensure_tun_gateway(const std::string& network_id) {
    auto subnet = ipam_.getSubnet(network_id);
    auto gateway_ip = ipam_.gatewayIp(network_id);
    if (subnet.empty() || gateway_ip.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(tun_mutex_);
        if (tun_by_network_.find(network_id) != tun_by_network_.end()) {
            return true;
        }
    }

    auto slash = subnet.find('/');
    int prefix = 24;
    if (slash != std::string::npos) {
        prefix = std::stoi(subnet.substr(slash + 1));
    }

    auto state = std::make_shared<TunState>();
    state->tun = TunInterface::create(ioc_);
    if (!state->tun->open("natgw%d", gateway_ip, prefix, 1300)) {
        spdlog::warn("TUN gateway: failed to create gateway for network {} at {}/{}",
                     network_id, gateway_ip, prefix);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(tun_mutex_);
        tun_by_network_[network_id] = state;
    }

    spdlog::info("TUN gateway: network {} server gateway {} on {}",
                 network_id, gateway_ip, state->tun->interfaceName());
    do_tun_read(network_id);
    return true;
}

void HttpServer::do_tun_read(const std::string& network_id) {
    std::shared_ptr<TunState> state;
    {
        std::lock_guard<std::mutex> lock(tun_mutex_);
        auto it = tun_by_network_.find(network_id);
        if (it == tun_by_network_.end()) return;
        state = it->second;
    }

    state->tun->asyncRead(net::buffer(state->buffer),
        [this, network_id, state](boost::system::error_code ec, std::size_t len) {
            if (ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    // TUN gateway closed (e.g. shutdown), don't restart
                    return;
                }
                spdlog::warn("TUN gateway read error on {}: {}, retrying...",
                             network_id, ec.message());
                // Transient error — re-arm after a short delay
                auto timer = std::make_shared<net::steady_timer>(ioc_);
                timer->expires_after(std::chrono::milliseconds(100));
                timer->async_wait(
                    [this, network_id, timer](boost::system::error_code) {
                        do_tun_read(network_id);
                    });
                return;
            }

            std::vector<uint8_t> packet(state->buffer.data(), state->buffer.data() + len);
            forward_tun_packet(network_id, packet);
            do_tun_read(network_id);
        });
}

void HttpServer::forward_tun_packet(const std::string& network_id,
                                    const std::vector<uint8_t>& packet) {
    auto dst = ipv4_dst(packet);
    if (dst.empty()) return;

    auto peers = node_registry_.listNetworkNodes(network_id);
    auto it = std::find_if(peers.begin(), peers.end(), [&](const NodeInfo& node) {
        return node.virtual_ip == dst;
    });
    if (it == peers.end()) {
        spdlog::debug("TUN gateway: no online node for dst {}", dst);
        return;
    }

    nlohmann::json msg = {
        {"type", "tun_packet"},
        {"payload", hex_encode(packet)}
    };
    session_mgr_.sendTo(it->node_id, msg);
}

void HttpServer::handle_tun_packet(const std::string& node_id, const nlohmann::json& msg) {
    auto node = node_registry_.findNode(node_id);
    if (!node.has_value()) return;
    node_registry_.updateTransportState(node_id, "server");

    auto payload = msg.value("payload", std::string());
    auto packet = hex_decode(payload);
    if (packet.empty()) return;

    auto dst = ipv4_dst(packet);
    if (!dst.empty() && dst != node->gateway_ip) {
        auto peers = node_registry_.listNetworkNodes(node->network_id);
        auto it = std::find_if(peers.begin(), peers.end(), [&](const NodeInfo& peer) {
            return peer.virtual_ip == dst;
        });
        if (it != peers.end()) {
            node_registry_.updateTransportState(it->node_id, "server");
            nlohmann::json forward = {
                {"type", "tun_packet"},
                {"payload", payload}
            };
            session_mgr_.sendTo(it->node_id, forward);
            return;
        }
    }

    std::shared_ptr<TunState> state;
    {
        std::lock_guard<std::mutex> lock(tun_mutex_);
        auto it = tun_by_network_.find(node->network_id);
        if (it == tun_by_network_.end()) return;
        state = it->second;
    }

    state->tun->write(packet);
}

void HttpServer::signal_punch_start(const NodeInfo& requester,
                                    const NodeInfo& target) {
    // Send punch_start to requester (outgoing direction)
    auto req_session = session_mgr_.getSession(requester.node_id);
    if (req_session) {
        nlohmann::json out_msg = {
            {"type", "punch_start"},
            {"direction", "outgoing"},
            {"peer_node_id", target.node_id},
            {"peer_public_ip", target.public_ip},
            {"peer_public_port", target.public_port},
            {"peer_local_addrs", nlohmann::json::array()},
            {"peer_public_key", target.public_key}
        };
        for (const auto& addr : target.local_addrs) {
            out_msg["peer_local_addrs"].push_back(addr);
        }
        req_session->send_json(out_msg.dump());
    }

    // Send punch_start to target (incoming direction)
    auto tgt_session = session_mgr_.getSession(target.node_id);
    if (tgt_session) {
        nlohmann::json in_msg = {
            {"type", "punch_start"},
            {"direction", "incoming"},
            {"peer_node_id", requester.node_id},
            {"peer_public_ip", requester.public_ip},
            {"peer_public_port", requester.public_port},
            {"peer_local_addrs", nlohmann::json::array()},
            {"peer_public_key", requester.public_key}
        };
        for (const auto& addr : requester.local_addrs) {
            in_msg["peer_local_addrs"].push_back(addr);
        }
        tgt_session->send_json(in_msg.dump());
    }

    spdlog::info("Auto punch_start: {} → {}", requester.node_id, target.node_id);
}

void HttpServer::start() {
    do_accept();
    heartbeat_monitor_->start();
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        [self = shared_from_this()](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                // Handle the connection synchronously in a thread
                std::thread([self, sock = std::move(socket)]() mutable {
                    self->handle_connection(std::move(sock));
                }).detach();
            } else {
                spdlog::error("Accept error: {}", ec.message());
            }
            self->do_accept();
        });
}

void HttpServer::handle_connection(tcp::socket socket) {
    beast::flat_buffer buf;
    http::request_parser<http::string_body> parser;
    parser.body_limit(std::numeric_limits<uint64_t>::max());

    // Read the request
    beast::error_code ec;
    http::read(socket, buf, parser, ec);
    if (ec) {
        spdlog::error("HTTP read error: {}", ec.message());
        return;
    }

    auto& req = parser.get();

    // Check for WebSocket upgrade
    if (beast::websocket::is_upgrade(req)) {
        handle_websocket_upgrade(std::move(socket), std::move(req));
        return;
    }

    // Handle REST
    auto res = handle_rest_request(req);

    // Write the response synchronously
    http::write(socket, res, ec);
    if (ec) {
        spdlog::error("HTTP write error: {}", ec.message());
    }
}

// ── REST Router ────────────────────────────────────────────

http::response<http::string_body> HttpServer::handle_rest_request(
    const http::request<http::string_body>& req)
{
    auto method = std::string(req.method_string());
    auto path = std::string(req.target());

    spdlog::debug("HTTP {} {}", method, path);

    try {
        // ── Public endpoints ──
        if (path == "/admin" && method == "GET") {
            return make_html_response(http::status::ok, kAdminHtml);
        }
        if (path == "/api/v1/auth/register" && method == "POST") {
            return handle_register(req);
        }
        if (path == "/api/v1/auth/login" && method == "POST") {
            return handle_login(req);
        }
        if (path == "/health" && method == "GET") {
            return handle_health(req);
        }

        // ── Authenticated endpoints ──
        auto auth = authenticate(req);
        if (!auth.has_value()) {
            return make_error_response(http::status::unauthorized, "Invalid or missing token");
        }
        int64_t user_id = auth->user_id;

        // Device endpoints
        if (path == "/api/v1/devices" && method == "POST") {
            return handle_create_device(req, user_id);
        }
        if (path == "/api/v1/devices" && method == "GET") {
            return handle_list_devices(req, user_id);
        }
        if (path.rfind("/api/v1/devices/", 0) == 0 && method == "GET") {
            auto node_id = path.substr(16);
            return handle_get_device(req, node_id, user_id);
        }
        if (path.rfind("/api/v1/devices/", 0) == 0 && method == "DELETE") {
            auto node_id = path.substr(16);
            return handle_delete_device(req, node_id, user_id);
        }

        // Network endpoints
        if (path == "/api/v1/networks" && method == "POST") {
            return handle_create_network(req, user_id);
        }
        if (path == "/api/v1/networks" && method == "GET") {
            return handle_list_networks(req, user_id);
        }

        // /api/v1/networks/:id/join
        if (path.size() > 5 && path.rfind("/join") == path.size() - 5) {
            auto net_path = path.substr(0, path.size() - 5);
            auto id_str = net_path.substr(16); // after /api/v1/networks/
            int64_t net_id = std::stoll(id_str);
            if (method == "POST") return handle_join_network(req, net_id);
        }

        // /api/v1/networks/:id/devices/:node_id
        if (path.rfind("/api/v1/networks/", 0) == 0 && path.find("/devices") != std::string::npos) {
            auto prefix = std::string("/api/v1/networks/");
            auto rest = path.substr(prefix.size());
            auto slash = rest.find('/');
            if (slash != std::string::npos) {
                auto id_str = rest.substr(0, slash);
                auto action = rest.substr(slash + 1);
                int64_t net_id = std::stoll(id_str);

                if (action == "devices" && method == "GET") {
                    return handle_list_network_devices(req, net_id);
                }
                if (action.rfind("devices/", 0) == 0 && method == "DELETE") {
                    auto node_id = action.substr(8);
                    return handle_leave_network(req, net_id, node_id);
                }
            }
        }

        // WS token
        if (path == "/api/v1/ws/token" && method == "GET") {
            return handle_ws_token(req, user_id);
        }

        // ── NAT status endpoints ──
        if (path == "/api/v1/nat/peers" && method == "GET") {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& nid : session_mgr_.listSessions()) {
                auto node = node_registry_.findNode(nid);
                if (node.has_value()) {
                    arr.push_back(node->to_peer_json());
                }
            }
            return make_json_response(http::status::ok, arr);
        }
        if (path == "/api/v1/admin/overview" && method == "GET") {
            return handle_admin_overview(req, user_id);
        }
        if (path.rfind("/api/v1/admin/devices/", 0) == 0 && method == "GET") {
            return handle_admin_device_detail(req, path.substr(std::string("/api/v1/admin/devices/").size()), user_id);
        }

    } catch (const std::exception& e) {
        spdlog::error("Request handler error: {}", e.what());
        return make_error_response(http::status::bad_request, e.what());
    }

    return make_error_response(http::status::not_found, "Not found");
}

// ── Auth handlers ──────────────────────────────────────────

http::response<http::string_body> HttpServer::handle_register(
    const http::request<http::string_body>& req)
{
    auto body = nlohmann::json::parse(req.body());
    auto username = body["username"].get<std::string>();
    auto password = body["password"].get<std::string>();

    auto err = user_svc_.register_user(username, password);
    if (!err.empty()) {
        return make_error_response(http::status::bad_request, err);
    }

    return make_json_response(http::status::created, {{"message", "User registered successfully"}});
}

http::response<http::string_body> HttpServer::handle_login(
    const http::request<http::string_body>& req)
{
    auto body = nlohmann::json::parse(req.body());
    auto username = body["username"].get<std::string>();
    auto password = body["password"].get<std::string>();

    auto result = user_svc_.login(username, password);
    if (!result.has_value()) {
        return make_error_response(http::status::unauthorized, "Invalid username or password");
    }

    return make_json_response(http::status::ok, {
        {"token", result->token},
        {"user_id", result->user_id},
        {"username", result->username}
    });
}

// ── Device handlers ────────────────────────────────────────

http::response<http::string_body> HttpServer::handle_create_device(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto body = nlohmann::json::parse(req.body());
    auto device_name = body["device_name"].get<std::string>();
    auto public_key = body.value("public_key", std::string(""));

    auto [err, node_id] = device_svc_.create_device(user_id, device_name, public_key);
    if (!err.empty()) {
        return make_error_response(http::status::bad_request, err);
    }

    return make_json_response(http::status::created, {
        {"node_id", node_id},
        {"message", "Device created successfully"}
    });
}

http::response<http::string_body> HttpServer::handle_list_devices(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto devices = device_svc_.list_user_devices(user_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : devices) {
        arr.push_back(d.to_json());
    }
    return make_json_response(http::status::ok, arr);
}

http::response<http::string_body> HttpServer::handle_get_device(
    const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id)
{
    auto device = device_svc_.get_device(node_id);
    if (!device.has_value()) {
        return make_error_response(http::status::not_found, "Device not found");
    }
    if (device->user_id != user_id) {
        return make_error_response(http::status::forbidden, "Not your device");
    }
    return make_json_response(http::status::ok, device->to_json());
}

http::response<http::string_body> HttpServer::handle_delete_device(
    const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id)
{
    if (!device_svc_.delete_device(node_id, user_id)) {
        return make_error_response(http::status::not_found, "Device not found or not owned by you");
    }
    return make_json_response(http::status::ok, {{"message", "Device deleted"}});
}

// ── Network handlers ────────────────────────────────────────

http::response<http::string_body> HttpServer::handle_create_network(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto body = nlohmann::json::parse(req.body());
    auto name = body["name"].get<std::string>();

    auto [err, net_id] = network_svc_.create_network(name, user_id);
    if (!err.empty()) {
        return make_error_response(http::status::bad_request, err);
    }

    return make_json_response(http::status::created, {
        {"id", net_id},
        {"message", "Network created successfully"}
    });
}

http::response<http::string_body> HttpServer::handle_list_networks(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto networks = network_svc_.list_user_networks(user_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : networks) {
        arr.push_back(n.to_json());
    }
    return make_json_response(http::status::ok, arr);
}

http::response<http::string_body> HttpServer::handle_join_network(
    const http::request<http::string_body>& req, int64_t network_id)
{
    auto body = nlohmann::json::parse(req.body());
    auto node_id = body["node_id"].get<std::string>();

    auto err = network_svc_.join_network(network_id, node_id);
    if (!err.empty()) {
        return make_error_response(http::status::bad_request, err);
    }

    return make_json_response(http::status::ok, {{"message", "Device joined network"}});
}

http::response<http::string_body> HttpServer::handle_leave_network(
    const http::request<http::string_body>& req, int64_t network_id, const std::string& node_id)
{
    auto err = network_svc_.leave_network(network_id, node_id);
    if (!err.empty()) {
        return make_error_response(http::status::bad_request, err);
    }

    return make_json_response(http::status::ok, {{"message", "Device removed from network"}});
}

http::response<http::string_body> HttpServer::handle_list_network_devices(
    const http::request<http::string_body>& req, int64_t network_id)
{
    auto devices = network_svc_.get_network_devices(network_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : devices) {
        arr.push_back(d.to_peer_json());
    }
    return make_json_response(http::status::ok, arr);
}

http::response<http::string_body> HttpServer::handle_ws_token(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto auth_header = req.base()["Authorization"];
    if (auth_header.substr(0, 7) != "Bearer ") {
        return make_error_response(http::status::unauthorized, "Invalid authorization header");
    }
    auto token = auth_header.substr(7);

    return make_json_response(http::status::ok, {
        {"token", token}
    });
}

http::response<http::string_body> HttpServer::handle_health(
    const http::request<http::string_body>& req)
{
    return make_json_response(http::status::ok, {
        {"status", "ok"},
        {"online_devices", session_mgr_.onlineCount()}
    });
}

http::response<http::string_body> HttpServer::handle_admin_overview(
    const http::request<http::string_body>& req, int64_t user_id)
{
    auto db_devices = device_svc_.list_user_devices(user_id);
    auto networks = network_svc_.list_user_networks(user_id);
    auto runtime_nodes = node_registry_.listAllNodes();

    nlohmann::json device_networks = nlohmann::json::object();
    nlohmann::json network_devices = nlohmann::json::object();
    for (const auto& network : networks) {
        auto members = network_svc_.get_network_devices(network.id);
        nlohmann::json member_ids = nlohmann::json::array();
        for (const auto& member : members) {
            device_networks[member.node_id].push_back({
                {"id", network.id},
                {"name", network.name},
                {"subnet", network.subnet}
            });
            member_ids.push_back(member.node_id);
        }
        network_devices[std::to_string(network.id)] = member_ids;
    }

    nlohmann::json peers = nlohmann::json::array();
    for (const auto& node : runtime_nodes) {
        if (session_mgr_.hasSession(node.node_id)) {
            peers.push_back(node.to_peer_json());
        }
    }

    std::unordered_map<std::string, Device> merged_devices;
    for (const auto& device : db_devices) {
        merged_devices[device.node_id] = device;
    }
    for (const auto& node : runtime_nodes) {
        bool has_session = session_mgr_.hasSession(node.node_id);
        auto it = merged_devices.find(node.node_id);
        if (it == merged_devices.end()) {
            Device synthetic;
            synthetic.node_id = node.node_id;
            synthetic.device_name = node.node_id;
            synthetic.public_key = node.public_key;
            synthetic.public_ip = node.public_ip;
            synthetic.public_port = node.public_port;
            synthetic.lan_ips = node.local_addrs;
            synthetic.virtual_ip = node.virtual_ip;
            synthetic.online = has_session;
            merged_devices[node.node_id] = std::move(synthetic);
        } else {
            it->second.online = has_session;
            if (it->second.public_ip.empty()) it->second.public_ip = node.public_ip;
            if (it->second.public_port == 0) it->second.public_port = node.public_port;
            if (it->second.lan_ips.empty()) it->second.lan_ips = node.local_addrs;
            if (it->second.virtual_ip.empty()) it->second.virtual_ip = node.virtual_ip;
        }

        if (!node.network_id.empty()) {
            auto& memberships = device_networks[node.node_id];
            bool exists = false;
            for (const auto& item : memberships) {
                if (item.value("name", std::string()) == node.network_id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                memberships.push_back({
                    {"id", node.network_id},
                    {"name", node.network_id},
                    {"subnet", node.subnet}
                });
            }

            auto& member_ids = network_devices[node.network_id];
            bool member_exists = false;
            for (const auto& item : member_ids) {
                if (item.get<std::string>() == node.node_id) {
                    member_exists = true;
                    break;
                }
            }
            if (!member_exists) {
                member_ids.push_back(node.node_id);
            }
        }
    }

    nlohmann::json device_arr = nlohmann::json::array();
    for (const auto& [_, device] : merged_devices) {
        auto json = device.to_json();
        auto node = node_registry_.findNode(device.node_id);
        bool ws_online = node.has_value() ? node->session_online : session_mgr_.hasSession(device.node_id);
        bool registry_online = node.has_value() && node->online;
        json["online"] = ws_online;
        json["ws_online"] = ws_online;
        json["registry_online"] = registry_online;
        json["connection_state"] = node.has_value()
            ? node->connection_state
            : (ws_online ? "online" : (registry_online ? "grace" : "offline"));
        json["transport_mode"] = node.has_value() ? node->transport_mode : "unknown";
        json["last_transport_rtt_ms"] = node.has_value() ? node->last_transport_rtt_ms : 0;
        json["reconnect_count"] = node.has_value() ? node->reconnect_count : 0;
        json["last_disconnect_reason"] = node.has_value() ? node->last_disconnect_reason : "";
        device_arr.push_back(std::move(json));
    }
    nlohmann::json network_arr = nlohmann::json::array();
    for (const auto& network : networks) {
        network_arr.push_back(network.to_json());
    }
    for (const auto& node : runtime_nodes) {
        if (node.network_id.empty()) continue;
        bool exists = false;
        for (const auto& network : network_arr) {
            if (network.value("name", std::string()) == node.network_id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            network_arr.push_back({
                {"id", node.network_id},
                {"name", node.network_id},
                {"subnet", node.subnet},
                {"owner_id", 0},
                {"created_at", ""},
                {"updated_at", ""}
            });
        }
    }

    return make_json_response(http::status::ok, {
        {"user", {{"user_id", user_id}}},
        {"devices", device_arr},
        {"networks", network_arr},
        {"online_peers", peers},
        {"device_networks", device_networks},
        {"network_devices", network_devices},
        {"health", {
            {"status", "ok"},
            {"online_sessions", session_mgr_.onlineCount()},
            {"online_registry_nodes", node_registry_.onlineCount()}
        }}
    });
}

http::response<http::string_body> HttpServer::handle_admin_device_detail(
    const http::request<http::string_body>& req, const std::string& node_id, int64_t user_id)
{
    auto device = device_svc_.get_device(node_id);
    auto node = node_registry_.findNode(node_id);
    if (!device.has_value() && !node.has_value()) {
        return make_error_response(http::status::not_found, "Device not found");
    }
    if (device.has_value() && device->user_id != user_id) {
        return make_error_response(http::status::forbidden, "Not your device");
    }

    auto networks = network_svc_.list_user_networks(user_id);
    nlohmann::json membership = nlohmann::json::array();
    for (const auto& network : networks) {
        auto members = network_svc_.get_network_devices(network.id);
        auto it = std::find_if(members.begin(), members.end(), [&](const Device& member) {
            return member.node_id == node_id;
        });
        if (it != members.end()) {
            membership.push_back(network.to_json());
        }
    }
    if (node.has_value()) {
        bool has_membership = false;
        for (const auto& item : membership) {
            if (item.value("name", std::string()) == node->network_id) {
                has_membership = true;
                break;
            }
        }
        if (!has_membership && !node->network_id.empty()) {
            membership.push_back({
                {"id", node->network_id},
                {"name", node->network_id},
                {"subnet", node->subnet},
                {"owner_id", 0},
                {"created_at", ""},
                {"updated_at", ""}
            });
        }
    }

    Device detail_device;
    if (device.has_value()) {
        detail_device = *device;
    } else {
        detail_device.node_id = node->node_id;
        detail_device.device_name = node->node_id;
        detail_device.public_key = node->public_key;
        detail_device.public_ip = node->public_ip;
        detail_device.public_port = node->public_port;
        detail_device.lan_ips = node->local_addrs;
        detail_device.virtual_ip = node->virtual_ip;
        detail_device.online = session_mgr_.hasSession(node_id);
    }

    bool ws_online = node.has_value() ? node->session_online : session_mgr_.hasSession(node_id);
    bool registry_online = node.has_value() && node->online;
    detail_device.online = ws_online;

    nlohmann::json runtime = {
        {"ws_online", ws_online},
        {"connection_state", node.has_value() ? node->connection_state : (ws_online ? "online" : (registry_online ? "grace" : "offline"))}
    };
    if (node.has_value()) {
        runtime["registry_online"] = node->online;
        runtime["session_online"] = node->session_online;
        runtime["transport_mode"] = node->transport_mode;
        runtime["last_transport_rtt_ms"] = node->last_transport_rtt_ms;
        runtime["reconnect_count"] = node->reconnect_count;
        runtime["last_disconnect_reason"] = node->last_disconnect_reason;
        runtime["runtime_public_key"] = node->public_key;
        runtime["runtime_public_ip"] = node->public_ip;
        runtime["runtime_public_port"] = node->public_port;
        runtime["runtime_virtual_ip"] = node->virtual_ip;
        runtime["runtime_network_id"] = node->network_id;
        runtime["runtime_local_addrs"] = node->local_addrs;
        runtime["gateway_ip"] = node->gateway_ip;
        runtime["subnet"] = node->subnet;
    }

    auto device_json = detail_device.to_json();
    device_json["online"] = ws_online;
    device_json["ws_online"] = ws_online;
    device_json["registry_online"] = registry_online;
    device_json["connection_state"] = runtime["connection_state"];
    device_json["transport_mode"] = runtime.value("transport_mode", std::string("unknown"));

    return make_json_response(http::status::ok, {
        {"device", device_json},
        {"networks", membership},
        {"runtime", runtime}
    });
}

// ── WebSocket Upgrade ─────────────────────────────────────

void HttpServer::handle_websocket_upgrade(tcp::socket&& socket,
                                           http::request<http::string_body> req)
{
    // Determine auth mode based on URL path
    auto target = std::string(req.target());
    bool use_node_auth = (target.find("/api/v1/ws/nat") != std::string::npos);

    if (use_node_auth) {
        // ── New protocol: node_id-based auth (no JWT required) ──
        auto session = std::make_shared<beast_ws::WsSession>(
            std::move(socket),
            std::move(req),

            // Node auth callback — validates node identity
            [this](const std::string& node_id,
                   const std::string& network_id,
                   const std::string& public_key) -> beast_ws::AuthResult {
                auto info = assign_virtual_ip(node_id, network_id, public_key);
                ensure_node_auth_persisted(info);
                node_registry_.registerNode(info);
                node_registry_.updateSessionState(node_id, true, "online");
                ensure_tun_gateway(info.network_id);

                beast_ws::AuthResult result;
                result.success = true;
                result.node_id = node_id;
                result.network_id = info.network_id;
                result.public_key = public_key;
                return result;
            },

            // Post-node-auth callback — registers session + sends peer list
            [this](beast_ws::WsSessionPtr session) {
                auto nid = session->node_id();
                auto net_id = session->network_id();

                // Register session
                session_mgr_.registerSession(nid, session);

                // Send peer_list with all online nodes in same network
                auto peers = node_registry_.listNetworkNodes(net_id);
                nlohmann::json peer_list = {
                    {"type", "peer_list"},
                    {"peers", nlohmann::json::array()}
                };
                for (const auto& p : peers) {
                    if (p.node_id != nid) {
                        peer_list["peers"].push_back(p.to_peer_json());
                    }
                }
                session->send_json(peer_list.dump());

                auto node = node_registry_.findNode(nid);
                if (node.has_value() && !node->virtual_ip.empty()) {
                    nlohmann::json vip_msg = {
                        {"type", "virtual_ip_assigned"},
                        {"virtual_ip", node->virtual_ip},
                        {"gateway_ip", node->gateway_ip},
                        {"subnet", node->subnet}
                    };
                    session->send_json(vip_msg.dump());
                }

                // Also broadcast updated peer list to other nodes in the network
                nlohmann::json broadcast_list = {
                    {"type", "peer_list"},
                    {"peers", nlohmann::json::array()}
                };
                for (const auto& p : peers) {
                    broadcast_list["peers"].push_back(p.to_peer_json());
                }
                session_mgr_.broadcastIf(broadcast_list, [&](const std::string& id) {
                    return id != nid && node_registry_.isOnline(id);
                });

                spdlog::info("Node online: node_id={}, network_id={}, online={}",
                             nid, net_id, session_mgr_.onlineCount());
            },

            // Message handler
            [this](beast_ws::WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
                // Dispatch to registered handlers
                auto type = msg["type"].get<std::string>();

                if (type == "update_addr") {
                    msg_dispatcher_.dispatch(session, node_id, msg);
                } else if (type == "heartbeat") {
                    msg_dispatcher_.dispatch(session, node_id, msg);
                } else if (type == "connect_peer") {
                    msg_dispatcher_.dispatch(session, node_id, msg);
                } else if (type == "tun_packet") {
                    handle_tun_packet(node_id, msg);
                } else {
                    spdlog::warn("Unknown message type '{}' from node_id={}", type, node_id);
                }
            },

            // Disconnect handler — only remove session, keep node in registry.
            // Node will be removed by HeartbeatMonitor if heartbeat times out.
            // This allows clients to reconnect within the heartbeat window
            // without losing their state.
            [this](const std::string& node_id) {
                if (shutting_down_) return;
                spdlog::info("WebSocket disconnected: node_id={} (waiting for heartbeat timeout)", node_id);
                node_registry_.updateSessionState(node_id, false, "grace", "ws_disconnected");
                session_mgr_.removeSession(node_id);

                // Broadcast updated peer list (node is still in registry but offline session)
                auto node = node_registry_.findNode(node_id);
                if (node.has_value()) {
                    auto peers = node_registry_.listNetworkNodes(node->network_id);
                    nlohmann::json peer_list = {{"type", "peer_list"}, {"peers", nlohmann::json::array()}};
                    for (const auto& p : peers) {
                        if (p.node_id != node_id) {
                            peer_list["peers"].push_back(p.to_peer_json());
                        }
                    }
                    session_mgr_.broadcastIf(peer_list, [&](const std::string& id) {
                        return id != node_id && node_registry_.isOnline(id);
                    });
                }
            },

            config_.websocket.heartbeat_timeout_sec
        );

        session->start();

    } else {
        // ── Legacy: JWT-based auth ──
        auto session = std::make_shared<beast_ws::WsSession>(
            std::move(socket),
            std::move(req),

            // Auth callback: verify JWT token
            [this](const std::string& token) -> std::optional<auth::JwtPayload> {
                return jwt_.verify_token(token);
            },

            // Post-auth callback: log successful auth
            [](beast_ws::WsSessionPtr session, int64_t user_id, const std::string& username) {
                spdlog::info("WebSocket authenticated: user_id={}, username={}", user_id, username);
            },

            // Message handler: dispatch WS messages after auth
            [this](beast_ws::WsSessionPtr session, const std::string& node_id, const nlohmann::json& msg) {
                auto type = msg["type"].get<std::string>();

                if (type == "device_update") {
                    auto& d = msg["device"];
                    std::vector<std::string> lan_ips;
                    if (d.contains("lan_ips")) {
                        lan_ips = d["lan_ips"].get<std::vector<std::string>>();
                    }

                    device_svc_.update_connection_info(
                        node_id,
                        d.value("public_ip", std::string("")),
                        d.value("public_port", 0),
                        lan_ips
                    );

                    auto device = device_svc_.get_device(node_id);
                    if (device.has_value()) {
                        session->set_device_info(*device);
                    }

                    peer_mgr_.register_session(node_id, session);

                } else if (type == "connect_peer") {
                    auto target = msg["target_node_id"].get<std::string>();
                    auto err = peer_mgr_.handle_connect_peer(node_id, target);
                    if (!err.empty()) {
                        nlohmann::json response = {
                            {"type", "error"},
                            {"message", err}
                        };
                        session->send_json(response.dump());
                    }
                }
            },

            // Disconnect handler
            [this](const std::string& node_id) {
                if (shutting_down_) return;
                peer_mgr_.unregister_session(node_id);
                device_svc_.set_offline(node_id);
            },

            config_.websocket.heartbeat_timeout_sec
        );

        session->start();
    }
}

// ── Auth helper ────────────────────────────────────────────

std::optional<auth::JwtPayload> HttpServer::authenticate(
    const http::request<http::string_body>& req)
{
    auto auth_header = req.base()["Authorization"];
    spdlog::debug("Auth header: '{}'", std::string(auth_header));
    if (auth_header.empty() || auth_header.substr(0, 7) != "Bearer ") {
        spdlog::debug("Auth header invalid format");
        return std::nullopt;
    }

    std::string token(auth_header.substr(7));
    auto result = jwt_.verify_token(token);
    if (!result.has_value()) {
        spdlog::debug("Token verification failed");
    }
    return result;
}

// ── Response helpers ───────────────────────────────────────

http::response<http::string_body> HttpServer::make_json_response(
    http::status status, const nlohmann::json& body)
{
    http::response<http::string_body> res{status, 11};
    res.set(http::field::server, "NATMesh-Server/0.1");
    res.set(http::field::content_type, "application/json");
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::make_html_response(
    http::status status, const std::string& body)
{
    http::response<http::string_body> res{status, 11};
    res.set(http::field::server, "NATMesh-Server/0.1");
    res.set(http::field::content_type, "text/html; charset=utf-8");
    res.body() = body;
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::make_error_response(
    http::status status, const std::string& message)
{
    return make_json_response(status, {{"error", message}});
}
