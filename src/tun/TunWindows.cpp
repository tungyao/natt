/// Windows Wintun TUN implementation.
///
/// Dynamically loads wintun.dll at runtime — no compile-time dependency
/// on the Wintun SDK header. Users just need to place wintun.dll
/// next to the executable.
///
/// Download wintun.dll from: https://www.wintun.net/
///
/// If wintun.dll is not found, open() logs a clear error and returns false.

#include "tun/TunInterface.h"
#include <spdlog/spdlog.h>

#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>
#include <vector>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <netioapi.h>
#else
// This file is Windows-only — compilation on other platforms is an error
#error TunWindows.cpp is for Windows only
#endif

// ── Wintun API types (loaded dynamically from wintun.dll) ──────

struct WINTUN_ADAPTER_;
struct WINTUN_SESSION_;

using WintunCreateAdapter_t  = WINTUN_ADAPTER_* (*)(const wchar_t*, const wchar_t*, const GUID*, BOOL);
using WintunCloseAdapter_t   = void                (*)(WINTUN_ADAPTER_*);
using WintunGetAdapterLUID_t = BOOL                (*)(WINTUN_ADAPTER_*, NET_LUID*);
using WintunStartSession_t   = WINTUN_SESSION_*    (*)(WINTUN_ADAPTER_*, DWORD);
using WintunEndSession_t     = void                (*)(WINTUN_SESSION_*);
using WintunAllocateSendPacket_t = BYTE*           (*)(WINTUN_SESSION_*, DWORD);
using WintunSendPacket_t     = void                (*)(WINTUN_SESSION_*, BYTE*);
using WintunReceivePacket_t  = BYTE*               (*)(WINTUN_SESSION_*, DWORD*);
using WintunReleaseReceivePacket_t = void          (*)(WINTUN_SESSION_*, BYTE*);

struct WintunApi {
    HMODULE dll = nullptr;

    WintunCreateAdapter_t  CreateAdapter  = nullptr;
    WintunCloseAdapter_t   CloseAdapter   = nullptr;
    WintunGetAdapterLUID_t GetAdapterLUID = nullptr;
    WintunStartSession_t   StartSession   = nullptr;
    WintunEndSession_t     EndSession     = nullptr;
    WintunAllocateSendPacket_t AllocateSendPacket = nullptr;
    WintunSendPacket_t     SendPacket     = nullptr;
    WintunReceivePacket_t  ReceivePacket  = nullptr;
    WintunReleaseReceivePacket_t ReleaseReceivePacket = nullptr;

    bool load() {
        dll = LoadLibraryW(L"wintun.dll");
        if (!dll) {
            spdlog::error("TunWindows: LoadLibrary(wintun.dll) failed (code {})", GetLastError());
            return false;
        }

        auto get = [&](const char* name) -> FARPROC {
            auto p = GetProcAddress(dll, name);
            if (!p) spdlog::error("TunWindows: wintun.dll missing export '{}'", name);
            return p;
        };

        CreateAdapter  = (WintunCreateAdapter_t) get("WintunCreateAdapter");
        CloseAdapter   = (WintunCloseAdapter_t)  get("WintunCloseAdapter");
        GetAdapterLUID = (WintunGetAdapterLUID_t)get("WintunGetAdapterLUID");
        StartSession   = (WintunStartSession_t)  get("WintunStartSession");
        EndSession     = (WintunEndSession_t)    get("WintunEndSession");
        AllocateSendPacket = (WintunAllocateSendPacket_t) get("WintunAllocateSendPacket");
        SendPacket     = (WintunSendPacket_t)    get("WintunSendPacket");
        ReceivePacket  = (WintunReceivePacket_t) get("WintunReceivePacket");
        ReleaseReceivePacket = (WintunReleaseReceivePacket_t) get("WintunReleaseReceivePacket");

        if (!CreateAdapter || !CloseAdapter || !GetAdapterLUID ||
            !StartSession || !EndSession ||
            !AllocateSendPacket || !SendPacket ||
            !ReceivePacket || !ReleaseReceivePacket) {
            FreeLibrary(dll);
            dll = nullptr;
            return false;
        }
        return true;
    }

    void unload() {
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
        CreateAdapter = nullptr;
        CloseAdapter = nullptr;
        GetAdapterLUID = nullptr;
        StartSession = nullptr;
        EndSession = nullptr;
        AllocateSendPacket = nullptr;
        SendPacket = nullptr;
        ReceivePacket = nullptr;
        ReleaseReceivePacket = nullptr;
    }
};

// ── TunWindows implementation ────────────────────────────────

class TunWindows : public TunInterface {
public:
    explicit TunWindows(net::io_context& ioc)
        : ioc_(ioc)
        , read_timer_(ioc)
    {}

