include(FetchContent)

# ── Conan (openssl, boost) ──────────────────────────────────
# OpenSSL and Boost are fetched from Conan Center instead of being
# vendored or requiring system packages. Conan installs them to
# its cache and generates CMake config files under
# ${CMAKE_BINARY_DIR}/.
#
# First configure:  conan install . -of=build --build=missing
# Subsequent:       cmake -B build

if(NOT EXISTS "${CMAKE_BINARY_DIR}/OpenSSLConfig.cmake")
    find_program(CONAN_CMD conan REQUIRED)
    message(STATUS "Conan: installing dependencies (openssl, boost)...")
    execute_process(
        COMMAND ${CONAN_CMD} install -of "${CMAKE_BINARY_DIR}"
                "${CMAKE_SOURCE_DIR}" --build=missing
        RESULT_VARIABLE _conan_result
    )
    if(NOT _conan_result EQUAL 0)
        message(FATAL_ERROR "\nconan install failed. Check the output above.\n"
            "See https://conan.io/downloads for installation instructions.\n")
    endif()
endif()

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")

find_package(OpenSSL REQUIRED CONFIG)
find_package(Boost  REQUIRED CONFIG)

message(STATUS "Conan: OpenSSL ${OpenSSL_VERSION}, Boost ${Boost_VERSION}")

# ── nlohmann_json ──────────────────────────────────────────────
set(NLOHMANN_JSON_LOCAL_SOURCE "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src")
if(EXISTS "${NLOHMANN_JSON_LOCAL_SOURCE}/include/nlohmann/json.hpp")
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_LOCAL_SOURCE}/include"
    )
    message(STATUS "nlohmann_json: ${NLOHMANN_JSON_LOCAL_SOURCE}")
elseif(EXISTS "${NLOHMANN_JSON_LOCAL_SOURCE}/CMakeLists.txt")
    add_subdirectory(
        "${NLOHMANN_JSON_LOCAL_SOURCE}"
        "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-build"
        EXCLUDE_FROM_ALL
    )
    message(STATUS "nlohmann_json: ${NLOHMANN_JSON_LOCAL_SOURCE}")
else()
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://gh-proxy.org/https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
	GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(STATUS "nlohmann_json: ${nlohmann_json_SOURCE_DIR}")
endif()

# ── spdlog ─────────────────────────────────────────────────────
set(SPDLOG_LOCAL_SOURCE "${CMAKE_BINARY_DIR}/_deps/spdlog-src")
if(EXISTS "${SPDLOG_LOCAL_SOURCE}/CMakeLists.txt")
    add_subdirectory(
        "${SPDLOG_LOCAL_SOURCE}"
        "${CMAKE_BINARY_DIR}/_deps/spdlog-build"
        EXCLUDE_FROM_ALL
    )
    message(STATUS "spdlog: ${SPDLOG_LOCAL_SOURCE}")
else()
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://gh-proxy.org/https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
	GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(spdlog)
    message(STATUS "spdlog: ${spdlog_SOURCE_DIR}")
endif()

# ── webview (GUI) ──────────────────────────────────────────────
FetchContent_Declare(
    webview
    GIT_REPOSITORY https://gh-proxy.org/https://github.com/webview/webview.git
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(webview)
message(STATUS "webview: ${webview_SOURCE_DIR}")

# ── MSYS2 / MinGW cmake config path hint ─────────────────────
# MSYS2 installs packages under /ucrt64/, /mingw64/, etc. — add
# those to CMAKE_PREFIX_PATH so find_package can discover them.
if(WIN32 AND NOT MSVC)
    if(DEFINED ENV{MSYSTEM_PREFIX} AND EXISTS "$ENV{MSYSTEM_PREFIX}/lib/cmake")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{MSYSTEM_PREFIX}")
    else()
        foreach(_pfx /ucrt64 /mingw64 /mingw32 /clang64)
            if(EXISTS "${_pfx}/lib/cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_pfx}")
            endif()
        endforeach()
    endif()
endif()

# ── yaml-cpp (system) ──────────────────────────────────────────
find_package(yaml-cpp REQUIRED)
message(STATUS "yaml-cpp found: ${yaml-cpp_VERSION}")

# ── SQLite3 (system) ───────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 sqlite3)
endif()
if(NOT SQLITE3_FOUND)
    find_package(SQLite3 REQUIRED)
endif()
message(STATUS "SQLite3: ${SQLite3_VERSION}")
