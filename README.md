# NATMesh — NAT 穿透组网系统

轻量级 NAT 穿透组网系统。控制服务器负责设备管理、虚拟网络组织和信令交换，**不参与 VPN 数据转发**，**不做 TURN/Relay**。

```
设备 A ──WebSocket──→ NATMesh Server ←──WebSocket── 设备 B
    │                      │                      │
    │   ← punch_start ──→  │  ← punch_start ──→   │
    │                      │                      │
    ├── STUN Server ───────┤                      │
    │                      │                      │
    └──────── UDP Hole Punch (直连) ──────────────┘
```

---

## 目录

- [架构](#架构)
- [技术栈](#技术栈)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [构建目标](#构建目标)
- [运行指南](#运行指南)
- [NAT 协议（轻量认证）](#nat-协议轻量认证)
- [STUN 协议](#stun-协议)
- [UDP Hole Punch 协议](#udp-hole-punch-协议)
- [打洞测试](#打洞测试)
- [完整信令流程](#完整信令流程)
- [REST API](#rest-api)
- [WebSocket 协议（JWT 认证）](#websocket-协议jwt-认证)
- [配置说明](#配置说明)
- [数据库](#数据库)
- [构建选项](#构建选项)

---

## 架构

### 分层设计

```
┌─────────────────────────────────────────────────────┐
│             HTTP / WebSocket (Boost.Beast)           │
│           REST API  +  WebSocket 升级                │
├─────────────────────────────────────────────────────┤
│                      JWT 鉴权                         │
├─────────────────────────────────────────────────────┤
│   UserService  │  DeviceService  │  NetworkService   │
│   PeerManager  │  SessionManager  │  NodeRegistry    │
│   MessageDispatcher  │  HeartbeatMonitor             │
├─────────────────────────────────────────────────────┤
│   UserRepo  │  DeviceRepo  │  NetworkRepo            │
├─────────────────────────────────────────────────────┤
│                      SQLite                          │
└─────────────────────────────────────────────────────┘

┌──────────────────┐   ┌──────────────────────────────┐
│   STUN Server     │   │   Test Client                │
│   (UDP 3478)      │   │   WsClient + UdpPuncher      │
└──────────────────┘   └──────────────────────────────┘
```

### 核心模块

| 模块 | 职责 |
|---|---|
| `SessionManager` | 线程安全 WebSocket 会话管理，NodeID → Session 映射，`sendTo` / `broadcast` |
| `NodeRegistry` | 在线节点信息注册中心，记录公钥/公网IP/局域网IP/最后心跳 |
| `MessageDispatcher` | 按 `type` 字段路由 WebSocket 消息到注册处理器 |
| `HeartbeatMonitor` | 周期性心跳超时检查，自动下线超时节点 |
| `StunServer` | UDP 端口 3478，返回客户端公网地址 |

### 核心设计决策

| 决策 | 选择 | 理由 |
|---|---|---|
| 数据库 | SQLite | MVP 无需独立数据库进程，单文件部署 |
| 端口 | 单端口复用 | Beast 支持 HTTP + WebSocket 同端口 |
| JWT 算法 | HS256 | 纯 C++ 实现，零外部加密依赖 |
| 密码存储 | HMAC-SHA256 x 10000 轮 | 无需 OpenSSL，安全强度够 MVP |
| 设备标识 | UUID v4 | 全局唯一，客户端/服务端均可生成 |
| 心跳 | 15s 间隔 / 45s 超时 | 平衡精度与开销 |
| HTTP 处理 | 每连接独立线程 | 同步读写，避免 async 生命周期问题 |

---

## 技术栈

| 组件 | 库 | 版本 |
|---|---|---|
| 语言 | C++20 | |
| 构建 | CMake | ≥ 3.20 |
| 网络 | Boost.Asio + Boost.Beast | 1.83+ |
| JSON | nlohmann/json | 3.11+ (FetchContent) |
| 日志 | spdlog | 1.14+ (FetchContent) |
| 配置 | yaml-cpp | 0.8+ (系统) |
| 数据库 | SQLite3 | (系统) |
| 加密 | 纯 C++ SHA-256 + HMAC | 无外部依赖 |

---

## 项目结构

```
├── CMakeLists.txt
├── cmake/dependencies.cmake       # 依赖管理
├── config.yaml                    # 默认配置文件
├── init.sql                       # 数据库建表 SQL（参考）
├── cmd/
│   ├── server/main.cpp            # 控制服务器入口
│   ├── stun_server/main.cpp       # STUN 服务入口
│   └── test_client/main.cpp       # 打洞测试客户端入口
├── src/
│   ├── config/
│   │   ├── config.h               # 配置结构体
│   │   └── config.cpp             # yaml-cpp 加载
│   ├── model/
│   │   ├── user.h                 # 用户模型
│   │   ├── device.h               # 设备模型
│   │   └── network.h              # 网络模型
│   ├── repository/
│   │   ├── database.h/.cpp        # SQLite 初始化 + 建表
│   │   ├── user_repo.h/.cpp       # 用户 CRUD
│   │   ├── device_repo.h/.cpp     # 设备 CRUD
│   │   └── network_repo.h/.cpp    # 网络 CRUD
│   ├── auth/
│   │   ├── jwt.h                  # JWT 接口
│   │   └── jwt.cpp                # JWT HS256 实现
│   ├── util/
│   │   ├── uuid.h                 # UUID 生成接口
│   │   └── uuid.cpp               # UUID v4 实现
│   ├── service/
│   │   ├── user_service.h/.cpp    # 注册/登录业务
│   │   ├── device_service.h/.cpp  # 设备管理业务
│   │   ├── network_service.h/.cpp # 虚拟网络业务
│   │   └── peer_manager.h/.cpp    # NAT 穿透信令调度（JWT 路径）
│   ├── http/
│   │   ├── http_server.h/.cpp     # REST API + WebSocket 升级
│   ├── ws/
│   │   └── ws_session.h/.cpp      # WebSocket 会话
│   ├── nat/
│   │   ├── session_manager.h/.cpp # WebSocket 会话管理
│   │   ├── node_registry.h/.cpp   # 在线节点注册中心
│   │   ├── message_dispatcher.h/.cpp  # 消息路由
│   │   └── heartbeat_monitor.h/.cpp   # 心跳监控
│   ├── stun/
│   │   ├── StunServer.h           # STUN 服务
│   │   └── StunServer.cpp
│   └── client/
│       ├── WsClient.h/.cpp        # WebSocket 客户端
│       ├── UdpPuncher.h/.cpp      # UDP 打洞引擎
│       └── TestClient.h/.cpp      # 打洞测试编排
```

---

## 快速开始

### 依赖安装

**Ubuntu 22.04 / 24.04：**

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libboost-all-dev \
    libsqlite3-dev \
    libyaml-cpp-dev
```

### 编译

```bash
git clone <repo-url> natmesh-server
cd natmesh-server
mkdir -p build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j1   # 低内存环境用 -j1
```

编译完成后生成三个可执行文件：

```bash
ls -la build/natmesh-server build/stun_server build/test_client
```

---

## 构建目标

| 目标 | 文件 | 依赖 | 功能 |
|---|---|---|---|
| `natmesh-server` | `cmd/server/main.cpp` | yaml-cpp, sqlite3 | 控制服务器（REST + WS）|
| `stun_server` | `cmd/stun_server/main.cpp` | 无 | STUN UDP 服务 |
| `test_client` | `cmd/test_client/main.cpp` | 无 | 打洞测试客户端 |

---

## 运行指南

### 1. 启动控制服务器

```bash
mkdir -p data
./build/natmesh-server config.yaml
# 监听 0.0.0.0:8080 (HTTP + WebSocket)
```

### 2. 启动 STUN 服务

```bash
./build/stun_server --listen 0.0.0.0 --port 3478
```

### 3. 启动两个测试客户端

**终端 A — 节点 A（发起连接）：**

```bash
./build/test_client \
  --node-id node-a \
  --network-id home \
  --control ws://127.0.0.1:8080 \
  --stun 127.0.0.1:3478 \
  --udp-port 40001 \
  --connect node-b \
  --local-addr 127.0.0.1:40001
```

**终端 B — 节点 B（等待连接）：**

```bash
./build/test_client \
  --node-id node-b \
  --network-id home \
  --control ws://127.0.0.1:8080 \
  --stun 127.0.0.1:3478 \
  --udp-port 40002 \
  --local-addr 127.0.0.1:40002
```

### 预期输出

```
═══════════════════════════════════════════
  P2P SUCCESS with node_id=node-b
  Remote addr: 127.0.0.1:40002
  RTT: 0ms
═══════════════════════════════════════════
✓ P2P Hole Punch SUCCESS with node-b at 127.0.0.1:40002 (RTT=0ms)
```

---

## NAT 协议（轻量认证）

WebSocket 路径：`ws://host:port/api/v1/ws/nat`

无需 JWT token，直接使用 `node_id + network_id + public_key` 认证。

### 认证

```
C→S: {
  "type": "auth",
  "node_id": "node-a",
  "network_id": "home",
  "public_key": "ssh-ed25519 AAAAC3..."
}

S→C: {
  "type": "auth_ok",
  "node_id": "node-a",
  "network_id": "home"
}

S→C: {
  "type": "peer_list",
  "peers": [ ... ]
}
```

### 地址上报

```
C→S: {
  "type": "update_addr",
  "public_ip": "1.2.3.4",
  "public_port": 50000,
  "local_addrs": ["192.168.1.10:50000", "10.0.0.2:50000"]
}
```

### 心跳

```
C→S: {"type": "heartbeat"}
S→C: {"type": "pong"}
```

客户端每 30 秒发送一次。服务端超过 90 秒未收到心跳则自动下线。

### 打洞信令

```
C→S: {"type": "connect_peer", "target_node_id": "node-b"}

S→C (to A): {
  "type": "punch_start",
  "direction": "outgoing",
  "peer_node_id": "node-b",
  "peer_public_ip": "5.6.7.8",
  "peer_public_port": 60000,
  "peer_local_addrs": ["192.168.1.20:60000"],
  "peer_public_key": "key-b"
}

S→C (to B): {
  "type": "punch_start",
  "direction": "incoming",
  "peer_node_id": "node-a",
  "peer_public_ip": "1.2.3.4",
  "peer_public_port": 50000,
  "peer_local_addrs": ["192.168.1.10:50000"],
  "peer_public_key": "key-a"
}
```

---

## STUN 协议

UDP 端口 **3478**，JSON over UDP。

### 请求

```json
{"type": "stun_request", "node_id": "node-a"}
```

### 响应

```json
{"type": "stun_response", "public_ip": "1.2.3.4", "public_port": 50000}
```

服务端返回客户端 UDP 套接字看到的公网 IP 和端口。

---

## UDP Hole Punch 协议

UDP 直连，JSON over UDP。

### Punch 包

```json
{
  "type": "punch",
  "from_node_id": "node-a",
  "to_node_id": "node-b",
  "timestamp": 1685000000123
}
```

### Punch ACK

```json
{
  "type": "punch_ack",
  "from_node_id": "node-b",
  "to_node_id": "node-a",
  "timestamp": 1685000000123
}
```

### 打洞参数

| 参数 | 值 |
|---|---|
| 发送间隔 | 100ms |
| 超时时间 | 5s |
| 发送目标 | 公网地址 + 所有局域网地址 |
| 成功条件 | 收到对方 `punch_ack` |

---

## 打洞测试

### 命令行参数

**test_client：**

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--node-id` | 节点标识 | `test-node` |
| `--network-id` | 网络标识 | `home` |
| `--control` | 控制服务器 WS URL | `127.0.0.1:8080` |
| `--stun` | STUN 服务器地址 | `127.0.0.1:3478` |
| `--udp-port` | 本地 UDP 绑定端口 | 自动分配 |
| `--connect` | 要连接的目标节点 ID | 空（等待） |
| `--local-addr` | 本地地址（逗号分隔） | 自动生成 |

**stun_server：**

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--listen` | 监听地址 | `0.0.0.0` |
| `--port` | UDP 端口 | `3478` |

---

## 完整信令流程

```
设备 A                     NATMesh Server                 STUN Server                设备 B
  │                            │                            │                          │
  │                          STUN Query                     │                          │
  ├──── stun_request ────────►│                            │                          │
  │◄─── stun_response ────────┤                            │                          │
  │                            │                            │                          │
  │                    WebSocket 连接                        │                          │
  ├──── auth ────────────────►│                            │                          │
  │◄─── auth_ok ──────────────┤                            │                          │
  │◄─── peer_list ────────────┤                            │                          │
  ├──── update_addr ─────────►│                            │                          │
  │                            │                            │                          │
  │                    connect_peer                         │                          │
  ├──── connect_peer(B) ─────►│                            │                          │
  │                            │                            │                          │
  │◄─── punch_start(out) ──── │                            │── punch_start(in) ──────►│
  │                            │                            │                          │
  │  ── punch (公网+局域网) ──────────────────────────────────────────────────────►  │
  │  ◄─ punch_ack ──────────────────────────────────────────────────────────────────  │
  │                            │                            │                          │
  │◄══════ P2P SUCCESS ══════►│                            │                          │
```

---

## REST API

所有请求和响应均为 `Content-Type: application/json`。

### 公开端点

#### `GET /health`

```json
{"status": "ok", "online_devices": 0}
```

#### `POST /api/v1/auth/register`

```json
// Request
{"username": "alice", "password": "secret123"}
// Response 201
{"message": "User registered successfully"}
```

#### `POST /api/v1/auth/login`

```json
// Request
{"username": "alice", "password": "secret123"}
// Response 200
{"token": "eyJhbGciOiJIUzI1NiIs...", "user_id": 1, "username": "alice"}
```

### 认证端点（需 `Authorization: Bearer <token>`）

| 方法 | 路径 | 功能 |
|---|---|---|
| POST | `/api/v1/devices` | 注册设备 |
| GET | `/api/v1/devices` | 列出设备 |
| GET | `/api/v1/devices/:node_id` | 设备详情 |
| DELETE | `/api/v1/devices/:node_id` | 删除设备 |
| POST | `/api/v1/networks` | 创建网络 |
| GET | `/api/v1/networks` | 列出网络 |
| POST | `/api/v1/networks/:id/join` | 设备加入网络 |
| DELETE | `/api/v1/networks/:id/devices/:node_id` | 设备退出网络 |
| GET | `/api/v1/networks/:id/devices` | 网络内设备列表 |
| GET | `/api/v1/nat/peers` | NAT 在线节点列表 |

---

## WebSocket 协议（JWT 认证）

路径：`ws://host:port/api/v1/ws`

通过 REST API 获取 JWT token 后进行认证。

### 认证

```
C→S: {"type":"auth", "token":"eyJhbGciOiJIUzI1NiIs..."}
S→C: {"type":"auth_ok", "user_id":1, "username":"alice"}
```

### 设备上线

```
C→S: {
  "type": "device_update",
  "device": {
    "public_ip": "1.2.3.4",
    "public_port": 54321,
    "lan_ips": ["192.168.1.100"]
  }
}
```

### 心跳

```
C→S: {"type":"heartbeat"}
S→C: {"type":"pong"}
```

### 协议状态机

```
未认证 ──auth──→ 已认证 ──device_update──→ 在线
                    │                          │
                    ├──heartbeat──→ pong        ├──connect_peer──→ punch_start
                    │                          │
                    └──断开──→ offline          └──断开──→ offline
```

---

## 配置说明

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  workers: 4

database:
  path: "data/natmesh.db"

jwt:
  secret: "change-this-to-a-secure-random-secret-in-production"
  expiration_hours: 72

websocket:
  heartbeat_interval_sec: 15
  heartbeat_timeout_sec: 45

logging:
  level: "info"
  pattern: "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v"
```

---

## 数据库

### 表结构

```sql
CREATE TABLE users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE devices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id         TEXT NOT NULL UNIQUE,
    user_id         INTEGER NOT NULL,
    device_name     TEXT NOT NULL,
    public_key      TEXT NOT NULL DEFAULT '',
    public_ip       TEXT NOT NULL DEFAULT '',
    public_port     INTEGER NOT NULL DEFAULT 0,
    lan_ips         TEXT NOT NULL DEFAULT '[]',
    online          INTEGER NOT NULL DEFAULT 0,
    last_heartbeat  TEXT,
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE networks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    owner_id    INTEGER NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE network_devices (
    network_id  INTEGER NOT NULL,
    device_id   INTEGER NOT NULL,
    joined_at   TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (network_id, device_id),
    FOREIGN KEY (network_id) REFERENCES networks(id) ON DELETE CASCADE,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE
);
```

---

## 构建选项

### Debug 构建

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j1
```

### Release 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j1
```

### 单目标构建

```bash
cmake --build build -j1 --target stun_server
cmake --build build -j1 --target test_client
cmake --build build -j1 --target natmesh-server
```

### 清理

```bash
rm -rf build build-debug
```