    ~TunWindows() override {
        close();
    }

    bool open(const std::string& name,
              const std::string& ip,
              int prefix,
              int mtu) override
    {
        if (opened_) {
            spdlog::warn("TunWindows: already open");
            return false;
        }

        if (!api_.load()) {
            spdlog::error("TunWindows: failed to load wintun.dll (place it next to the executable)");
            return false;
        }

        // Create Wintun adapter
        std::wstring wname  = to_wide(name);
        std::wstring wtype  = L"NAT-Tunnel";
        adapter_ = api_.CreateAdapter(wname.c_str(), wtype.c_str(), nullptr, FALSE);
        if (!adapter_) {
            spdlog::error("TunWindows: WintunCreateAdapter failed (code {})", GetLastError());
            api_.unload();
            return false;
        }

        // Resolve the interface name from LUID
        NET_LUID luid;
        if (api_.GetAdapterLUID(adapter_, &luid)) {
            wchar_t buf[256];
            DWORD len = 256;
            if (ConvertInterfaceLuidToNameW(&luid, buf, len) == NO_ERROR) {
                ifname_ = to_utf8(buf);
            }
        }
        if (ifname_.empty()) {
            ifname_ = name;
        }

        // Start session (ring-buffer capacity 1 MiB)
        session_ = api_.StartSession(adapter_, 1024 * 1024);
        if (!session_) {
            spdlog::error("TunWindows: WintunStartSession failed (code {})", GetLastError());
            api_.CloseAdapter(adapter_);
            adapter_ = nullptr;
            api_.unload();
            return false;
        }

        virtual_ip_ = ip;
        mtu_ = mtu;
        opened_ = true;

        // Set IP via netsh (requires admin)
        set_ip_via_netsh(ip, prefix);

        // Set MTU via netsh
        set_mtu_via_netsh(mtu);

        // Get interface index for route commands
        if (ConvertInterfaceLuidToIndex(&luid, &ifindex_) != NO_ERROR) {
            ifindex_ = 0;
        }

        spdlog::info("TunWindows: adapter '{}' ({}/{} mtu={})",
                     ifname_, ip, prefix, mtu);
        return true;
    }

    void close() override {
        if (!opened_) return;
        opened_ = false;

        // Cancel pending async read
        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            if (read_handler_) {
                auto handler = std::move(read_handler_);
                net::post(ioc_, [handler]() {
                    handler(boost::asio::error::operation_aborted, 0);
                });
            }
        }
        read_timer_.cancel();

        // Teardown Wintun
        if (session_) {
            api_.EndSession(session_);
            session_ = nullptr;
        }
        if (adapter_) {
            api_.CloseAdapter(adapter_);
            adapter_ = nullptr;
        }
        api_.unload();

        spdlog::info("TunWindows: closed {}", ifname_);
    }

    bool isOpen() const override { return opened_; }
    std::string interfaceName() const override { return ifname_; }
    std::string virtualIp() const override { return virtual_ip_; }

    std::vector<uint8_t> read() override {
        if (!opened_ || !session_) return {};
        for (int i = 0; i < 50; ++i) {
            DWORD len;
            BYTE* pkt = api_.ReceivePacket(session_, &len);
            if (pkt) {
                std::vector<uint8_t> out(pkt, pkt + len);
                api_.ReleaseReceivePacket(session_, pkt);
                return out;
            }
            Sleep(1);
        }
        return {};
    }

    int write(const std::vector<uint8_t>& packet) override {
        if (!opened_ || !session_) return -1;
        DWORD len = static_cast<DWORD>(packet.size());
        BYTE* buf = api_.AllocateSendPacket(session_, len);
        if (!buf) {
            spdlog::warn("TunWindows: send ring full, dropping {} bytes", len);
            return -1;
        }
        std::memcpy(buf, packet.data(), len);
        api_.SendPacket(session_, buf);
        return static_cast<int>(len);
    }

    void asyncRead(net::mutable_buffer buf, ReadHandler handler) override {
        if (!opened_ || !session_) {
            if (handler) handler(boost::asio::error::operation_aborted, 0);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            if (read_handler_) {
                auto old = std::move(read_handler_);
                net::post(ioc_, [old]() {
                    old(boost::asio::error::operation_aborted, 0);
                });
            }
            read_buf_ = buf;
            read_handler_ = std::move(handler);
        }

        try_read_now();
    }

    void asyncWrite(net::const_buffer buf, WriteHandler handler) override {
        if (!opened_ || !session_) {
            if (handler) handler(boost::asio::error::operation_aborted, 0);
            return;
        }

        DWORD len = static_cast<DWORD>(buf.size());
        BYTE* wbuf = api_.AllocateSendPacket(session_, len);
        if (!wbuf) {
            if (handler) {
                net::post(ioc_, [handler]() {
                    handler(boost::system::errc::make_error_code(boost::system::errc::no_buffer_space), 0);
                });
            }
            return;
        }
        std::memcpy(wbuf, buf.data(), len);
        api_.SendPacket(session_, wbuf);

        if (handler) {
            net::post(ioc_, [handler, len]() {
                handler({}, len);
            });
        }
    }

    bool addRoute(const std::string& dest) override {
        auto slash = dest.find('/');
        if (slash == std::string::npos) {
            spdlog::error("TunWindows: addRoute: invalid CIDR '{}'", dest);
            return false;
        }
        auto net_ip = dest.substr(0, slash);
        auto prefix = std::stoi(dest.substr(slash + 1));
        auto mask = prefix_to_mask(prefix);

        // route ADD <network> MASK <mask> <gateway> IF <ifindex>
        std::string cmd = "route ADD " + net_ip + " MASK " + mask + " "
                        + virtual_ip_ + " IF " + std::to_string(ifindex_);
        spdlog::info("TunWindows: route add: {}", cmd);
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            spdlog::error("TunWindows: route add failed (code {})", rc);
            return false;
        }
        return true;
    }

    bool removeRoute(const std::string& dest) override {
        auto slash = dest.find('/');
        if (slash == std::string::npos) return false;
        auto net_ip = dest.substr(0, slash);
        std::string cmd = "route DELETE " + net_ip;
        std::system(cmd.c_str());
        return true;
    }

