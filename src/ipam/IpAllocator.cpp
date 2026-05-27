#include "ipam/IpAllocator.h"
#include <spdlog/spdlog.h>
#include <sstream>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// ── Static helpers ──────────────────────────────────────────

uint32_t IpAllocator::ipToUint(const std::string& ip) {
    struct in_addr addr{};
    inet_pton(AF_INET, ip.c_str(), &addr);
    return ntohl(addr.s_addr);
}

std::string IpAllocator::uintToIp(uint32_t addr) {
    addr = htonl(addr);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

uint32_t IpAllocator::networkAddr(uint32_t addr, int prefix) {
    if (prefix == 0) return 0;
    uint32_t mask = ~((1u << (32 - prefix)) - 1);
    return addr & mask;
}

uint32_t IpAllocator::broadcastAddr(uint32_t addr, int prefix) {
    if (prefix >= 32) return addr;
    uint32_t mask = (1u << (32 - prefix)) - 1;
    return addr | mask;
}

bool IpAllocator::parseCidr(const std::string& cidr,
                            uint32_t& base_addr,
                            int& prefix) {
    auto slash = cidr.find('/');
    if (slash == std::string::npos) {
        return false;
    }
    auto ip_str = cidr.substr(0, slash);
    auto prefix_str = cidr.substr(slash + 1);

    base_addr = ipToUint(ip_str);
    prefix = std::stoi(prefix_str);

    if (prefix < 0 || prefix > 32) return false;

    // Normalize to network address
    base_addr = networkAddr(base_addr, prefix);
    return true;
}

// ── Pool management ─────────────────────────────────────────

void IpAllocator::addPool(const std::string& network_id,
                          const std::string& cidr,
                          const std::unordered_set<std::string>& reserved) {
    std::lock_guard<std::mutex> lock(mutex_);

    Pool pool;
    uint32_t base = 0;
    if (!parseCidr(cidr, base, pool.prefix)) {
        spdlog::error("IpAllocator: invalid CIDR '{}' for network {}", cidr, network_id);
        return;
    }

    pool.base_addr = base;
    pool.start_addr = base + 2;           // skip network address and gateway (.1)
    pool.end_addr = broadcastAddr(base, pool.prefix) - 1;  // skip broadcast
    pool.allocated.insert(base + 1);

    // Mark reserved IPs as allocated
    for (const auto& ip : reserved) {
        pool.allocated.insert(ipToUint(ip));
    }

    pools_[network_id] = std::move(pool);

    uint32_t total = pool.end_addr - pool.start_addr + 1;
    spdlog::info("IpAllocator: pool for '{}': {} - {} ({} addrs, {} reserved)",
                 network_id,
                 uintToIp(pool.start_addr), uintToIp(pool.end_addr),
                 total, reserved.size());
}

std::string IpAllocator::allocate(const std::string& network_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pools_.find(network_id);
    if (it == pools_.end()) {
        spdlog::error("IpAllocator: network '{}' has no pool", network_id);
        return {};
    }

    auto& pool = it->second;

    // Scan from start_addr to end_addr for a free slot
    for (uint32_t addr = pool.start_addr; addr <= pool.end_addr; ++addr) {
        if (pool.allocated.find(addr) == pool.allocated.end()) {
            pool.allocated.insert(addr);
            std::string ip = uintToIp(addr);
            spdlog::info("IpAllocator: allocated {} for network {}", ip, network_id);
            return ip;
        }
    }

    spdlog::warn("IpAllocator: network '{}' address pool exhausted", network_id);
    return {};
}

bool IpAllocator::free(const std::string& network_id, const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pools_.find(network_id);
    if (it == pools_.end()) return false;

    uint32_t addr = ipToUint(ip);
    auto& pool = it->second;
    auto ait = pool.allocated.find(addr);
    if (ait == pool.allocated.end()) return false;

    pool.allocated.erase(ait);
    spdlog::info("IpAllocator: freed {} from network {}", ip, network_id);
    return true;
}

bool IpAllocator::isAllocated(const std::string& network_id,
                              const std::string& ip) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(network_id);
    if (it == pools_.end()) return false;
    return it->second.allocated.find(ipToUint(ip)) != it->second.allocated.end();
}

std::string IpAllocator::getSubnet(const std::string& network_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(network_id);
    if (it == pools_.end()) return {};
    return uintToIp(it->second.base_addr) + "/" + std::to_string(it->second.prefix);
}

int IpAllocator::getPrefix(const std::string& network_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(network_id);
    if (it == pools_.end()) return 0;
    return it->second.prefix;
}

std::string IpAllocator::gatewayIp(const std::string& network_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(network_id);
    if (it == pools_.end()) return {};
    return uintToIp(it->second.base_addr + 1);
}

size_t IpAllocator::allocatedCount(const std::string& network_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(network_id);
    if (it == pools_.end()) return 0;
    return it->second.allocated.size();
}
