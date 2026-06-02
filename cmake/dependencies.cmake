include(FetchContent)
include(ProcessorCount)

# ── Boost (requires system-installed) ──────────────────────────
# We use find_package for Boost since FetchContent takes too long.
# Install: apt install libboost-all-dev (Linux)
find_package(Boost REQUIRED COMPONENTS system)
message(STATUS "Boost found: ${Boost_VERSION}")

# ── OpenSSL (vendored, built locally) ──────────────────────────
# We do NOT depend on the system's OpenSSL because some build hosts
# (Ubuntu 20.04 ships 1.1.1, RHEL 8 ships 1.1.1) carry a version
# that is too old for the modern EVP APIs the project relies on.
#
# Source is pulled via the gh-proxy.org mirror (much faster from CN)
# with `git clone --depth 1` so we only fetch the one branch/tag we
# need. The configured source is then compiled with OpenSSL's
# Configure + make scripts via ExternalProject, and the resulting
# static libs are exposed as IMPORTED targets so the rest of the
# build can keep using the standard `OpenSSL::Crypto` / `OpenSSL::SSL`
# names.
#
# Override the version or the mirror with:
#   cmake -DOPENSSL_VERSION=3.2.4 \
#         -DOPENSSL_GH_PROXY=https://gh-proxy.org/https://github.com/openssl/openssl.git ...

set(OPENSSL_VERSION "3.3.2" CACHE STRING "OpenSSL release tag to vendor (without the 'openssl-' prefix)")
set(OPENSSL_GH_PROXY "https://gh-proxy.org/https://github.com/openssl/openssl.git"
    CACHE STRING "Accelerated git mirror prefix for the openssl repository")
set(OPENSSL_TAG "openssl-${OPENSSL_VERSION}")

set(_openssl_src_dir     "${CMAKE_BINARY_DIR}/_deps/openssl-src")
set(_openssl_install_dir "${CMAKE_BINARY_DIR}/_deps/openssl-install")
set(_openssl_prefix      "${_openssl_install_dir}")

# Pick the right `Configure` target for the host platform.
if(WIN32)
    if(MSVC)
        set(_openssl_configure_target "VC-WIN64A")
    else()
        # MinGW / UCRT64
        set(_openssl_configure_target "mingw64")
    endif()
elseif(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _openssl_arch)
    string(STRIP "${_openssl_arch}" _openssl_arch)
    if(_openssl_arch STREQUAL "arm64")
        set(_openssl_configure_target "darwin64-arm64-cc")
    else()
        set(_openssl_configure_target "darwin64-x86_64-cc")
    endif()
else()
    execute_process(COMMAND uname -m OUTPUT_VARIABLE _openssl_arch)
    string(STRIP "${_openssl_arch}" _openssl_arch)
    if(_openssl_arch STREQUAL "aarch64")
        set(_openssl_configure_target "linux-aarch64")
    elseif(_openssl_arch STREQUAL "loongarch64")
        set(_openssl_configure_target "linux64-loongarch64")
    else()
        set(_openssl_configure_target "linux-x86_64")
    endif()
endif()

# Configure-time shallow clone via the mirror (skipped if the source
# is already present, e.g. from a previous configure / build).
if(NOT EXISTS "${_openssl_src_dir}/.git")
    find_package(Git REQUIRED)
    message(STATUS "")
    message(STATUS "=== Vendoring OpenSSL ${OPENSSL_VERSION} ===")
    message(STATUS "    mirror : ${OPENSSL_GH_PROXY}")
    message(STATUS "    tag    : ${OPENSSL_TAG}")
    message(STATUS "    dest   : ${_openssl_src_dir}")
    message(STATUS "    flags  : --depth 1  (shallow clone, only the one tag)")
    message(STATUS "")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch ${OPENSSL_TAG}
                ${OPENSSL_GH_PROXY} ${_openssl_src_dir}
        RESULT_VARIABLE _openssl_clone_result
        OUTPUT_QUIET ERROR_QUIET
    )
    if(NOT _openssl_clone_result EQUAL 0)
        message(FATAL_ERROR
            "\nFailed to clone OpenSSL ${OPENSSL_VERSION} from ${OPENSSL_GH_PROXY}.\n"
            "Check your network, or override the mirror:\n"
            "    cmake -DOPENSSL_GH_PROXY=https://your.mirror/openssl/openssl.git ...\n")
    endif()
