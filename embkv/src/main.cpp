#include "log.h"
#include <ev.h>
#include "socket/socket.h"

using namespace embkv::socket::detail;
using namespace embkv::log;
using namespace embkv::net;

static void accept_cb(struct ev_loop* loop, ev_io *w, int revents) {
    if (revents & EV_READ) {
        sockaddr_in clnt_addr{};
        socklen_t clnt_addr_len = sizeof(clnt_addr);

        int clnt_fd = accept(w->fd, reinterpret_cast<struct sockaddr*>(&clnt_addr), &clnt_addr_len);
        console.info("Accept connection from {}", clnt_fd);
    } else if (revents & EV_ERROR) {
        console.error("ERROR");
        ev_io_stop(loop, w);
    }
}

int main() {
    auto listener = embkv::socket::detail::Socket::create(PF_INET, SOCK_STREAM, 0);
    std::error_code ec;
    SocketAddr addr{};
    if (!SocketAddr::parse("127.0.0.1", 8080, addr, ec)) {
        console.error("Failed to parse address");
        return -1;
    }
    if (!listener.bind(addr)) {
        console.error("Failed to bind");
        return -1;
    }
    if (!listener.listen()) {
        console.error("Failed to listen on socket");
        return -1;
    }
    struct ev_loop* loop = EV_DEFAULT;

    ev_io accept_watcher;
    ev_io_init(&accept_watcher, accept_cb, listener.fd(), EV_READ);
    ev_io_start(loop, &accept_watcher);
    ev_run(loop, 0);
    return 0;
}