#include "util/uuid.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include <chrono>

namespace util {

std::string generate_uuid() {
    // Use multiple entropy sources to ensure uniqueness
    static std::mt19937_64 gen(
        std::chrono::steady_clock::now().time_since_epoch().count() ^
        reinterpret_cast<uintptr_t>(&gen) +
        []() {
            std::random_device rd;
            return rd();
        }()
    );

    uint64_t r1 = gen();
    uint64_t r2 = gen();

    // UUID version 4
    r1 &= 0xFFFFFFFFFFFF0FFFull;
    r1 |= 0x0000000000004000ull;
    r2 &= 0x3FFFFFFFFFFFFFFFull;
    r2 |= 0x8000000000000000ull;

    auto hex8 = [](uint64_t v, int shift) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(8) << (uint32_t)(v >> shift);
        return oss.str();
    };
    auto hex4 = [](uint64_t v, int shift) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(4) << (uint16_t)(v >> shift);
        return oss.str();
    };

    // Standard UUID format: 8-4-4-4-12
    std::ostringstream oss;
    oss << hex8(r1, 32) << "-"
        << hex4(r1, 16) << "-"
        << hex4(r1, 0) << "-"
        << hex4(r2, 48) << "-"
        << hex4(r2, 32)
        << hex8(r2, 16);

    return oss.str();
}

} // namespace util
