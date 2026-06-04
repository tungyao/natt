# NATMesh Client Refactoring & WebView GUI Design

## 概述

将现有 `test_client` 重构为 **Core + CLI + GUI** 三层架构。Core 封装全部 NAT 穿透逻辑为静态库，CLI 作为 Linux 下的薄封装前端，GUI 通过 webview 库提供 Windows 桌面图形界面。

## 架构

```
natcore (static library)
  ├── src/core/CoreClient.h/.cpp   — 统一入口，封装 TestClient/WsClient/UdpPuncher
  ├── src/client/                  — TestClient/WsClient/UdpPuncher 作为内部实现
  └── include/natcore/CoreClient.h — 前端唯一需要 include 的公共头

natt-cli (executable, Linux/Windows)
  └── cmd/client/main.cpp          — 解析 CLI 参数 → CoreClient → 信号等待退出

natt-gui (executable, Windows only)
  └── cmd/gui/main.cpp             — webview + CoreClient，HTML/CSS/JS 内联
```

## CoreClient 接口

```cpp
// include/natcore/CoreClient.h

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
};
```

## CLI 前端 (`cmd/client/main.cpp`)

- 与现有 `test_client` 完全相同 CLI 参数
- 创建 CoreCallbacks，所有回调输出到 spdlog
- 一个 `main()` 函数完成全部逻辑，无额外业务代码

## GUI 前端 (`cmd/gui/main.cpp` + `cmd/gui/index.html`)

### C++ 侧

- 创建 webview 窗口
- 构造 CoreClient，回调通过 `w.eval()` 推送给 HTML
- 通过 `w.bind()` 注册 JS 可调用的 C++ 函数（start/stop）
- 渲染循环由 webview 内部处理

### JS/HTML 侧

- 无框架，手写极简响应式状态管理（~30 行）
- 四个面板区域：
  1. **配置面板** — node-id/network-id/control/stun/relay 等输入 + 启动/停止按钮
  2. **状态面板** — 连接状态指示灯、当前模式（P2P/Relay）、RTT、在线节点列表
  3. **TUN 面板** — 虚拟 IP、网关、子网信息
  4. **日志面板** — 滚动文本日志输出

### JS ↔ C++ 通信

| 方向 | 方式 | 用途 |
|------|------|------|
| C++ → JS | `w.eval("fn(arg)")` | 推送日志、状态变更、节点上线、TUN 信息 |
| JS → C++ | `w.bind("fn", handler)` | 启动客户端、停止客户端、修改配置 |

## 构建系统

```cmake
add_library(natcore STATIC
    src/core/CoreClient.cpp
    src/client/TestClient.cpp
    src/client/WsClient.cpp
    src/client/UdpPuncher.cpp
    src/stun/StunServer.cpp
    src/crypto/NoiseProtocol.cpp
    ${PLATFORM_TUN_SRC}
)
target_include_directories(natcore PUBLIC include)
target_link_libraries(natcore PUBLIC ${COMMON_LIBS} PRIVATE yaml-cpp)

add_executable(natt-cli cmd/client/main.cpp)
target_link_libraries(natt-cli PRIVATE natcore)

if(WIN32)
    add_executable(natt-gui cmd/gui/main.cpp)
    target_link_libraries(natt-gui PRIVATE natcore webview)
endif()
```

- webview 库通过 FetchContent 或 git submodule 引入
- COMMON_LIBS = Boost::headers, Boost::system, OpenSSL::Crypto, OpenSSL::SSL, nlohmann_json, spdlog

## 迁移步骤

1. 目录重命名 `cmd/test_client/` → `cmd/client/`
2. 提取 `src/core/CoreClient.h/.cpp`，将 TestClient 封装为 CoreClient 的内部实现
3. 精简 `cmd/client/main.cpp` 为 CoreClient 薄封装
4. 新建 `cmd/gui/main.cpp` + `cmd/gui/index.html`
5. 更新 CMakeLists.txt 添加 natcore/natt-cli/natt-gui 目标
6. 验证 CLI 功能与现有 test_client 完全一致
