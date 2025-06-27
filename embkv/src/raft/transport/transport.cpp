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
    // 接收消息循环
    boost::asio::post(pool_, [this]() {
        receive_loop();
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
        auto size = hs_data->stream.read_exact(buffer.data(), buffer.size());
        if (size < sizeof(detail::HeadManager::Header)) {
            delete hs_data;
            return;
        }
        if (auto header = detail::HeadManager::deserialize()) {
            if (!header.has_value()) { // 头部无效，握手失败
                delete hs_data;
                return;
            }

            auto peer = hs_data->transport.session_manager().peer_at(be64toh(header.value().length));
            if (!peer) {
                delete hs_data;
                return;
            }
            // 启动通信
            peer->pipeline()->run(std::move(hs_data->stream));
            log::console().info("Connect to {}", peer->node_id());
        }
    }
    delete hs_data;
}

void embkv::raft::Transport::handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* hs_data = static_cast<HandshakeData*>(w->data);
    handshake_data().erase(hs_data);
    delete hs_data;
    log::console().info("Handshake timeout");
}

void embkv::raft::Transport::handle_serialize_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    thread_local std::array<Message, 64> deser_buf;
    auto* ser_data = static_cast<SerializeData*>(w->data);
    auto& transport = ser_data->transport;
    auto& peers = transport.session_manager().peers();

    if (revents & EV_TIMEOUT) {
        auto& queue = ser_data->transport.to_pipeline_deser_queue();
        auto count = queue.try_dequeue_bulk(deser_buf.data(), deser_buf.size());
        std::array<std::shared_ptr<std::string>, 64> ser_buf;
        for (auto i = 0; i < count; ++i) {
            ser_buf[i] = std::make_shared<std::string>(deser_buf[i].SerializeAsString());
        }
        if (count > 0) {
            // 广播消息到活跃pipeline，同时尝试连接id大于自己且非活跃的pipeline
            for (auto& peer : peers) {
                auto pipeline = peer.second->pipeline();
                if (pipeline->is_running()) {
                    pipeline->from_ser_queue().enqueue_bulk(ser_buf.data(), count);
                } else if (peer.first > transport.node_id()) {
                    transport.try_connect_to_peer(peer.first);
                }
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_serialize_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::Transport::handle_receive_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    thread_local std::array<Message, 64> deser_buf;
    auto* recv_data = static_cast<ReceiveData*>(w->data);
    auto& transport = recv_data->transport;
    auto& sess_mgr = transport.session_manager();
    auto& to_raftnode_deser_queue = transport.to_raftnode_deser_queue();

    if (revents & EV_TIMEOUT) {
        for (auto& peer : sess_mgr.peers()) {
            auto pipeline = peer.second->pipeline();
            if (pipeline->is_running()) {
                auto count = pipeline->to_deser_queue().try_dequeue_bulk(deser_buf.data(), deser_buf.size());
                to_raftnode_deser_queue.enqueue_bulk(std::make_move_iterator(deser_buf.data()), count);
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_receive_timeout error : {}", strerror(errno));
    }
}

auto embkv::raft::Transport::handshake_data() noexcept
-> std::unordered_set<HandshakeData*>& {
    thread_local std::unordered_set<HandshakeData*> data;
    return data;
}

auto embkv::raft::Transport::connect_data() noexcept -> std::unordered_set<ConnectData*>& {
    thread_local std::unordered_set<ConnectData*> data;
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
        log::console().error("Failed to bind {}:{} {}", config_.ip, config_.port, strerror(errno));
        return;
    }
    struct ev_loop* ac_loop = ev_loop_new(EVFLAG_AUTO);
    loops_.insert(ac_loop);
    ev_io ac_watcher;
    auto* ac_data = new AcceptData{std::move(listener), *this};
    ac_watcher.data = ac_data;
    ev_io_init(&ac_watcher, accept_cb, ac_data->listener.fd(), EV_READ);
    ev_io_start(ac_loop, &ac_watcher);
    log::console().info("Transport running on {}:{}", config_.ip, config_.port);
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
    // 每3ms进行一次序列化(执行一次handle_serialize_timeout)
    struct ev_loop* ser_loop = ev_loop_new(EVFLAG_AUTO);
    loops_.insert(ser_loop);
    ev_timer ser_watcher;
    auto* ser_data = new SerializeData{*this};
    ser_watcher.data = ser_data;
    ev_timer_init(&ser_watcher, handle_serialize_timeout, 0, 0.003);
    ev_timer_start(ser_loop, &ser_watcher);
    ev_run(ser_loop, 0);
    ev_timer_stop(ser_loop, &ser_watcher);
    // 清理资源
    delete ser_data;
    ev_loop_destroy(ser_loop);
}

void embkv::raft::Transport::receive_loop() {
    // 每3ms将pipeline中的反序列化消息取走交给raftnode
    struct ev_loop* recv_loop = ev_loop_new(EVFLAG_AUTO);
    loops_.insert(recv_loop);
    ev_timer recv_watcher;
    auto* recv_data = new ReceiveData{*this};
    recv_watcher.data = recv_data;
    ev_timer_init(&recv_watcher, handle_receive_timeout, 0, 0.003);
    ev_timer_start(recv_loop, &recv_watcher);
    ev_run(recv_loop, 0);
    ev_timer_stop(recv_loop, &recv_watcher);
    delete recv_data;
    ev_loop_destroy(recv_loop);
}

void embkv::raft::Transport::try_connect_to_peer(uint64_t id) {
    auto peer = sess_mgr_.peer_at(id);
    if (!peer || !peer->pipeline()->try_get_connect_mutex()) {
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
        log::console().info("Failed to parse addr");
        return;
    }

    auto stream = socket::net::TcpStream::connect(addr);
    if (stream.is_valid()) {
        // 发送握手消息
        detail::HeadManager::Header header {
            .flags = detail::HeadManager::Flags::kRaftNode,
            .length = htobe64(config_.node_id)
        };
        if (detail::HeadManager::serialize(header)) {
            auto& buffer = detail::HeadManager::buffer();
            stream.write_exact(buffer.data(), buffer.size());
            peer->pipeline()->run(std::move(stream));
            log::console().info("Connect to {}", id);
        }
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