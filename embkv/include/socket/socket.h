#pragma once
#include "common/util/fd.h"
#include "socket/net/addr.h"

namespace embkv::socket::detail
{
class Socket : public util::FD {
public:
    explicit Socket(int fd) : FD(fd) {}

public:
    auto bind(const net::SocketAddr& addr) const -> bool;
    auto listen(int maxn = SOMAXCONN) const -> bool;
    auto shutdown(int how) const noexcept -> bool;
    auto set_reuseaddr(int option) const -> bool;
    auto set_nonblock(int option) const -> bool;
    auto set_nodelay(int option) const -> bool;
    auto set_keepalive(int option) const -> bool;
    auto set_reuseport(int option) const -> bool;

public:
    // 需要检查返回的套接字是否有效
    static auto create(int domain, int type, int protocol) -> Socket;
};
}
