# ── Conan (all dependencies) ───────────────────────────────
# All third-party libraries are managed via Conan Center.
# Conan installs them to its cache and generates CMake config
# files under ${CMAKE_BINARY_DIR}/.
#
# First configure:  conan install . -of=build --build=missing
# Subsequent:       cmake -B build -DCMAKE_BUILD_TYPE=Release

if(NOT EXISTS "${CMAKE_BINARY_DIR}/OpenSSLConfig.cmake" AND
   NOT EXISTS "${CMAKE_BINARY_DIR}/generators/OpenSSLConfig.cmake")
    find_program(CONAN_CMD conan REQUIRED)
    message(STATUS "Conan: installing dependencies...")
    execute_process(
        COMMAND ${CONAN_CMD} install -of "${CMAKE_BINARY_DIR}"
                "${CMAKE_SOURCE_DIR}" --build=missing
                -s compiler.cppstd=23
        RESULT_VARIABLE _conan_result
    )
    if(NOT _conan_result EQUAL 0)
        message(FATAL_ERROR "\nconan install failed. See https://conan.io/downloads\n")
    endif()
endif()

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/generators")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/generators")

find_package(OpenSSL       REQUIRED CONFIG)
find_package(Boost         REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(spdlog        REQUIRED CONFIG)
find_package(yaml-cpp      REQUIRED CONFIG)
find_package(SQLite3       REQUIRED CONFIG)

message(STATUS "Conan: OpenSSL ${OpenSSL_VERSION}, Boost ${Boost_VERSION}")
message(STATUS "Conan: nlohmann_json ${nlohmann_json_VERSION}, spdlog ${spdlog_VERSION}")
message(STATUS "Conan: yaml-cpp ${yaml-cpp_VERSION}, SQLite3 ${SQLite3_VERSION}")

# ── Qt6 (GUI) ────────────────────────────────────────────────
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
    message(STATUS "Qt6: ${Qt6_VERSION}")
endif()

# ── MSYS2 / MinGW cmake config path hint ─────────────────────
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
