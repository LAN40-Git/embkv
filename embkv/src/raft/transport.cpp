#include "raft/transport.h"

void embkv::raft::Transport::run() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }

    // 启动监听
    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse("0.0.0.0", 8080, addr, ec)) {
        return;
    }

    auto listener = socket::net::TcpListener::bind(addr);
    if (!listener.is_valid()) {
        return;
    }

    is_running_.store(true, std::memory_order_release);
    auto fd = listener.fd();
    accept_watcher_.data = new AcceptData{std::move(listener), *this};
    ev_io_init(&accept_watcher_, accept_cb, fd, EV_READ);
    ev_io_start(loop, &accept_watcher_);
}

void embkv::raft::Transport::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
}

void embkv::raft::Transport::accept_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto* data = static_cast<AcceptData*>(w->data);
    if (!data->transport.is_running() || revents & EV_ERROR) {
        ev_io_stop(loop, w);
        delete data;
        return;
    }

    if (revents & EV_READ) {
        // 接收连接
        auto pair = data->listener.accept();
        auto& stream = pair.first;
        auto& peer_addr = pair.second;
        if (stream.is_valid()) {
            // 开始握手
            data->transport.start_handshake(std::move(stream), peer_addr);
        }
    }
}

void embkv::raft::Transport::handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents) {

}

void embkv::raft::Transport::handle_handshake_timeout(struct ev_loop* loop, struct ev_io* w, int revents) {

}

void embkv::raft::Transport::start_handshake(socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept {

}
