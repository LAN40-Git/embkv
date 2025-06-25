#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <arpa/inet.h>
#include <netdb.h>
#include <memory>

namespace embkv::socket::net
{
class Ipv4Addr {
public:
    Ipv4Addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : ip_{static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8
              | static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24} {}

    Ipv4Addr(uint32_t ip)
        : ip_{ip} {}

public:
    uint32_t addr() const noexcept { return ip_; }

    std::string to_string() const {
        char buf[16];
        if (!inet_ntop(AF_INET, &ip_, buf, sizeof(buf))) {
            throw std::runtime_error("inet_ntop failed");
        }
        return buf;
    }

public:
    static bool parse(const std::string& ip, Ipv4Addr& out_addr, std::error_code& ec) {
        uint32_t addr;
        if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
            ec = std::error_code(errno, std::generic_category());
            return false;
        }
        out_addr = Ipv4Addr(addr);
        return true;
    }

public:
    friend bool operator==(const Ipv4Addr& lhs, const Ipv4Addr& rhs) noexcept {
        return lhs.ip_ == rhs.ip_;
    }

    friend bool operator!=(const Ipv4Addr& lhs, const Ipv4Addr& rhs) noexcept {
        return lhs.ip_ != rhs.ip_;
    }

private:
    uint32_t ip_;
};

class SocketAddr {
public:
    SocketAddr() = default;

public:
    SocketAddr(const sockaddr *addr, std::size_t len) {
        std::memcpy(&addr_, addr, len);
    }

    SocketAddr(Ipv4Addr ip, uint16_t port) {
        addr_.in4.sin_family = AF_INET;
        addr_.in4.sin_port = ::htons(port);
        addr_.in4.sin_addr.s_addr = ip.addr();
    }


    auto family() const noexcept {
        return addr_.in4.sin_family;
    }

    auto ip() const noexcept -> Ipv4Addr {
        return Ipv4Addr{addr_.in4.sin_addr.s_addr};
    }

    void set_ip(Ipv4Addr ip) {
        addr_.in4.sin_addr.s_addr = ip.addr();
    }

    auto port() const -> uint16_t {
        return ::ntohs(addr_.in4.sin_port);
    }

    auto to_string() const -> std::string {
        char buf[128];
        ::inet_ntop(AF_INET, &addr_.in4.sin_addr, buf, sizeof(buf) - 1);
        return std::string{buf} + ":" + std::to_string(this->port());
    }

    auto sockaddr() const noexcept -> const struct sockaddr * {
        return reinterpret_cast<const struct sockaddr *>(&addr_);
    }

    auto sockaddr() noexcept -> struct sockaddr * {
        return reinterpret_cast<struct sockaddr *>(&addr_);
    }

    auto length() const noexcept -> socklen_t {
        return sizeof(sockaddr_in);
    }

public:
    static auto parse(const std::string& host_name, uint16_t port,
                      SocketAddr& out_addr, std::error_code& ec) -> bool {
        addrinfo hints{};
        hints.ai_flags = AI_NUMERICHOST;
        addrinfo *result{nullptr};
        struct AddrInfoDeleter {
            void operator()(addrinfo* p) const { if (p) freeaddrinfo(p); }
        };
        std::unique_ptr<addrinfo, AddrInfoDeleter> result_guard;

        int ret = ::getaddrinfo(host_name.c_str(),
                               std::to_string(port).c_str(),
                               &hints, &result);
        if (ret != 0) {
            ec = std::error_code(errno, std::generic_category());
            return false;
        }

        result_guard.reset(result);  // 接管资源

        if (result == nullptr) {
            ec = std::error_code(EINVAL, std::generic_category());
            return false;
        }

        out_addr = SocketAddr(result->ai_addr, result->ai_addrlen);
        return true;
    }

private:
    union {
        sockaddr_in  in4;
    } addr_;
};
} // namespace embkv::net
