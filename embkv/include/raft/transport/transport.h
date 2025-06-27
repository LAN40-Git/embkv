#pragma once
#include <utility>
#include "session_manager.h"
#include "socket/net/listener.h"

namespace embkv::raft
{
class Transport {
    struct AcceptData {
        socket::net::TcpListener listener;
        Transport& transport;
        explicit AcceptData(socket::net::TcpListener&& l, Transport& t)
            : listener(std::move(l)), transport(t) {}
    };

    struct HandshakeData {
        socket::net::TcpStream stream;
        Transport& transport;
        ev_io io_watcher{};
        ev_timer timer_watcher{};
        struct ev_loop* loop{nullptr};
        socket::net::SocketAddr peer_addr;

        explicit HandshakeData(socket::net::TcpStream&& s, Transport& t, socket::net::SocketAddr addr)
            : stream(std::move(s)), transport(t), peer_addr(addr) {}
        ~HandshakeData() {
            ev_io_stop(loop, &io_watcher);
            ev_timer_stop(loop, &timer_watcher);
        }
    };

    struct ConnectData {
        Transport& transport;
        ev_io io_watcher{};
        explicit ConnectData(Transport& t) : transport(t) {}
    };

    struct SerializeData {
        Transport& transport;
        explicit SerializeData(Transport& t) : transport(t) {}
    };

    struct ReceiveData {
        Transport& transport;
        explicit ReceiveData(Transport& t) : transport(t) {}
    };

public:
    using DeserQueue = util::PriorityQueue<Message>;
    explicit Transport(detail::SessionManager::PeerMap&& peers)
        : config_(Config::load()), sess_mgr_(std::move(peers)) {}

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool { return is_running_.load(std::memory_order_relaxed); }

public:
    auto cluster_id() const noexcept -> uint64_t { return config_.cluster_id; }
    auto node_id() const noexcept -> uint64_t { return config_.node_id; }
    auto node_name() const noexcept -> std::string { return config_.node_name; }
    auto ip() const noexcept -> std::string { return config_.ip; }
    auto port() const noexcept -> uint16_t { return config_.port; }
    auto session_manager() noexcept -> detail::SessionManager& { return sess_mgr_; }
    auto to_raftnode_deser_queue() noexcept -> DeserQueue& { return to_raftnode_deser_queue_; }
    auto to_pipeline_deser_queue() noexcept -> DeserQueue& { return to_pipeline_deser_queue_; }

private:
    static void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_serialize_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_receive_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    static auto handshake_data() noexcept -> std::unordered_set<HandshakeData*>&;

private:
    // ====== loop ======
    void accept_loop();
    void serialize_loop();
    void receive_loop();

private:
    void try_connect_to_peer(uint64_t id);
    void start_handshake(struct ev_loop* loop, socket::net::TcpStream&& stream, socket::net::SocketAddr addr);

private:
    const Config&            config_;
    detail::SessionManager   sess_mgr_;
    std::atomic<bool>        is_running_{false};
    std::mutex               run_mutex_;
    boost::asio::thread_pool pool_{4};
    std::unordered_set<struct ev_loop*> loops_;

    // ====== 消息缓冲 ======
    DeserQueue to_raftnode_deser_queue_; // 存放Pipeline接收的消息
    DeserQueue to_pipeline_deser_queue_; // 存放RaftNode生产的（广播）消息
};
} // namespace embkv::raft