#!/usr/bin/env bash
# ================================================================
# NATMesh Ubuntu 编译脚本
# 自动安装依赖 + 编译全部目标 (natmesh-server, stun_server,
# test_client, relay_server)
#
# 用法:
#   chmod +x build-ubuntu.sh
#   ./build-ubuntu.sh                    # Debug 构建
#   ./build-ubuntu.sh release            # Release 构建
#   ./build-ubuntu.sh debug              # Debug 构建 (默认)
#   ./build-ubuntu.sh clean              # 清理构建目录
# ================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ── 检测系统 ─────────────────────────────────────────────────
detect_os() {
    if [ ! -f /etc/os-release ]; then
        log_error "仅支持 Ubuntu/Debian 系统"
        exit 1
    fi
    . /etc/os-release
    if [ "$ID" != "ubuntu" ] && [ "$ID" != "debian" ]; then
        log_error "仅支持 Ubuntu/Debian，当前系统: $ID"
        exit 1
    fi
    log_info "系统: $PRETTY_NAME"
}

# ── 安装系统依赖 ────────────────────────────────────────────
install_deps() {
    log_info "更新软件包索引..."
    sudo apt-get update -qq

    log_info "安装编译工具链..."
    sudo apt-get install -y -qq \
        build-essential \
        cmake \
        git \
        pkg-config \
        g++ 2>&1 | tail -1

    log_info "安装 Boost 库..."
    sudo apt-get install -y -qq \
        libboost-dev \
        libboost-system-dev 2>&1 | tail -1

    log_info "安装 yaml-cpp (配置文件解析)..."
    sudo apt-get install -y -qq \
        libyaml-cpp-dev 2>&1 | tail -1

    log_info "安装 SQLite3 (数据库)..."
    sudo apt-get install -y -qq \
        libsqlite3-dev 2>&1 | tail -1

    log_info "安装 nlohmann-json (JSON 解析)..."
    sudo apt-get install -y -qq \
        nlohmann-json3-dev 2>&1 | tail -1

    log_info "安装 spdlog (日志库)..."
    sudo apt-get install -y -qq \
        libspdlog-dev \
        libfmt-dev 2>&1 | tail -1

    log_ok "系统依赖安装完成"
}

# ── 配置 CMake ───────────────────────────────────────────────
configure_cmake() {
    local build_type="$1"
    local build_dir="$2"

    log_info "配置 CMake (${build_type})..."

    # ── 选项说明 ─────────────────────────────────────────────
    # CMAKE_BUILD_TYPE       Debug / Release
    # CMAKE_EXPORT_COMPILE_COMMANDS  生成 compile_commands.json (IDE 补全)
    # -DCMAKE_CXX_FLAGS      额外的编译标志

    cmake -S "${PROJECT_DIR}" -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DCMAKE_CXX_STANDARD=20 \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON

    log_ok "CMake 配置完成: ${build_dir}"
}

# ── 编译全部目标 ────────────────────────────────────────────
build_all() {
    local build_dir="$1"

    log_info "编译全部目标 (单线程)..."
    cmake --build "${build_dir}" \
        --target natmesh-server stun_server test_client relay_server

    log_info ""
    log_info "═══════════════════════════════════════════"
    log_ok  "编译成功！"
    log_info "生成的可执行文件:"
    log_info "  ${build_dir}/natmesh-server     (控制服务器)"
    log_info "  ${build_dir}/stun_server         (STUN 服务)"
    log_info "  ${build_dir}/test_client         (测试客户端)"
    log_info "  ${build_dir}/relay_server        (中继服务器)"
    log_info "═══════════════════════════════════════════"

    echo ""
    ls -lh "${build_dir}"/natmesh-server \
          "${build_dir}"/stun_server \
          "${build_dir}"/test_client \
          "${build_dir}"/relay_server 2>/dev/null \
        | awk '{print "  " $NF "  (" $5 ")"}'
}

