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
    auto is_valid() const noexcept { return inner_.is_valid(); }

public:
    static auto connect(const net::SocketAddr& addr) -> Stream {
        // TODO: 支持非阻塞
        auto sock(Socket::create(addr.family(), SOCK_STREAM, 0));

        auto ret = ::connect(sock.fd(), addr.sockaddr(), addr.length());
        if (ret == -1) {
            return Stream{Socket{-1}};
        }
        return Stream{std::move(sock)};
    }

private:
    Socket inner_;
};
}
