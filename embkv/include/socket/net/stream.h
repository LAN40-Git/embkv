#pragma once
#include "socket/net/addr.h"
#include "socket/stream.h"

namespace embkv::socket::net
{
class TcpStream : public detail::BaseStream<TcpStream> {
public:
    explicit TcpStream(detail::Socket &&inner)
        : BaseStream(std::move(inner)) {}
};
}
