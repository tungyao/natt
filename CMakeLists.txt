cmake_minimum_required(VERSION 3.20)
project(natmesh-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Platform-specific setup ───────────────────────────────────
if(WIN32)
    set(PLATFORM_TUN_SRC src/tun/TunWindows.cpp)
    set(PLATFORM_LIBS ws2_32 iphlpapi mswsock)
    add_compile_definitions(_WIN32_WINNT=0x0601)  # Windows 7+ for netioapi/ws2tcpip
    if(NOT MSVC)
        # MinGW: full static link to embed all runtime (libgcc, libstdc++,
        # winpthread, libmingwex) into the exe, avoiding dependency on
        # MSYS2 DLLs and old UCRT functions like nanosleep64/clock_gettime64.
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
else()
    set(PLATFORM_TUN_SRC src/tun/TunLinux.cpp)
    set(PLATFORM_LIBS)
endif()

# ── Dependencies ──────────────────────────────────────────────
include(cmake/dependencies.cmake)

# ── Common library sources ────────────────────────────────────
set(LIB_SOURCES
    src/config/config.cpp
    src/crypto/NoiseProtocol.cpp
    src/crypto/CertGenerator.cpp
    src/repository/database.cpp
    src/repository/user_repo.cpp
    src/repository/device_repo.cpp
    src/repository/network_repo.cpp
    src/auth/jwt.cpp
    src/util/uuid.cpp
    src/service/user_service.cpp
    src/service/device_service.cpp
    src/service/network_service.cpp
    src/service/peer_manager.cpp
    src/http/http_server.cpp
    src/ws/ws_session.cpp
    src/ws/wss_session.cpp
    src/nat/session_manager.cpp
    src/nat/node_registry.cpp
    src/nat/message_dispatcher.cpp
    src/nat/heartbeat_monitor.cpp
)

set(LIB_HEADERS
    src/config/config.h
    src/crypto/NoiseProtocol.h
    src/crypto/CertGenerator.h
    src/model/user.h
    src/model/device.h
    src/model/network.h
    src/repository/database.h
    src/repository/user_repo.h
    src/repository/device_repo.h
    src/repository/network_repo.h
    src/auth/jwt.h
    src/util/uuid.h
    src/service/user_service.h
    src/service/device_service.h
    src/service/network_service.h
    src/service/peer_manager.h
    src/http/http_server.h
    src/ws/ws_session.h
    src/ws/wss_session.h
    src/nat/session_manager.h
    src/nat/node_registry.h
    src/nat/message_dispatcher.h
    src/nat/heartbeat_monitor.h
)

# ── Common include + link ────────────────────────────────────
set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(COMMON_LIBS
    Boost::headers
    Boost::system
    OpenSSL::Crypto
    OpenSSL::SSL
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ${PLATFORM_LIBS}
)

macro(setup_target TARGET)
    target_include_directories(${TARGET} PRIVATE ${COMMON_INCLUDES})
    target_link_libraries(${TARGET} PRIVATE ${COMMON_LIBS})
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endmacro()

# ── natmesh-server (with DB, yaml-cpp) ──────────────────────
add_executable(natmesh-server
    ${LIB_SOURCES}
    src/stun/StunServer.cpp
    src/relay/RelayRegistry.cpp
    src/relay/RelayServer.cpp
    src/ipam/IpAllocator.cpp
    ${PLATFORM_TUN_SRC}
    cmd/server/main.cpp
    ${LIB_HEADERS}
)
setup_target(natmesh-server)
target_link_libraries(natmesh-server PRIVATE yaml-cpp SQLite::SQLite3)

# ── stun_server ──────────────────────────────────────────────
add_executable(stun_server
    cmd/stun_server/main.cpp
    src/stun/StunServer.cpp
)
setup_target(stun_server)

# ── natcore (static library) ──────────────────────────────────
add_library(natcore STATIC
    src/core/CoreClient.cpp
    src/client/TestClient.cpp
    src/client/WsClient.cpp
    src/client/UdpPuncher.cpp
    src/stun/StunServer.cpp
    src/crypto/NoiseProtocol.cpp
    ${PLATFORM_TUN_SRC}
)
target_include_directories(natcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(natcore PUBLIC ${COMMON_LIBS} PRIVATE yaml-cpp)

# ── natt-cli ──────────────────────────────────────────────────
add_executable(natt-cli cmd/client/main.cpp)
target_link_libraries(natt-cli PRIVATE natcore yaml-cpp)
target_compile_options(natt-cli PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ── natt-gui ────────────────────────────────────────────────────

# Embed index.html as a hex array directly into the exe binary.
# Uses a header file with inline function so no extra .cpp compilation needed.
set(_gui_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_gui_hdr "${_gui_gen_dir}/embedded_html.h")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmd/gui/index.html" _gui_hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," _gui_arr "${_gui_hex}")
file(WRITE "${_gui_hdr}"
    "#pragma once\n"
    "#include <string>\n"
    "inline const std::string& embedded_html() {\n"
    "    static const unsigned char d[] = { ${_gui_arr} };\n"
    "    static const std::string s(reinterpret_cast<const char*>(d), sizeof(d));\n"
    "    return s;\n"
    "}\n"
)

add_executable(natt-gui cmd/gui/main.cpp)
target_include_directories(natt-gui PRIVATE "${_gui_gen_dir}")
target_link_libraries(natt-gui PRIVATE natcore webview::core_static)

# ── relay_server ─────────────────────────────────────────────
add_executable(relay_server
    cmd/relay_server/main.cpp
    src/relay/RelayRegistry.cpp
    src/relay/RelayServer.cpp
)
setup_target(relay_server)
