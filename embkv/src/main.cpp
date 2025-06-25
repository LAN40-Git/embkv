#include "log.h"
#include <ev.h>
#include "socket/net/listener.h"
#include "socket/net/stream.h"

using namespace embkv::socket::detail;
using namespace embkv::log;
using namespace embkv::socket::net;

static auto read_cb(struct ev_loop* loop, ev_io* w, int revents) {
    auto stream = static_cast<TcpStream*>(w->data);
    thread_local char buf[1024];

    if (revents & EV_READ) {
        auto bytes_read = stream->read(buf, sizeof(buf));
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            console.info("Received data: {}", buf);
        } else if (bytes_read == 0) {  // 客户端关闭连接
            console.info("Client disconnected");
            ev_io_stop(loop, w);
            delete stream;
            delete w;       // 释放 ev_io 监视器
        }
    } else if (revents & EV_WRITE) {

    } else if (revents & EV_ERROR) {
        console.error("READ ERROR");
        ev_io_stop(loop, w);
        delete stream;
        delete w;
    }
}

static auto accept_cb(struct ev_loop* loop, ev_io *w, int revents) {
    auto listener = static_cast<TcpListener *>(w->data);

    if (revents & EV_READ) {
        auto [stream, peer_addr] = listener->accept();
        if (stream.is_valid()) {
            console.info("Accept connection from {}-{}", peer_addr.to_string(), stream.fd());

            auto* stream_watcher = new ev_io;
            auto* stream_ptr = new TcpStream(std::move(stream));
            stream_watcher->data = stream_ptr;
            ev_io_init(stream_watcher, read_cb, stream_ptr->fd(), EV_READ);
            ev_io_start(loop, stream_watcher);
        }

    } else if (revents & EV_ERROR) {
        console.error("ACCEPT ERROR");
        ev_io_stop(loop, w);
    }
}

int main() {
    std::error_code ec;
    SocketAddr addr{};
    if (!SocketAddr::parse("127.0.0.1", 9090, addr, ec)) {
        console.error("{}", ec.message());
        return -1;
    }
    auto listener = TcpListener::bind(addr);
    if (!listener.is_valid()) {
        console.error("Failed to bind socket");
        return -1;
    }

    struct ev_loop* loop = EV_DEFAULT;

    ev_io accept_watcher;
    accept_watcher.data = &listener;
    ev_io_init(&accept_watcher, accept_cb, listener.fd(), EV_READ | EV_WRITE);
    ev_io_start(loop, &accept_watcher);
    ev_run(loop, 0);
    return 0;
}