private:
    // ── net helpers ──

    static std::wstring to_wide(const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
        return w;
    }

    static std::string to_utf8(const std::wstring& w) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
        return s;
    }

    static std::string prefix_to_mask(int prefix) {
        if (prefix <= 0) return "0.0.0.0";
        if (prefix >= 32) return "255.255.255.255";
        uint32_t m = htonl(~((1u << (32 - prefix)) - 1));
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &m, buf, sizeof(buf));
        return buf;
    }

    void set_ip_via_netsh(const std::string& ip, int prefix) {
        std::string mask = prefix_to_mask(prefix);
        std::string cmd = "netsh interface ip set address name=\"" + ifname_
                        + "\" static " + ip + " " + mask;
        spdlog::info("TunWindows: {}", cmd);
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            spdlog::warn("TunWindows: netsh set IP failed (code {}), may need admin rights", rc);
        }
    }

    void set_mtu_via_netsh(int mtu) {
        std::string cmd = "netsh interface ipv4 set subinterface \""
                        + ifname_ + "\" mtu=" + std::to_string(mtu);
        spdlog::info("TunWindows: {}", cmd);
        std::system(cmd.c_str());
    }

    // ── async read helpers ──

    void try_read_now() {
        if (!opened_ || !session_) return;
        DWORD len;
        BYTE* pkt = api_.ReceivePacket(session_, &len);
        if (pkt) {
            deliver_packet(pkt, len);
        } else {
            schedule_read_poll();
        }
    }

    void schedule_read_poll() {
        if (!opened_) return;
        read_timer_.expires_after(std::chrono::milliseconds(1));
        read_timer_.async_wait([this](boost::system::error_code ec) {
            if (ec || !opened_) return;
            try_read_now();
        });
    }

    void deliver_packet(BYTE* pkt, DWORD len) {
        ReadHandler handler;
        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            if (!read_handler_) {
                api_.ReleaseReceivePacket(session_, pkt);
                return;
            }
            size_t copy_n = std::min(static_cast<size_t>(len), read_buf_.size());
            if (copy_n > 0) {
                std::memcpy(read_buf_.data(), pkt, copy_n);
            }
            api_.ReleaseReceivePacket(session_, pkt);
            handler = std::move(read_handler_);
        }
        if (handler) {
            net::post(ioc_, [handler, len]() {
                handler({}, len);
            });
        }
    }

    // ── members ──

    net::io_context& ioc_;
    WintunApi api_;
    WINTUN_ADAPTER_* adapter_ = nullptr;
    WINTUN_SESSION_* session_ = nullptr;

    std::string ifname_;
    std::string virtual_ip_;
    int mtu_ = 1300;
    bool opened_ = false;
    DWORD ifindex_ = 0;

    std::mutex read_mutex_;
    net::mutable_buffer read_buf_;
    ReadHandler read_handler_;
    boost::asio::steady_timer read_timer_;
};

// ── Factory ───────────────────────────────────────────────────

std::shared_ptr<TunInterface> TunInterface::create(net::io_context& ioc) {
#ifdef _WIN32
    return std::make_shared<TunWindows>(ioc);
#else
    (void)ioc;
    return nullptr;
#endif
}
