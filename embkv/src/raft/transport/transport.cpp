#include "raft/transport/transport.h"
#include "raft/transport/head_manager.h"

void embkv::raft::Transport::run() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    // 监听循环
    struct ev_loop* ac_loop = ev_loop_new(EVFLAG_AUTO);
    works_.emplace(ac_loop, std::thread([this, ac_loop] {
        this->accept_loop(ac_loop);
    }));
}

void embkv::raft::Transport::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    for (auto& work : works_) {
        ev_break(work.first, EVBREAK_ALL);
        if (work.second.joinable()) {
            work.second.join();
        }
    }
    works_.clear();
}

void embkv::raft::Transport::accept_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto* ac_data = static_cast<AcceptData*>(w->data);
    if (revents & EV_ERROR) {
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
            ac_data->transport.start_handshake(loop, std::move(stream), peer_addr);
        }
    }
}

void embkv::raft::Transport::handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    auto it = handshake_data().find(static_cast<HandshakeData*>(w->data));
    if (it == handshake_data().end()) {
        return;
    }
    auto hs_data = std::move(it->second); // 接管智能指针
    remove_handshake_data(it->first);
    if (revents & EV_ERROR) {
        return;
    }

    if (revents & EV_READ) {
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
}

void embkv::raft::Transport::handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    remove_handshake_data(hs_data);
}

auto embkv::raft::Transport::accept_data() noexcept
-> std::unordered_map<AcceptData*, std::unique_ptr<AcceptData>>& {
    thread_local std::unordered_map<AcceptData*, std::unique_ptr<AcceptData>> data;
    return data;
}

auto embkv::raft::Transport::handshake_data() noexcept
-> std::unordered_map<HandshakeData*, std::unique_ptr<HandshakeData>>& {
    thread_local std::unordered_map<HandshakeData*, std::unique_ptr<HandshakeData>> data;
    return data;
}

void embkv::raft::Transport::add_accept_data(AcceptData* data) noexcept {
    std::unique_ptr<AcceptData> ptr(data);
    accept_data().emplace(data, std::move(ptr));
}

void embkv::raft::Transport::remove_accept_data(AcceptData* data) noexcept {
    accept_data().erase(data);
}

void embkv::raft::Transport::add_handshake_data(HandshakeData* data) noexcept {
    std::unique_ptr<HandshakeData> ptr(data);
    handshake_data().emplace(data, std::move(ptr));
}

void embkv::raft::Transport::remove_handshake_data(HandshakeData* data) noexcept {
    handshake_data().erase(data);
}

void embkv::raft::Transport::accept_loop(struct ev_loop* loop) noexcept {
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
    add_accept_data(ac_data);
    ac_data->loop = loop;
    ac_data->io_watcher.data = ac_data;
    ev_io_init(&ac_data->io_watcher, accept_cb, ac_data->listener.fd(), EV_READ);
    ev_io_start(loop, &ac_data->io_watcher);
    ev_run(loop, 0);
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

void embkv::raft::Transport::start_handshake(struct ev_loop* loop, socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept {
    auto* hs_data = new HandshakeData{std::move(stream), *this, addr};
    add_handshake_data(hs_data);
    // 开始握手
    hs_data->io_watcher.data = hs_data;
    ev_io_init(&hs_data->io_watcher, handshake_cb, hs_data->stream.fd(), EV_READ);
    ev_io_start(loop, &hs_data->io_watcher);

    // 启动握手定时器（3s）
    hs_data->timer_watcher.data = hs_data;
    ev_timer_init(&hs_data->timer_watcher, handle_handshake_timeout, 3.0, 0.0);
    ev_timer_start(loop, &hs_data->timer_watcher);
}