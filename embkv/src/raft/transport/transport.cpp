#include "../../../include/raft/transport/transport.h"
#include "../../../include/raft/transport/head_manager.h"

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
    ev_io_start(loop_, &accept_watcher_);
}

void embkv::raft::Transport::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    // 停止接收连接
    ev_io_stop(loop_, &accept_watcher_);
    auto* ac_data = static_cast<AcceptData*>(accept_watcher_.data);
    delete ac_data;
}

void embkv::raft::Transport::accept_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto* ac_data = static_cast<AcceptData*>(w->data);
    if (revents & EV_ERROR) {
        // ev_io_stop(loop, w);
        // delete data;
        // TODO: 记录并处理错误
        return;
    }

    if (revents & EV_READ) {
        // 接收连接
        auto pair = ac_data->listener.accept();
        auto& stream = pair.first;
        auto& peer_addr = pair.second;
        if (stream.is_valid()) {
            // 开始握手
            ac_data->transport.start_handshake(std::move(stream), peer_addr);
        }
    }
}

void embkv::raft::Transport::handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    if (!hs_data->transport.is_running() || revents & EV_ERROR) {
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

void embkv::raft::Transport::try_connect_to_peer(uint64_t id) noexcept {
    auto peer = sess_mgr_.peer_at(id);
    if (!peer) {
        return;
    }
    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse(peer->ip(), peer->port(), addr, ec)) {
        return;
    }

    auto stream = socket::net::TcpStream::connect(addr);
    if (!stream.is_valid()) {
        return;
    }
    // 发送握手消息
    detail::HeadManager::Header header;
    header.flags = detail::HeadManager::Flags::kRaftNode;
    header.length = id_;
    if (detail::HeadManager::serialize(header)) {
        auto& buffer = detail::HeadManager::buffer();
        stream.write_exact(buffer.data(), buffer.size());
        peer->pipeline()->run(std::move(stream));
    }
}

void embkv::raft::Transport::start_handshake(socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept {
    auto* hs_data = new HandshakeData{std::move(stream), *this, addr};
    // 开始握手
    hs_data->io_watcher.data = hs_data;
    ev_io_init(&hs_data->io_watcher, handshake_cb, hs_data->stream.fd(), EV_READ);
    ev_io_start(loop_, &hs_data->io_watcher);

    // 启动握手定时器（3s）
    hs_data->timer_watcher.data = hs_data;
    ev_timer_init(&hs_data->timer_watcher, handle_handshake_timeout, 3.0, 0.0);
    ev_timer_start(loop_, &hs_data->timer_watcher);
}
