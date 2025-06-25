#pragma once
#include "socket/listener.h"
#include "stream.h"

namespace embkv::socket::net
{
class TcpListener : public detail::BaseListener<TcpListener, TcpStream> {
public:
    explicit TcpListener(detail::Socket &&inner)
        : BaseListener(std::move(inner)) {}
};
}