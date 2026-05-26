cmake_minimum_required(VERSION 3.20)
project(natmesh-server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Dependencies ──────────────────────────────────────────────
include(cmake/dependencies.cmake)

# ── Sources ──────────────────────────────────────────────────
set(SOURCES
    cmd/server/main.cpp
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
)

set(HEADERS
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
)

# ── Executable ───────────────────────────────────────────────
add_executable(natmesh-server ${SOURCES} ${HEADERS})
target_include_directories(natmesh-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(natmesh-server PRIVATE
    Boost::headers
    Boost::system
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    yaml-cpp
    sqlite3
)

# ── Compiler options ─────────────────────────────────────────
target_compile_options(natmesh-server PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)


