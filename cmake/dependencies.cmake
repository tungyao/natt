include(FetchContent)

# ── Boost (requires system-installed) ──────────────────────────
# We use find_package for Boost since FetchContent takes too long.
# Install: apt install libboost-all-dev (Linux)
find_package(Boost REQUIRED COMPONENTS system)
message(STATUS "Boost found: ${Boost_VERSION}")

# ── nlohmann_json ──────────────────────────────────────────────
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
message(STATUS "nlohmann_json: ${nlohmann_json_SOURCE_DIR}")

# ── spdlog ─────────────────────────────────────────────────────
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)
message(STATUS "spdlog: ${spdlog_SOURCE_DIR}")

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