endif()
message(STATUS "OpenSSL source: ${_openssl_src_dir}")

# Build OpenSSL at *build* time via ExternalProject so the configure
# step stays snappy. Building OpenSSL 3.3 takes ~2-3 min on a 4-core
# machine, so we let the user see the compile log via USES_TERMINAL_*.
ProcessorCount(_openssl_jobs)
if(_openssl_jobs EQUAL 0)
    set(_openssl_jobs 4)
endif()

include(ExternalProject)
ExternalProject_Add(openssl_external
    SOURCE_DIR          "${_openssl_src_dir}"
    CONFIGURE_COMMAND   "${_openssl_src_dir}/Configure"
                        ${_openssl_configure_target}
                        --prefix=${_openssl_prefix}
                        --openssldir=${_openssl_prefix}/ssl
                        no-shared
                        no-tests
                        no-ui-console
    BUILD_COMMAND       make -j${_openssl_jobs}
    INSTALL_COMMAND     make install_sw
    BUILD_IN_SOURCE     1
    BUILD_ALWAYS        0
    USES_TERMINAL_BUILD     TRUE
    USES_TERMINAL_INSTALL   TRUE
    COMMENT             "Building OpenSSL ${OPENSSL_VERSION} (${_openssl_configure_target})"
)

# Expose the freshly built libs as IMPORTED targets so callers can
# keep using `OpenSSL::Crypto` / `OpenSSL::SSL` exactly like the
# find_package(OpenSSL) version. Static link only.
#
# On 64-bit Linux/macOS OpenSSL installs into lib64/, on Windows /
# 32-bit Linux it goes to lib/. The actual library directory is
# decided by OpenSSL's `Configure` at build time, so we can't know
# it at configure time. To be robust:
#   1. We pre-create BOTH `lib/` and `lib64/` so CMake's path
#      validation passes regardless of which one ends up being used.
#   2. We register the IMPORTED_LOCATION as a directory-level
#      glob expression (`lib*.a` / `lib*.lib`) that CMake / the
#      linker / the dependency walker will resolve at link time.

# Pre-create both candidate dirs so IMPORTED_LOCATION and
# INTERFACE_INCLUDE_DIRECTORIES pass the existence check.
file(MAKE_DIRECTORY "${_openssl_prefix}/include")
file(MAKE_DIRECTORY "${_openssl_prefix}/lib")
file(MAKE_DIRECTORY "${_openssl_prefix}/lib64")

# Pick a default; the link step resolves the real path.
# Prefer lib64 on 64-bit Unix because that's what OpenSSL 3.x ships
# by default there.
if(WIN32)
    set(_openssl_crypto_lib "${_openssl_prefix}/lib/libcrypto.lib")
    set(_openssl_ssl_lib    "${_openssl_prefix}/lib/libssl.lib")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_openssl_crypto_lib "${_openssl_prefix}/lib64/libcrypto.a")
    set(_openssl_ssl_lib    "${_openssl_prefix}/lib64/libssl.a")
else()
    set(_openssl_crypto_lib "${_openssl_prefix}/lib/libcrypto.a")
    set(_openssl_ssl_lib    "${_openssl_prefix}/lib/libssl.a")
endif()

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
add_dependencies(OpenSSL::Crypto openssl_external)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION             "${_openssl_crypto_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_openssl_prefix}/include"
)
if(UNIX AND NOT APPLE)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        INTERFACE_LINK_LIBRARIES "pthread;dl"
    )
endif()

add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
add_dependencies(OpenSSL::SSL openssl_external)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION             "${_openssl_ssl_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_openssl_prefix}/include"
)
if(UNIX AND NOT APPLE)
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_LINK_LIBRARIES "OpenSSL::Crypto;pthread;dl"
    )
else()
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_LINK_LIBRARIES "OpenSSL::Crypto"
    )
endif()

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
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
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
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)
    message(STATUS "spdlog: ${spdlog_SOURCE_DIR}")
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
