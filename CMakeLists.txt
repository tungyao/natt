cmake_minimum_required(VERSION 3.20)
project(natmesh-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Dependencies ──────────────────────────────────────────────
include(cmake/dependencies.cmake)

# ── Library sources (shared between executables) ─────────────
set(LIB_SOURCES
    src/config/config.cpp
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
    src/nat/session_manager.cpp
    src/nat/node_registry.cpp
    src/nat/message_dispatcher.cpp
    src/nat/heartbeat_monitor.cpp
    src/stun/StunServer.cpp
    src/client/WsClient.cpp
    src/client/UdpPuncher.cpp
    src/client/TestClient.cpp
)

set(LIB_HEADERS
    src/config/config.h
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
    src/nat/session_manager.h
    src/nat/node_registry.h
    src/nat/message_dispatcher.h
    src/nat/heartbeat_monitor.h
    src/stun/StunServer.h
    src/client/WsClient.h
    src/client/UdpPuncher.h
    src/client/TestClient.h
)

# ── Common include + link ────────────────────────────────────
set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(COMMON_LIBS
    Boost::headers
    Boost::system
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# ── natmesh-server (with DB, yaml-cpp) ──────────────────────
set(SERVER_SOURCES ${LIB_SOURCES})  # copy
list(REMOVE_ITEM SERVER_SOURCES
    src/client/WsClient.cpp
    src/client/UdpPuncher.cpp
    src/client/TestClient.cpp
    src/stun/StunServer.cpp
)
list(APPEND SERVER_SOURCES cmd/server/main.cpp)

add_executable(natmesh-server ${SERVER_SOURCES} ${LIB_HEADERS})
target_include_directories(natmesh-server PRIVATE ${COMMON_INCLUDES})
target_link_libraries(natmesh-server PRIVATE ${COMMON_LIBS} yaml-cpp sqlite3)
target_compile_options(natmesh-server PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ── stun_server ──────────────────────────────────────────────
set(STUN_SOURCES
    cmd/stun_server/main.cpp
    src/stun/StunServer.cpp
)

add_executable(stun_server ${STUN_SOURCES} src/stun/StunServer.h)
target_include_directories(stun_server PRIVATE ${COMMON_INCLUDES})
target_link_libraries(stun_server PRIVATE ${COMMON_LIBS})
target_compile_options(stun_server PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ── test_client ──────────────────────────────────────────────
set(CLIENT_SOURCES
    cmd/test_client/main.cpp
    src/client/WsClient.cpp
    src/client/UdpPuncher.cpp
    src/client/TestClient.cpp
    src/stun/StunServer.cpp
)

# Need StunServer.h for STUN query (included from TestClient)
add_executable(test_client ${CLIENT_SOURCES}
    src/client/WsClient.h
    src/client/UdpPuncher.h
    src/client/TestClient.h
    src/stun/StunServer.h
)
target_include_directories(test_client PRIVATE ${COMMON_INCLUDES})
target_link_libraries(test_client PRIVATE ${COMMON_LIBS})
target_compile_options(test_client PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
