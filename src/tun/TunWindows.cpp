/// Windows Wintun TUN implementation.
///
/// Requires:
///   1. Download wintun.dll from https://www.wintun.net/
///      Place in the same directory as the executable.
///   2. Include wintun.h header (from Wintun SDK)
///   3. Compile with -DWINTUN_ENABLED on Windows
///
/// This file is a compile-ready skeleton. The full implementation
/// requires the Wintun DLL at runtime and the wintun.h header.
///
/// Build (Windows MSVC):
///   cl /EHsc /DWINTUN_ENABLED /I<wintun-sdk> ... /link wintun.lib
///
/// Build (Windows MinGW):
///   g++ -DWINTUN_ENABLED -I<wintun-sdk> ... -lwintun

#include "tun/TunInterface.h"
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#endif

// ── Wintun stub (only compiled when WINTUN_ENABLED is defined) ──

#ifdef WINTUN_ENABLED
#include <wintun.h>
#endif

class TunWindows : public TunInterface {
public:
    explicit TunWindows(net::io_context& ioc)
        : ioc_(ioc)
    {}

    ~TunWindows() override {
        close();
    }

    bool open(const std::string& name,
              const std::string& ip,
              int prefix,
              int mtu) override
    {
#ifdef WINTUN_ENABLED
        // Wintun implementation would go here.
        // Steps:
        //   1. Load wintun.dll via LoadLibrary
        //   2. Get function pointers (WintunCreateAdapter, etc.)
        //   3. WintunCreateAdapter() → creates a virtual adapter
        //   4. WintunStartSession() → gets a ring buffer session
        //   5. netsh interface ip set address name="<adapter>" static <ip> <mask>
        //   6. Register read/write callbacks with the session
        //
        // See: https://git.zx2c4.com/wintun/tree/api

        spdlog::info("TunWindows: Wintun adapter '{}' with IP {}/{} (stub)", name, ip, prefix);
        virtual_ip_ = ip;
        ifname_ = name;
        opened_ = true;
        return true;
#else
        (void)name; (void)ip; (void)prefix; (void)mtu;
        spdlog::error("TunWindows: not implemented (compile with -DWINTUN_ENABLED)");
        return false;
#endif
    }

    void close() override {
#ifdef WINTUN_ENABLED
        // WintunDestroyAdapter(), etc.
#endif
        opened_ = false;
        spdlog::info("TunWindows: closed");
    }

    bool isOpen() const override { return opened_; }
    std::string interfaceName() const override { return ifname_; }
    std::string virtualIp() const override { return virtual_ip_; }

    std::vector<uint8_t> read() override {
        return {};
    }

    int write(const std::vector<uint8_t>& packet) override {
        (void)packet;
        return -1;
    }

    void asyncRead(net::mutable_buffer buf, ReadHandler handler) override {
        // For Wintun, use WintunReceivePacket() in a loop
        // and post to io_context via net::post
        if (handler) {
            boost::system::error_code ec = boost::system::errc::make_error_code(boost::system::errc::not_supported);
            handler(ec, 0);
        }
    }

    void asyncWrite(net::const_buffer buf, WriteHandler handler) override {
        // For Wintun, use WintunAllocateSendPacket() + WintunSendPacket()
        if (handler) {
            boost::system::error_code ec = boost::system::errc::make_error_code(boost::system::errc::not_supported);
            handler(ec, 0);
        }
    }

    bool addRoute(const std::string& dest) override {
        // On Windows, use:
        //   route ADD <dest> MASK <mask> <gateway>
        // or netsh interface ip add route
        spdlog::info("TunWindows: add route {} (stub)", dest);
        return false;
    }

    bool removeRoute(const std::string& dest) override {
        (void)dest;
        return false;
    }

private:
    net::io_context& ioc_;
    std::string ifname_;
    std::string virtual_ip_;
    bool opened_ = false;
};

// ── Factory (Windows) ───────────────────────────────────────

std::shared_ptr<TunInterface> TunInterface::create(net::io_context& ioc) {
#ifdef _WIN32
    return std::make_shared<TunWindows>(ioc);
#else
    // On non-Windows, the factory is defined in TunLinux.cpp
    // This should not be reached
    (void)ioc;
    return nullptr;
#endif
}
