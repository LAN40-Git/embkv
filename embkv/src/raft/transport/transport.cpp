#include "raft/transport/transport.h"

void embkv::raft::Transport::run() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    // 监听循环
    boost::asio::post(pool_, [this]() {
        accept_loop();
    });
    // 解析数据循环
    boost::asio::post(pool_, [this]() {
        serialize_loop();
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
    thread_local std::array<std::unique_ptr<Message>, 64> deser_buf;
    auto* ser_data = static_cast<SerializeData*>(w->data);
    auto& transport = ser_data->transport;
    auto& peers = transport.session_manager().peers();

    if (revents & EV_TIMEOUT) {
        auto& queue = ser_data->transport.to_pipeline_deser_queue();
        auto count = queue.try_dequeue_bulk(deser_buf.data(), deser_buf.size());
        std::array<std::shared_ptr<std::string>, 64> ser_buf;
        for (auto i = 0; i < count; ++i) {
            const auto& msg = *deser_buf[i];
            size_t body_size = msg.ByteSizeLong();
            size_t total_size = sizeof(detail::HeadManager::Header) + body_size;

            ser_buf[i] = std::make_shared<std::string>();
            ser_buf[i]->resize(total_size);

            detail::HeadManager::Header header;
            header.flags = detail::HeadManager::Flags::kRaftNode;
            header.length = total_size;
            memcpy(const_cast<char*>(ser_buf[i]->data()), &header, sizeof(header));

            msg.SerializeToArray(const_cast<char*>(ser_buf[i]->data() + sizeof(header)), body_size);
        }
        // 广播消息到活跃pipeline，同时尝试连接id大于自己且非活跃的pipeline
        for (auto& peer : peers) {
            auto pipeline = peer.second->pipeline();
            if (pipeline->is_running()) {
                pipeline->from_ser_queue().enqueue_bulk(ser_buf.data(), count);
            } else if (peer.first > transport.node_id()) {
                transport.try_connect_to_peer(peer.first);
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_serialize_timeout error : {}", strerror(errno));
    }
}

auto embkv::raft::Transport::handshake_data() noexcept
-> std::unordered_set<HandshakeData*>& {
    thread_local std::unordered_set<HandshakeData*> data;
    return data;
}

void embkv::raft::Transport::accept_loop() {
    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse(config_.ip, config_.port, addr, ec)) {
        log::console().error("Failed to parse addr : {}", ec.message());
        return;
    }

    auto listener = socket::net::TcpListener::bind(addr);
    if (!listener.is_valid()) {
        log::console().error("Failed to bind listener");
        return;
    }
    struct ev_loop* ac_loop = ev_loop_new(EVFLAG_AUTO);
    loops_.insert(ac_loop);
    ev_io ac_watcher;
    auto* ac_data = new AcceptData{std::move(listener), *this};
    ac_watcher.data = ac_data;
    ev_io_init(&ac_watcher, accept_cb, ac_data->listener.fd(), EV_READ);
    ev_io_start(ac_loop, &ac_watcher);
    ev_run(ac_loop, 0);
    ev_io_stop(ac_loop, &ac_watcher);
    // 清理资源
    delete ac_data;
    for (auto data : handshake_data()) {
        delete data;
    }
    ev_loop_destroy(ac_loop);
}

void embkv::raft::Transport::serialize_loop() {
    // 每1ms进行一次序列化(执行一次handle_serialize_timeout)
    struct ev_loop* ser_loop = ev_loop_new(EVFLAG_AUTO);
    loops_.insert(ser_loop);
    ev_timer ser_watcher;
    auto* ser_data = new SerializeData{*this};
    ser_watcher.data = ser_data;
    ev_timer_init(&ser_watcher, handle_serialize_timeout, 0, 0.001);
    ev_timer_start(ser_loop, &ser_watcher);
    ev_run(ser_loop, 0);
    ev_timer_stop(ser_loop, &ser_watcher);
    // 清理资源
    delete ser_data;
    ev_loop_destroy(ser_loop);
}

void embkv::raft::Transport::try_connect_to_peer(uint64_t id) {
    auto peer = sess_mgr_.peer_at(id);
    if (!peer) {
        return;
    }
    if (!peer->pipeline()->try_get_connect_mutex()) {
        return;
    }
    if (peer->pipeline()->is_running()) {
        peer->pipeline()->release_connect_mutex();
        return;
    }

    socket::net::SocketAddr addr{};
    std::error_code ec;
    if (!socket::net::SocketAddr::parse(peer->ip(), peer->port(), addr, ec)) {
        peer->pipeline()->release_connect_mutex();
        return;
    }

    auto stream = socket::net::TcpStream::connect(addr);
    if (!stream.is_valid()) {
        peer->pipeline()->release_connect_mutex();
        return;
    }
    // 发送握手消息
    detail::HeadManager::Header header;
    header.flags = detail::HeadManager::Flags::kRaftNode;
    header.length = config_.node_id;
    if (detail::HeadManager::serialize(header)) {
        auto& buffer = detail::HeadManager::buffer();
        stream.write_exact(buffer.data(), buffer.size());
        peer->pipeline()->run(std::move(stream));
    }
    peer->pipeline()->release_connect_mutex();
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