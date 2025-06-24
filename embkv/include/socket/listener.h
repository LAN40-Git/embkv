#pragma once
#include "socket/socket.h"

namespace embkv::socket::detail
{
template <class Listener, class Stream>
class BaseListener {
protected:
    explicit BaseListener(Socket &&inner)
        : inner_(std::move(inner)) {}

public:
    auto accept() noexcept {
        
    }

private:
    Socket inner_;
};
}
