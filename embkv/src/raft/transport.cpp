#include "raft/transport.h"
#include "raft/head_manager.h"

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
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    if (revents & EV_ERROR) {
        delete hs_data;
        return;
    }

    if (revents & EV_READ) {
        auto& buffer = detail::HeadManager::buffer();
        hs_data->stream.read_exact(buffer.data(), buffer.size());
        if (auto header = detail::HeadManager::deserialize()) {
            if (!header.has_value() || !header.value().is_valid()) { // 头部无效，握手失败
                delete hs_data;
                return;
            }

            auto peer = hs_data->transport.session_manager().peer_at(header.value().length);
            if (!peer) {
                delete hs_data;
                return;
            }
            // 启动通信
            peer->pipeline()->run(std::move(hs_data->stream));
            delete hs_data;
        }
    }
}

void embkv::raft::Transport::handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    delete hs_data;
}

void embkv::raft::Transport::start_handshake(socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept {
    auto* hs_data = new HandshakeData{std::move(stream), *this, addr};
    // 开始握手
    hs_data->io_watcher.data = hs_data;
    ev_io_init(&hs_data->io_watcher, handshake_cb, hs_data->stream.fd(), EV_READ);
    ev_io_start(loop, &hs_data->io_watcher);

    // 启动握手定时器
    hs_data->timer_watcher.data = hs_data;
    ev_timer_init(&hs_data->timer_watcher, handle_handshake_timeout, 3.0, 0.0);
    ev_timer_start(loop, &hs_data->timer_watcher);
}
