#pragma once
#include "socket/socket.h"
#include "impl/impl_stream_read.h"
#include "impl/impl_stream_write.h"

namespace embkv::socket::detail
{
template <typename Stream>
class BaseStream : public ImplStreamRead<Stream>,
                   public ImplStreamWrite<Stream> {
protected:
    explicit BaseStream(Socket &&inner)
        : inner_(std::move(inner)) {}

public:
    auto close() noexcept { return inner_.close(); }
    auto fd() const noexcept { return inner_.fd(); }

public:
    static auto connect(const net::SocketAddr& addr) -> Stream {
        auto sock(Socket::create(addr.family(), SOCK_STREAM, 0));

        if (!sock.set_nonblock(1)) {
            return Stream{-1};
        }

        auto ret = ::connect(sock.fd(), addr.sockaddr(), addr.length());
        if (ret == 0) {
            return std::move(sock);
        }
        return Stream{-1};
    }

private:
    Socket inner_;
};
}
