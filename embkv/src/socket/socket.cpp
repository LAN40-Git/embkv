#include "socket/socket.h"

auto embkv::socket::detail::Socket::bind(const net::SocketAddr& addr) const -> bool {
    if (::bind(fd_, addr.sockaddr(), addr.length()) != 0) {
        return false;
    }
    return true;
}

auto embkv::socket::detail::Socket::listen(int maxn) const -> bool {
    if (::listen(fd_, maxn) != 0) {
        return false;
    }
    return true;
}

auto embkv::socket::detail::Socket::shutdown(int how) const noexcept -> bool {
    return ::shutdown(fd_, how) == 0;
}

auto embkv::socket::detail::Socket::set_reuseaddr(int option) const -> bool {
    return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) == 0;
}

auto embkv::socket::detail::Socket::set_nonblock(int option) const -> bool {
    return ::setsockopt(fd_, SOL_SOCKET, SOCK_NONBLOCK, &option, sizeof(option)) == 0;
}

auto embkv::socket::detail::Socket::set_nodelay(int option) const -> bool {
    return ::setsockopt(fd_, SOL_SOCKET, SOCK_NONBLOCK, &option, sizeof(option)) == 0;
}

auto embkv::socket::detail::Socket::set_keepalive(int option) const -> bool {
    return ::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &option, sizeof(option)) == 0;
}

auto embkv::socket::detail::Socket::set_reuseport(int option) const -> bool {
    return ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &option, sizeof(option)) == 0;
}

auto embkv::socket::detail::Socket::create(int domain, int type, int protocol) -> Socket {
    auto fd = ::socket(domain, type, protocol);
    if (fd < 0) {
        return Socket{-1};
    }
    return Socket{fd};
}
