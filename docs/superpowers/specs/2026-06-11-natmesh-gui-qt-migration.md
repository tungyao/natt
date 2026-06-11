# NATMesh GUI: WebView → Qt Widgets Migration

## 概述

将 `natt-gui` 桌面客户端从 webview (HTML/CSS/JS) 迁移到 Qt 6 Widgets (C++)，解决 webview 方案调试困难、运行时依赖重的问题。

## 动机

| 问题 | 现状 | Qt 方案 |
|------|------|---------|
| 调试困难 | JS↔C++ 双向 bridge，断点不连续 | 纯 C++，gdb/lldb/VS 直接断点 |
| 构建依赖重 | FetchContent 拉取 webview + WebKitGTK 系统依赖 | Conan qt/6.11.1，项目已在用 Conan |
| 运行环境 | Linux 需安装 WebKitGTK，Windows 需 WebView2 Runtime | Qt 动态/静态链接，无额外运行时 |
| 维护成本 | HTML+CSS+JS 嵌入二进制，修改→构建→运行周期长 | 纯 C++，IDE 重构/补全/跳转完整支持 |

## 架构

```
natcore (static library)  ← 不变
  └── CoreClient (CoreCallbacks)

natt-gui (executable)
  └── cmd/gui/main.cpp  ← 重写为 Qt Widgets
       ├── QMainWindow (主窗口, 960×720)
       │   ├── 配置面板 (QFormLayout + QLineEdit + QCheckBox + QPushButton)
       │   ├── 状态面板 (QLabel + QListWidget)
       │   ├── TUN 面板 (QLabel)
       │   └── 日志面板 (QPlainTextEdit)
       └── CoreClient (CoreCallbacks → 直接更新 Qt 控件)
```

## UI 布局

与现有 webview 版本保持一致的 4 面板 2×2 布局：

```
┌─────────────────────────────────────┐
│ [状态指示灯] NATT Client  [Stopped] │  ← QMenuBar/自定义标题栏
├────────────────┬────────────────────┤
│ Configuration  │ Status             │
│ ┌────────────┐ │ Mode: —            │
│ │ Node ID    │ │ RTT: —             │
│ │ Network ID │ │ P2P: —             │
│ │ Control URL│ │ Uptime: —          │
│ │ ...        │ │ Peers: [列表]      │
│ │ [Start] [Stop]│                   │
│ └────────────┘ │                   │
├────────────────┼────────────────────┤
│ TUN Interface  │ Log                │
│ VIP: —         │ ┌──────────────┐   │
│ GW: —          │ │ 滚动日志     │   │
│ Subnet: —      │ │ [info] ...   │   │
│                │ │ [warn] ...   │   │
│                │ └──────────────┘   │
└────────────────┴────────────────────┘
```

## CoreClient 集成

CoreClient 和 CoreCallbacks 接口 **完全不变**，只改 UI 层：

```cpp
// 回调直接更新 Qt 控件，无需序列化/JS eval
CoreCallbacks cbs;
cbs.on_log = [this](const std::string& line) {
    // QPlainTextEdit::appendPlainText + 颜色标记
};
cbs.on_state_change = [this](const std::string& state) {
    // 更新状态指示灯和标签
};
cbs.on_peer_online = [this](const std::string& node_id,
                            const std::string& ip, uint16_t port) {
    // QListWidget::addItem
};
cbs.on_punch_success = [this]() {
    // 更新 P2P 状态文字
};
cbs.on_error = [this](const std::string& msg) {
    // 日志 + 状态灯变红
};
cbs.on_virtual_ip_assigned = [this](const std::string& vip,
                                    const std::string& gw,
                                    const std::string& subnet) {
    // 显示 TUN 信息
};
```

## 构建变更

### conanfile.txt

```diff
 [requires]
+qt/6.11.1
```

### cmake/dependencies.cmake

```diff
- if(BUILD_GUI)
-     FetchContent_Declare(webview ...)
-     FetchContent_MakeAvailable(webview)
- endif()
+ if(BUILD_GUI)
+     find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
+ endif()
```

### CMakeLists.txt

```diff
 if(BUILD_GUI)
-    set(_gui_gen_dir ...)
-    set(_gui_hdr ...)
-    file(READ ...)   # 删除 embedded_html.h 生成
-    add_executable(natt-gui cmd/gui/main.cpp)
-    target_include_directories(natt-gui PRIVATE "${_gui_gen_dir}")
-    target_link_libraries(natt-gui PRIVATE natcore webview::core_static)
+    add_executable(natt-gui cmd/gui/main.cpp)
+    target_link_libraries(natt-gui PRIVATE natcore Qt6::Core Qt6::Widgets)
 endif()
```

### 删除的文件

- `cmd/gui/index.html` — 不再需要，UI 由 C++ Qt Widgets 渲染

## 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `cmd/gui/main.cpp` | 重写 | webview 代码替换为 Qt Widgets |
| `cmd/gui/index.html` | 删除 | 不再需要 |
| `conanfile.txt` | 修改 | 添加 `qt/6.11.1` |
| `cmake/dependencies.cmake` | 修改 | webview FetchContent → Qt6 find_package |
| `CMakeLists.txt` | 修改 | natt-gui 构建规则 + setup_target 宏复用 |
| `docs/superpowers/specs/2026-06-04-natmesh-gui-design.md` | 不变 | 旧设计保留为历史参考 |

## 测试

- `natt-gui` 能正常编译链接（Linux + Windows）
- `natt-cli` 不受影响（独立 target）
- `natmesh-server` 不受影响
- GUI 功能与 webview 版本一致：配置输入 → 启动客户端 → 日志/状态更新 → 停止
