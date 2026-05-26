# NATMesh Server — NAT 穿透组网控制面

轻量级 NAT 穿透组网系统**服务器端控制面**。只负责信令交换、设备管理、虚拟网络组织，**不参与 VPN 数据转发**，**不做 Relay**。

```
设备 A ──WebSocket──→ NATMesh Server ←──WebSocket── 设备 B
    │                      │                      │
    │   ← punch_start ──→  │  ← punch_start ──→   │
    │                      │                      │
    └──────── UDP Hole Punch (直连) ──────────────┘
```

---

## 目录

- [架构](#架构)
- [技术栈](#技术栈)
- [项目结构](#项目结构)
- [快速开始](#快速开始)
- [配置说明](#配置说明)
- [REST API](#rest-api)
- [WebSocket 协议](#websocket-协议)
- [协议流程](#协议流程)
- [数据库](#数据库)
- [构建选项](#构建选项)

---

## 架构

### 分层设计

```
┌─────────────────────────────────────────────────────┐
│                   HTTP / WebSocket                   │
│          (Boost.Beast — 单端口复用)                   │
├─────────────────────────────────────────────────────┤
│                   JWT 鉴权                            │
├─────────────────────────────────────────────────────┤
│   UserService  │  DeviceService  │  NetworkService   │
│   PeerManager (NAT 信令调度)                          │
├─────────────────────────────────────────────────────┤
│   UserRepo  │  DeviceRepo  │  NetworkRepo            │
├─────────────────────────────────────────────────────┤
│                  SQLite                              │
└─────────────────────────────────────────────────────┘
```

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
├── cmd/server/main.cpp            # 入口
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
│   │   └── jwt.cpp                # JWT HS256 实现（纯 C++ SHA-256）
│   ├── util/
│   │   ├── uuid.h                 # UUID 生成接口
│   │   └── uuid.cpp               # UUID v4 实现
│   ├── service/
│   │   ├── user_service.h/.cpp    # 注册/登录业务
│   │   ├── device_service.h/.cpp  # 设备管理业务
│   │   ├── network_service.h/.cpp # 虚拟网络业务
│   │   └── peer_manager.h/.cpp    # NAT 穿透信令调度
│   ├── http/
│   │   ├── http_server.h/.cpp     # REST API 路由 + WebSocket 升级
│   └── ws/
│       └── ws_session.h/.cpp      # WebSocket 会话管理
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
cmake --build build -j$(nproc)
```

### 运行

```bash
# 使用默认配置（监听 0.0.0.0:8080）
mkdir -p data
./build/natmesh-server config.yaml

# 使用自定义配置
./build/natmesh-server /path/to/config.yaml
```

### 验证

```bash
# 健康检查
curl http://localhost:8080/health

# 注册
curl -X POST http://localhost:8080/api/v1/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"demo123"}'

# 登录
curl -X POST http://localhost:8080/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"demo123"}'
```

---

## 配置说明

```yaml
server:
  host: "0.0.0.0"
  port: 8080
  workers: 4           # io_context 线程数

database:
  path: "data/natmesh.db"   # SQLite 文件路径

jwt:
  secret: "change-this-to-a-secure-random-secret-in-production"
  expiration_hours: 72

websocket:
  heartbeat_interval_sec: 15   # 建议客户端发送间隔
  heartbeat_timeout_sec: 45    # 超过此时间未收到心跳则断开

logging:
  level: "info"       # trace, debug, info, warn, error, critical
  pattern: "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v"
```

---

## REST API

所有请求和响应均为 `Content-Type: application/json`。

### 公开端点

#### `POST /api/v1/auth/register`

注册新用户。

```json
// Request
{"username": "alice", "password": "secret123"}
// Response 201
{"message": "User registered successfully"}
// Error 400
{"error": "Username already exists"}
```

#### `POST /api/v1/auth/login`

登录获取 JWT。

```json
// Request
{"username": "alice", "password": "secret123"}
// Response 200
{
  "token": "eyJhbGciOiJIUzI1NiIs...",
  "user_id": 1,
  "username": "alice"
}
```

#### `GET /health`

```json
{"status": "ok", "online_devices": 0}
```

### 认证端点

以下端点需要在 HTTP Header 中携带 `Authorization: Bearer <token>`。

#### `POST /api/v1/devices`

注册一个设备到当前用户。

```json
// Request
{"device_name": "my-pc", "public_key": "ssh-ed25519 AAAAC3..."}
// Response 201
{"message": "Device created successfully", "node_id": "5a76a686-a2bd-4992-8707-180d180dea52"}
```

#### `GET /api/v1/devices`

列出当前用户的所有设备。

```json
// Response 200
[{
  "node_id": "5a76a686-a2bd-4992-8707-180d180dea52",
  "device_name": "my-pc",
  "public_key": "ssh-ed25519 AAAAC3...",
  "public_ip": "",
  "public_port": 0,
  "lan_ips": [],
  "online": false,
  "last_heartbeat": null,
  "created_at": "2026-05-26 03:38:50",
  "updated_at": "2026-05-26 03:38:50"
}]
```

#### `GET /api/v1/devices/:node_id`

获取设备详情。

#### `DELETE /api/v1/devices/:node_id`

删除设备。

#### `POST /api/v1/networks`

创建虚拟网络。

```json
{"name": "home-net"}
// Response 201
{"id": 1, "message": "Network created successfully"}
```

#### `GET /api/v1/networks`

列出当前用户的所有网络。

#### `POST /api/v1/networks/:id/join`

设备加入指定网络。

```json
{"node_id": "5a76a686-a2bd-4992-8707-180d180dea52"}
```

#### `DELETE /api/v1/networks/:id/devices/:node_id`

设备退出指定网络。

#### `GET /api/v1/networks/:id/devices`

列出指定网络中的所有设备（包含其他用户的设备）。

---

## WebSocket 协议

### 连接

```
ws://host:port/api/v1/ws
```

### 消息类型

#### 认证

```
C→S: {"type":"auth", "token":"eyJhbGciOiJIUzI1NiIs..."}
S→C: {"type":"auth_ok", "user_id":1, "username":"alice"}
S→C: {"type":"error", "message":"Invalid token"}
```

#### 心跳保活

```
C→S: {"type":"heartbeat"}
S→C: {"type":"pong"}
```

建议每 15 秒发送一次心跳。服务端 45 秒无心跳则断开连接。

#### 上线注册

设备连接 WebSocket 并通过 auth 后，需发送 `device_update` 通知服务端自己的网络信息：

```
C→S: {
  "type": "device_update",
  "device": {
    "public_ip": "1.2.3.4",
    "public_port": 54321,
    "lan_ips": ["192.168.1.100", "10.0.0.2"]
  }
}
```

此操作将：
- 更新数据库中设备的公网 IP / 端口 / 局域网 IP
- 在 PeerManager 中注册会话
- 设备状态标记为 `online`

#### 节点列表同步

当设备上线/下线时，服务端自动向同网络内所有在线设备推送：

```
S→C: {
  "type": "peer_list",
  "peers": [{
    "node_id": "...",
    "device_name": "other-device",
    "public_key": "...",
    "public_ip": "5.6.7.8",
    "public_port": 12345,
    "lan_ips": ["10.0.0.5"],
    "online": true
  }]
}
```

#### NAT 穿透信令

设备 A 请求连接设备 B：

```
C→S: {"type":"connect_peer", "target_node_id":"<B 的 node_id>"}
```

服务端向 A 发送（告知如何去连接 B）：

```
S→C: {
  "type": "punch_start",
  "direction": "outgoing",
  "peer": {
    "node_id": "<B 的 node_id>",
    "device_name": "B 的设备名",
    "public_key": "B 的公钥",
    "public_ip": "B 的公网 IP",
    "public_port": B 的公网端口,
    "lan_ips": ["B 的局域网 IP 列表"],
    "online": true
  }
}
```

服务端同时向 B 发送（告知有人要来连接）：

```
S→C: {
  "type": "punch_start",
  "direction": "incoming",
  "peer": {
    "node_id": "<A 的 node_id>",
    ...  // A 的连接信息
  }
}
```

A 和 B 收到 `punch_start` 后各自启动 UDP Hole Punch 尝试建立直连。

#### 错误

```
S→C: {"type":"error", "message":"Target device is not online"}
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

## 协议流程

### 完整信令流程

```
设备 A                     NATMesh Server                   设备 B
  │                            │                              │
  ├── WebSocket 连接 ──────►   │    ◄── WebSocket 连接 ──────┤
  │                            │                              │
  ├── auth(token) ──────────►  │  ◄── auth(token) ───────────┤
  │  ◄── auth_ok ────────────  │    ── auth_ok ──────────────►│
  │                            │                              │
  ├── device_update ────────►  │  ◄── device_update ─────────┤
  │  ◄── peer_list ──────────  │    ── peer_list ────────────►│
  │                            │                              │
  │  ◄── peer_list (更新) ───  │                              │
  │                            │                              │
  │  用户触发连接              │                              │
  ├── connect_peer(B) ──────►  │                              │
  │                            │                              │
  │  ◄── punch_start(out) ──  │  ── punch_start(in) ────────►│
  │                            │                              │
  │  ───── UDP Hole Punch (直连) ──────────────────────────► │
  │  ◄────────────────────────────────────────────────────── │
```

### 核心原则

1. **服务端不参与数据转发** — 只交换信令信息
2. **服务端不下发中继地址** — MVP 不包含 TURN/Relay 功能
3. **打洞由客户端自主完成** — 服务器下发对端地址后即完成职责
4. **公钥仅由客户端验证** — 服务器信任客户端提交的公钥

---

## 数据库

### 表结构

```sql
-- 用户表
CREATE TABLE users (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,          -- $sha256$10000$salt$hash
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

-- 设备表
CREATE TABLE devices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id         TEXT NOT NULL UNIQUE,        -- UUID v4
    user_id         INTEGER NOT NULL,
    device_name     TEXT NOT NULL,
    public_key      TEXT NOT NULL DEFAULT '',
    public_ip       TEXT NOT NULL DEFAULT '',
    public_port     INTEGER NOT NULL DEFAULT 0,
    lan_ips         TEXT NOT NULL DEFAULT '[]',  -- JSON 数组
    online          INTEGER NOT NULL DEFAULT 0,
    last_heartbeat  TEXT,
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 虚拟网络表
CREATE TABLE networks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    owner_id    INTEGER NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 网络-设备关联表
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
cmake --build build-debug -j$(nproc)
```

### 单线程构建（低内存环境）

```bash
cmake --build build -j1
```

### 清理

```bash
rm -rf build build-debug
```
