#include "tun/TunInterface.h"
#include <spdlog/spdlog.h>

#include <cstring>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

// Netlink / TUN headers — careful ordering avoids struct ifreq conflict
#include <linux/if.h>        // IFF_TUN, IFF_NO_PI, struct ifreq, IFNAMSIZ
#include <linux/if_tun.h>    // TUNSETIFF, TUNSETPERSIST
#include <linux/if_addr.h>   // IFA_LOCAL, IFA_ADDRESS
#include <linux/netlink.h>   // NETLINK_ROUTE
#include <linux/rtnetlink.h> // RTM_NEWADDR, RTM_NEWROUTE, RTM_DELROUTE
#include <arpa/inet.h>       // inet_pton

// if_nametoindex replacement (avoids <net/if.h> conflict with <linux/if.h>)
static unsigned int if_nametoindex_safe(const char* ifname) {
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return 0;
    }
    close(fd);
    return static_cast<unsigned int>(ifr.ifr_ifindex);
}

// ── Netlink helpers for setting IP / routes ─────────────────

static int netlink_socket() {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        spdlog::error("TunLinux: netlink socket: {}", strerror(errno));
    }
    return fd;
}

static bool netlink_send(int nl_fd, const void* msg, size_t len) {
    struct sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    struct iovec iov{const_cast<void*>(msg), len};
    struct msghdr mh{};
    mh.msg_name = &sa;
    mh.msg_namelen = sizeof(sa);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;

    if (sendmsg(nl_fd, &mh, 0) < 0) {
        spdlog::error("TunLinux: netlink sendmsg: {}", strerror(errno));
        return false;
    }
    return true;
}

static bool add_addr_v4(const std::string& ifname,
                        const std::string& ip, int prefix) {
    int nl_fd = netlink_socket();
    if (nl_fd < 0) return false;

    struct ifindex_req { struct nlmsghdr nh; struct ifaddrmsg ifa; char buf[256]; } req{};

    struct rtattr* rta;

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.nh.nlmsg_type = RTM_NEWADDR;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
    req.nh.nlmsg_seq = 1;
    req.ifa.ifa_family = AF_INET;
    req.ifa.ifa_prefixlen = static_cast<uint8_t>(prefix);
    req.ifa.ifa_index = if_nametoindex_safe(ifname.c_str());
    req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;

    // Add IP address
    rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(4);
    inet_pton(AF_INET, ip.c_str(), RTA_DATA(rta));
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    // Add IFA_ADDRESS (same as local for ptp/point-to-point)
    rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = IFA_ADDRESS;
    rta->rta_len = RTA_LENGTH(4);
    inet_pton(AF_INET, ip.c_str(), RTA_DATA(rta));
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    bool ok = netlink_send(nl_fd, &req, req.nh.nlmsg_len);
    close(nl_fd);
    return ok;
}

static bool add_route_v4(const std::string& dest, int prefix,
                         const std::string& ifname) {
    int nl_fd = netlink_socket();
    if (nl_fd < 0) return false;

    struct { struct nlmsghdr nh; struct rtmsg rtm; char buf[256]; } req{};

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nh.nlmsg_type = RTM_NEWROUTE;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
    req.nh.nlmsg_seq = 2;
    req.rtm.rtm_family = AF_INET;
    req.rtm.rtm_dst_len = static_cast<uint8_t>(prefix);
    req.rtm.rtm_table = RT_TABLE_MAIN;
    req.rtm.rtm_protocol = RTPROT_STATIC;
    req.rtm.rtm_scope = RT_SCOPE_LINK;
    req.rtm.rtm_type = RTN_UNICAST;

    // Destination network
    struct rtattr* rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = RTA_DST;
    rta->rta_len = RTA_LENGTH(4);
    inet_pton(AF_INET, dest.c_str(), RTA_DATA(rta));
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    // Interface index
    int ifindex = if_nametoindex_safe(ifname.c_str());
    rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = RTA_OIF;
    rta->rta_len = RTA_LENGTH(4);
    std::memcpy(RTA_DATA(rta), &ifindex, 4);
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    bool ok = netlink_send(nl_fd, &req, req.nh.nlmsg_len);
    close(nl_fd);
    return ok;
}

static bool del_route_v4(const std::string& dest, int prefix,
                         const std::string& ifname) {
    int nl_fd = netlink_socket();
    if (nl_fd < 0) return false;

    struct { struct nlmsghdr nh; struct rtmsg rtm; char buf[256]; } req{};

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nh.nlmsg_type = RTM_DELROUTE;
    req.nh.nlmsg_flags = NLM_F_REQUEST;
    req.nh.nlmsg_seq = 3;
    req.rtm.rtm_family = AF_INET;
    req.rtm.rtm_dst_len = static_cast<uint8_t>(prefix);
    req.rtm.rtm_table = RT_TABLE_MAIN;
    req.rtm.rtm_protocol = RTPROT_STATIC;
    req.rtm.rtm_scope = RT_SCOPE_LINK;
    req.rtm.rtm_type = RTN_UNICAST;

    struct rtattr* rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = RTA_DST;
    rta->rta_len = RTA_LENGTH(4);
    inet_pton(AF_INET, dest.c_str(), RTA_DATA(rta));
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    int ifindex = if_nametoindex_safe(ifname.c_str());
    rta = reinterpret_cast<struct rtattr*>(
        reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nh.nlmsg_len));
    rta->rta_type = RTA_OIF;
    rta->rta_len = RTA_LENGTH(4);
    std::memcpy(RTA_DATA(rta), &ifindex, 4);
    req.nh.nlmsg_len = NLMSG_ALIGN(req.nh.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    bool ok = netlink_send(nl_fd, &req, req.nh.nlmsg_len);
    close(nl_fd);
    return ok;
}

