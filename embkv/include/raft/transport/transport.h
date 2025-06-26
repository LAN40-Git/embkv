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
        ev_io io_watcher{};
        struct ev_loop* loop{nullptr};
        explicit AcceptData(socket::net::TcpListener&& l, Transport& t)
            : listener(std::move(l)), transport(t) {}
        ~AcceptData() {
            ev_io_stop(loop, &io_watcher);
        }
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

    struct SerializeData {
        Transport& transport;
        explicit SerializeData(Transport& t) : transport(t) {}
    };

public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    using FreeQueue = moodycamel::ConcurrentQueue<std::unique_ptr<Message>>;
    explicit Transport(uint64_t cluster_id, uint64_t node_id, std::string name, std::string ip,
        uint16_t port, detail::SessionManager::PeerMap peers)
        : sess_mgr_(std::move(peers))
        , cluster_id_(cluster_id)
        , node_id_(node_id)
        , name_(std::move(name))
        , ip_(std::move(ip))
        , port_(port) {}

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool { return is_running_.load(std::memory_order_relaxed); }

public:
    auto cluster_id() const noexcept -> uint64_t { return cluster_id_; }
    auto node_id() const noexcept -> uint64_t { return node_id_; }
    auto name() const noexcept -> std::string { return name_; }
    auto ip() const noexcept -> std::string { return ip_; }
    auto port() const noexcept -> uint16_t { return port_; }
    auto session_manager() noexcept -> detail::SessionManager& { return sess_mgr_; }
    auto free_deser_queue() noexcept -> FreeQueue& { return free_deser_queue_; }
    auto to_raftnode_deser_queue() noexcept -> DeserQueue& { return to_raftnode_deser_queue_; }
    auto to_pipeline_deser_queue() noexcept -> DeserQueue& { return to_pipeline_deser_queue_; }

private:
    static void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_serialize_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    static auto accept_data() noexcept -> std::unordered_set<AcceptData*>&;
    static auto handshake_data() noexcept -> std::unordered_set<HandshakeData*>&;
    static void clear_data() noexcept;

private:
    // ====== loop ======
    void accept_loop(struct ev_loop* loop);
    void serialize_loop(struct ev_loop* loop);

private:
    void try_connect_to_peer(uint64_t id);
    void start_handshake(struct ev_loop* loop, socket::net::TcpStream&& stream, socket::net::SocketAddr addr);

private:
    detail::SessionManager   sess_mgr_;
    std::atomic<bool>        is_running_{false};
    std::mutex               run_mutex_;
    uint64_t                 cluster_id_{0};
    uint64_t                 node_id_{0};
    std::string              name_;
    std::string              ip_;
    uint16_t                 port_;
    boost::asio::thread_pool pool_{4};
    std::unordered_set<struct ev_loop*> loops_;

    // ====== 消息缓冲 ======
    FreeQueue  free_deser_queue_;
    DeserQueue to_raftnode_deser_queue_; // 存放Pipeline接收的消息
    DeserQueue to_pipeline_deser_queue_; // 存放RaftNode生产的（广播）消息
};
} // namespace embkv::raft