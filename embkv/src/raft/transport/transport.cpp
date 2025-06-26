#include "raft/transport/transport.h"

void embkv::raft::Transport::run() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    // 监听循环
    boost::asio::post(pool_, [this]() {
        struct ev_loop* ac_loop = ev_loop_new(EVFLAG_AUTO);
        loops_.emplace(ac_loop);
        accept_loop(ac_loop);
    });
}

void embkv::raft::Transport::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    for (auto& loop : loops_) {
        ev_break(loop, EVBREAK_ALL);
    }
    pool_.wait();
    loops_.clear();
}

void embkv::raft::Transport::accept_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto* ac_data = static_cast<AcceptData*>(w->data);
    if (revents & EV_READ) {
        // 接收连接
        auto pair = ac_data->listener.accept();
        auto& stream = pair.first;
        auto& peer_addr = pair.second;
        if (stream.is_valid()) {
            // 开始握手
            ac_data->transport.start_handshake(loop, std::move(stream), peer_addr);
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("accept_cb error : {}", strerror(errno));
    }
}

void embkv::raft::Transport::handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto hs_data = static_cast<HandshakeData*>(w->data);
    handshake_data().erase(hs_data);
    if (revents & EV_ERROR) {
        log::console().error("handshake_cb error : {}", strerror(errno));
    } else if (revents & EV_READ) {
        auto& buffer = detail::HeadManager::buffer();
        hs_data->stream.read_exact(buffer.data(), buffer.size());

        if (auto header = detail::HeadManager::deserialize()) {
            if (!header.has_value() || !header.value().is_valid()) { // 头部无效，握手失败
                return;
            }

            auto peer = hs_data->transport.session_manager().peer_at(header.value().length);
            if (!peer) {
                return;
            }
            // 启动通信
            peer->pipeline()->run(std::move(hs_data->stream));
        }
    }
    delete hs_data;
}

void embkv::raft::Transport::handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    handshake_data().erase(hs_data);
    delete hs_data;
}

void embkv::raft::Transport::handle_serialize_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    thread_local std::array<std::unique_ptr<Message>, 64> buf;
    auto* ser_data = static_cast<SerializeData*>(w->data);

    if (revents & EV_ERROR) {
        log::console().error("handle_serialize_timeout error : {}", strerror(errno));
    } else if (revents & EV_TIMEOUT) {
        auto& queue = ser_data->transport.to_pipeline_deser_queue();
        auto count = queue.try_dequeue_bulk(buf.data(), buf.size());
        for (auto i = 0; i < count; ++i) {
            auto ser_str = std::make_shared<std::string>(buf[i]->SerializeAsString());

        }
    }
}

auto embkv::raft::Transport::accept_data() noexcept
-> std::unordered_set<AcceptData*>& {
    thread_local std::unordered_set<AcceptData*> data;
    return data;
}

auto embkv::raft::Transport::handshake_data() noexcept
-> std::unordered_set<HandshakeData*>& {
    thread_local std::unordered_set<HandshakeData*> data;
    return data;
}

void embkv::raft::Transport::accept_loop(struct ev_loop* loop) {
    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse("0.0.0.0", 8080, addr, ec)) {
        return;
    }

    auto listener = socket::net::TcpListener::bind(addr);
    if (!listener.is_valid()) {
        return;
    }
    auto* ac_data = new AcceptData{std::move(listener), *this};
    accept_data().emplace(ac_data);
    ac_data->loop = loop;
    ac_data->io_watcher.data = ac_data;
    ev_io_init(&ac_data->io_watcher, accept_cb, ac_data->listener.fd(), EV_READ);
    ev_io_start(loop, &ac_data->io_watcher);
    ev_run(loop, 0);
    clear_data();
}

void embkv::raft::Transport::serialize_loop(struct ev_loop* loop) {

}

void embkv::raft::Transport::try_connect_to_peer(uint64_t id) {
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
    header.length = node_id_;
    if (detail::HeadManager::serialize(header)) {
        auto& buffer = detail::HeadManager::buffer();
        stream.write_exact(buffer.data(), buffer.size());
        peer->pipeline()->run(std::move(stream));
    }
}

void embkv::raft::Transport::start_handshake(struct ev_loop* loop, socket::net::TcpStream&& stream, socket::net::SocketAddr addr) {
    auto* hs_data = new HandshakeData{std::move(stream), *this, addr};
    handshake_data().emplace(hs_data);
    hs_data->loop = loop;
    // 开始握手
    hs_data->io_watcher.data = hs_data;
    ev_io_init(&hs_data->io_watcher, handshake_cb, hs_data->stream.fd(), EV_READ);
    ev_io_start(loop, &hs_data->io_watcher);

    // 启动握手定时器（3s）
    hs_data->timer_watcher.data = hs_data;
    ev_timer_init(&hs_data->timer_watcher, handle_handshake_timeout, 3.0, 0.0);
    ev_timer_start(loop, &hs_data->timer_watcher);
}

void embkv::raft::Transport::clear_data() noexcept {
    for (auto data : accept_data()) {
        delete data;
    }
    accept_data().clear();
    for (auto data : handshake_data()) {
        delete data;
    }
    handshake_data().clear();
}
