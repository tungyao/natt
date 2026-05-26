#pragma once

#include <boost/asio.hpp>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace net = boost::asio;
using udp = net::ip::udp;

/// Cross-platform TUN virtual network interface abstraction.
///
/// Linux:  /dev/net/tun via ioctl + boost::asio::posix::stream_descriptor
/// Windows: Wintun API DLL
///
/// Usage:
///   auto tun = TunInterface::create(ioc);
///   tun->open("nat%d", "10.0.0.2", 24);
///   tun->async_read(buf, [](ec, len) { ... });
///   tun->write(packet);
///
class TunInterface : public std::enable_shared_from_this<TunInterface> {
public:
    /// Factory: creates platform-specific instance
    static std::shared_ptr<TunInterface> create(net::io_context& ioc);

    virtual ~TunInterface() = default;

    /// Open TUN device.
    /// @param name    Device name pattern (e.g. "nat%d" → "nat0")
    /// @param ip      Virtual IP address to assign
    /// @param prefix  Subnet prefix length (e.g. 24 for 255.255.255.0)
    /// @param mtu     MTU for the interface (default 1300)
    /// @return true on success
    virtual bool open(const std::string& name,
                      const std::string& ip,
                      int prefix,
                      int mtu = 1300) = 0;

    /// Close TUN device
    virtual void close() = 0;

    /// Check if TUN is open
    virtual bool isOpen() const = 0;

    /// Get the interface name (e.g. "nat0")
    virtual std::string interfaceName() const = 0;

    /// Get the assigned virtual IP
    virtual std::string virtualIp() const = 0;

    // ── Synchronous I/O ──

    /// Read one IP packet (blocking). Returns empty vector on error.
    virtual std::vector<uint8_t> read() = 0;

    /// Write one IP packet (blocking).
    /// @return bytes written, or -1 on error
    virtual int write(const std::vector<uint8_t>& packet) = 0;

    // ── Asynchronous I/O (boost::asio integration) ──

    /// Start async read of one IP packet.
    using ReadHandler = std::function<void(boost::system::error_code, std::size_t)>;
    virtual void asyncRead(net::mutable_buffer buf, ReadHandler handler) = 0;

    /// Start async write of one IP packet.
    using WriteHandler = std::function<void(boost::system::error_code, std::size_t)>;
    virtual void asyncWrite(net::const_buffer buf, WriteHandler handler) = 0;

    // ── Route management ──

    /// Add a route to the TUN interface.
    /// @param dest    Destination network (e.g. "10.0.0.0/16")
    /// @return true on success
    virtual bool addRoute(const std::string& dest) = 0;

    /// Remove a route from the TUN interface.
    virtual bool removeRoute(const std::string& dest) = 0;
};