# ── 清理 ─────────────────────────────────────────────────────
clean_build() {
    local dirs=("build" "build-debug" "build-release")
    for d in "${dirs[@]}"; do
        if [ -d "${PROJECT_DIR}/${d}" ]; then
            log_info "清理 ${d}..."
            rm -rf "${PROJECT_DIR:?}/${d}"
        fi
    done
    log_ok "构建目录已清理"
}

# ── 打印环境信息 ────────────────────────────────────────────
print_env() {
    echo ""
    log_info "环境信息:"
    echo "  g++:   $(g++ --version 2>/dev/null | head -1 || echo '未安装')"
    echo "  cmake: $(cmake --version 2>/dev/null | head -1 || echo '未安装')"
    echo "  make:  $(make --version 2>/dev/null | head -1 || echo '未安装')"

    # 检查 Boost
    if dpkg -l libboost-dev &>/dev/null 2>&1; then
        echo "  Boost: $(dpkg -l libboost-dev | tail -1 | awk '{print $3}')"
    else
        echo "  Boost: 未安装"
    fi

    # 检查关键头文件
    local checks=(
        "/usr/include/nlohmann/json.hpp"
        "/usr/include/spdlog/spdlog.h"
        "/usr/include/yaml-cpp/yaml.h"
        "/usr/include/sqlite3.h"
    )
    for header in "${checks[@]}"; do
        if [ -f "$header" ]; then
            echo "  $(basename $(dirname $header)): 已安装"
        fi
    done
    echo ""
}

# ── 主流程 ──────────────────────────────────────────────────
main() {
    local mode="${1:-debug}"
    local build_type="Debug"
    local build_dir="build-debug"

    case "${mode}" in
        release|Release)
            build_type="Release"
            build_dir="build"
            ;;
        debug|Debug)
            build_type="Debug"
            build_dir="build-debug"
            ;;
        clean)
            clean_build
            exit 0
            ;;
        --help|-h)
            echo "用法: $0 [debug|release|clean]"
            echo ""
            echo "  debug   (默认) Debug 构建，含调试符号"
            echo "  release         Release 构建，O3 优化"
            echo "  clean           清理构建目录"
            exit 0
            ;;
        *)
            log_error "未知模式: ${mode}"
            echo "用法: $0 [debug|release|clean]"
            exit 1
            ;;
    esac

    echo ""
    echo "╔═══════════════════════════════════════════╗"
    echo "║      NATMesh Ubuntu Build Script          ║"
    echo "╚═══════════════════════════════════════════╝"
    echo ""

    detect_os
    print_env

    echo ""
    echo "────────────────────────────────────────────"
    log_info "模式: ${build_type}"
    echo "────────────────────────────────────────────"
    echo ""

    install_deps
    configure_cmake "${build_type}" "${build_dir}"
    build_all "${build_dir}"

    echo ""
    log_ok "全部完成！运行方式:"
    echo ""
    echo "  # 1. 启动控制服务器"
    echo "  mkdir -p data"
    echo "  ./${build_dir}/natmesh-server config.yaml"
    echo ""
    echo "  # 2. 启动 STUN 服务"
    echo "  ./${build_dir}/stun_server --listen 0.0.0.0 --port 3478"
    echo ""
    echo "  # 3. 启动 Relay 中继"
    echo "  ./${build_dir}/relay_server --port 7000"
    echo ""
    echo "  # 4. 启动测试客户端 (节点 A)"
    echo "  ./${build_dir}/test_client \\"
    echo "    --node-id node-a \\"
    echo "    --network-id home \\"
    echo "    --control ws://127.0.0.1:8080/ws \\"
    echo "    --stun 127.0.0.1:3478 \\"
    echo "    --relay 127.0.0.1:7000 \\"
    echo "    --udp-port 40001 \\"
    echo "    --connect node-b \\"
    echo "    --tun"
    echo ""
}

main "$@"
