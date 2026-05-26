#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdint>

/// IP Address Allocator.
///
/// Manages a pool of IPv4 addresses within a subnet.
/// Thread-safe. In-memory allocation with optional DB persistence.
///
class IpAllocator {
public:
    IpAllocator() = default;

    /// Initialize a subnet pool for the given network.
    /// @param network_id  Network identifier
    /// @param cidr        Subnet in CIDR format, e.g. "10.0.0.0/16"
    /// @param reserved    Already-allocated IPs to mark as used (from DB)
    void addPool(const std::string& network_id,
                 const std::string& cidr,
                 const std::unordered_set<std::string>& reserved = {});

    /// Allocate the next available IP for a network.
    /// Returns the IP string, or empty string if pool exhausted.
    std::string allocate(const std::string& network_id);

    /// Free a specific IP back to the pool.
    /// @return true if the IP was found and freed
    bool free(const std::string& network_id, const std::string& ip);

    /// Check if an IP is already allocated.
    bool isAllocated(const std::string& network_id, const std::string& ip) const;

    /// Get the subnet CIDR for a network.
    std::string getSubnet(const std::string& network_id) const;

    /// Get the subnet prefix length for a network.
    int getPrefix(const std::string& network_id) const;

    /// Number of allocated IPs in a network.
    size_t allocatedCount(const std::string& network_id) const;

    /// Parse a CIDR string like "10.0.0.0/16" into base IP and prefix.
    /// Returns true on success.
    static bool parseCidr(const std::string& cidr,
                          uint32_t& base_addr,
                          int& prefix);

    /// Convert an IP string to uint32 (network byte order).
    static uint32_t ipToUint(const std::string& ip);

    /// Convert uint32 to IP string.
    static std::string uintToIp(uint32_t addr);

    /// Get the network address (base) from CIDR.
    static uint32_t networkAddr(uint32_t addr, int prefix);

    /// Calculate broadcast address.
    static uint32_t broadcastAddr(uint32_t addr, int prefix);

private:
    struct Pool {
        uint32_t base_addr = 0;
        uint32_t start_addr = 0;   // first allocatable (base + 1)
        uint32_t end_addr = 0;     // last allocatable (broadcast - 1)
        int prefix = 24;
        std::unordered_set<uint32_t> allocated;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Pool> pools_;
};
