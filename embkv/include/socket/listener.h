#pragma once
#include "socket/net/addr.h"
#include "socket/socket.h"

namespace embkv::socket::detail
{
template <class Listener, class Stream>
class BaseListener {
protected:
    explicit BaseListener(Socket &&inner)
        : inner_(std::move(inner)) {}

public:
    auto accept() noexcept -> std::pair<Stream, net::SocketAddr> {
        net::SocketAddr      addr{};
        socklen_t            addrlen{sizeof(net::SocketAddr)};
        auto fd = ::accept(inner_.fd(), reinterpret_cast<sockaddr *>(&addr), &addrlen);
        if (fd >= 0) {
            return std::make_pair(Stream{Socket{fd}}, addr);
        }
        return std::make_pair(Stream{Socket{-1}}, addr);
    }

    auto close() noexcept {
        return inner_.close();
    }

    auto fd() const noexcept {
        return inner_.fd();
    }

    auto is_valid() const noexcept {
        return inner_.is_valid();
    }
    
public:
    static auto bind(const net::SocketAddr& addr) -> Listener {
        auto inner = Socket::create(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (!inner.is_valid()) {
            return Listener{Socket(-1)};
        }
        if (!inner.bind(addr)) {
            return Listener{Socket(-1)};
        }
        // 默认复用地址和端口
        if (!inner.set_reuseaddr(1)) {
            return Listener{Socket(-1)};
        }
        if (!inner.set_reuseport(1)) {
            return Listener{Socket(-1)};
        }
        if (!inner.listen()) {
            return Listener{Socket(-1)};
        }
        return Listener{std::move(inner)};
    }

private:
    Socket inner_;
};
} // namespace embkv::socket::detail