// ── TunLinux implementation ─────────────────────────────────

class TunLinux : public TunInterface {
public:
    explicit TunLinux(net::io_context& ioc)
        : ioc_(ioc)
        , stream_(ioc)
    {}

    ~TunLinux() override {
        close();
    }

    bool open(const std::string& name,
              const std::string& ip,
              int prefix,
              int mtu) override
    {
        if (fd_ >= 0) {
            spdlog::warn("TunLinux: already open");
            return false;
        }

        fd_ = ::open("/dev/net/tun", O_RDWR | O_CLOEXEC);
        if (fd_ < 0) {
            spdlog::error("TunLinux: open /dev/net/tun: {} (need CAP_NET_ADMIN)", strerror(errno));
            return false;
        }

        struct ifreq ifr{};
        std::memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
        std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);

        if (ioctl(fd_, TUNSETIFF, &ifr) < 0) {
            spdlog::error("TunLinux: TUNSETIFF: {}", strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        ifname_ = ifr.ifr_name;
        virtual_ip_ = ip;
        prefix_ = prefix;
        opened_ = true;

        spdlog::info("TunLinux: created {} with fd={}", ifname_, fd_);

        // Set MTU
        int ctl_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (mtu > 0) {
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
            ifr.ifr_mtu = mtu;
            if (ctl_fd < 0 || ioctl(ctl_fd, SIOCSIFMTU, &ifr) < 0) {
                spdlog::warn("TunLinux: set MTU {}: {}", mtu, strerror(errno));
            } else {
                spdlog::info("TunLinux: set MTU={} on {}", mtu, ifname_);
            }
        }

        // Bring interface UP
        std::memset(&ifr, 0, sizeof(ifr));
        std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ - 1);
        if (ctl_fd < 0 || ioctl(ctl_fd, SIOCGIFFLAGS, &ifr) < 0) {
            spdlog::warn("TunLinux: get flags: {}", strerror(errno));
        } else {
            ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
            if (ioctl(ctl_fd, SIOCSIFFLAGS, &ifr) < 0) {
                spdlog::warn("TunLinux: set UP: {}", strerror(errno));
            }
        }
        if (ctl_fd >= 0) {
            ::close(ctl_fd);
        }

        // Assign IP address via netlink
        if (!add_addr_v4(ifname_, ip, prefix)) {
            spdlog::warn("TunLinux: failed to assign IP {} to {} (may need CAP_NET_ADMIN)",
                         ip, ifname_);
        }

        // Attach fd to boost::asio stream descriptor
        stream_.assign(fd_);

        spdlog::info("TunLinux: {} configured with IP {}/{}",
                     ifname_, virtual_ip_, prefix_);
        return true;
    }

    void close() override {
        if (!opened_) return;
        opened_ = false;

        boost::system::error_code ec;
        if (stream_.is_open()) {
            stream_.cancel(ec);
            auto released = stream_.release();
            if (released >= 0) {
                ::close(released);
            }
        } else if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        fd_ = -1;

        spdlog::info("TunLinux: closed {}", ifname_);
    }

    bool isOpen() const override { return opened_; }
    std::string interfaceName() const override { return ifname_; }
    std::string virtualIp() const override { return virtual_ip_; }

    std::vector<uint8_t> read() override {
        std::vector<uint8_t> buf(MTU_SIZE);
        auto n = ::read(fd_, buf.data(), buf.size());
        if (n > 0) {
            buf.resize(static_cast<size_t>(n));
            return buf;
        }
        return {};
    }

    int write(const std::vector<uint8_t>& packet) override {
        auto n = ::write(fd_, packet.data(), packet.size());
        if (n < 0) {
            spdlog::error("TunLinux: write error: {}", strerror(errno));
        }
        return static_cast<int>(n);
    }

    void asyncRead(net::mutable_buffer buf, ReadHandler handler) override {
        if (!opened_) {
            if (handler) handler(boost::asio::error::operation_aborted, 0);
            return;
        }
        stream_.async_read_some(buf, std::move(handler));
    }

    void asyncWrite(net::const_buffer buf, WriteHandler handler) override {
        if (!opened_) {
            if (handler) handler(boost::asio::error::operation_aborted, 0);
            return;
        }
        boost::asio::async_write(stream_, buf, std::move(handler));
    }

    bool addRoute(const std::string& dest) override {
        // dest format: "10.0.0.0/16"
        auto slash = dest.find('/');
        if (slash == std::string::npos) {
            spdlog::error("TunLinux: addRoute: invalid format '{}' (need cidr)", dest);
            return false;
        }
        auto ip = dest.substr(0, slash);
        auto prefix = std::stoi(dest.substr(slash + 1));

        bool ok = add_route_v4(ip, prefix, ifname_);
        if (ok) {
            spdlog::info("TunLinux: route added: {} via {}", dest, ifname_);
        } else {
            spdlog::error("TunLinux: failed to add route: {}", dest);
        }
        return ok;
    }

    bool removeRoute(const std::string& dest) override {
        auto slash = dest.find('/');
        if (slash == std::string::npos) return false;
        auto ip = dest.substr(0, slash);
        auto prefix = std::stoi(dest.substr(slash + 1));
        return del_route_v4(ip, prefix, ifname_);
    }

private:
    net::io_context& ioc_;
    boost::asio::posix::stream_descriptor stream_;
    int fd_ = -1;
    std::string ifname_;
    std::string virtual_ip_;
    int prefix_ = 24;
    bool opened_ = false;

    static constexpr size_t MTU_SIZE = 1500;
};

// ── Factory ─────────────────────────────────────────────────

std::shared_ptr<TunInterface> TunInterface::create(net::io_context& ioc) {
    return std::make_shared<TunLinux>(ioc);
}
