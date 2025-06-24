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
    static auto connect(const net::SocketAddr& addr) {
        
    }

private:
    Socket inner_;
};
}
