#include "log.h"
#include <ev.h>
#include "socket/net/listener.h"
#include "socket/net/stream.h"

using namespace embkv::socket::detail;
using namespace embkv::log;
using namespace embkv::socket::net;

int main() {
    // std::error_code ec;
    // SocketAddr addr{};
    // if (!SocketAddr::parse("127.0.0.1", 9090, addr, ec)) {
    //     console.error("{}", ec.message());
    //     return -1;
    // }
    // auto listener = TcpListener::bind(addr);
    // if (!listener.is_valid()) {
    //     console.error("Failed to bind socket");
    //     return -1;
    // }
    //
    // struct ev_loop* loop = EV_DEFAULT;
    //
    // ev_io accept_watcher;
    // accept_watcher.data = &listener;
    // ev_io_init(&accept_watcher, accept_cb, listener.fd(), EV_READ | EV_WRITE);
    // ev_io_start(loop, &accept_watcher);
    // ev_run(loop, 0);
    // return 0;
}