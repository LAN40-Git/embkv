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
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    explicit Transport(uint64_t id, std::string name, std::string ip, uint16_t port, detail::SessionManager::PeerMap peers)
        : id_(id)
        , name_(std::move(name))
        , ip_(std::move(ip))
        , port_(port)
        , sess_mgr_(std::move(peers)) {}

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool { return is_running_.load(std::memory_order_relaxed); }

public:
    auto id() const noexcept -> uint64_t { return id_; }
    auto name() const noexcept -> std::string { return name_; }
    auto ip() const noexcept -> std::string { return ip_; }
    auto port() const noexcept -> uint16_t { return port_; }
    auto session_manager() noexcept -> detail::SessionManager& { return sess_mgr_; }

private:
    // ====== callback ======
    // 接收连接回调，当有客户端连接时触发此回调
    static void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    // 握手回调，当对端发送握手消息时触发此回调
    // 若握手成功则会启动对应的pipeline
    static void handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    // 握手超时回调，当握手超时时触发此回调
    static void handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    static auto accept_data() noexcept -> std::unordered_set<AcceptData*>&;
    static auto handshake_data() noexcept -> std::unordered_set<HandshakeData*>&;
    static void add_accept_data(AcceptData* data) noexcept;
    static void remove_accept_data(AcceptData* data) noexcept;
    static void add_handshake_data(HandshakeData* data) noexcept;
    static void remove_handshake_data(HandshakeData* data) noexcept;

private:
    // ====== loop ======
    void accept_loop(struct ev_loop* loop);

private:
    void try_connect_to_peer(uint64_t id);
    void start_handshake(struct ev_loop* loop, socket::net::TcpStream&& stream, socket::net::SocketAddr addr);

private:
    detail::SessionManager   sess_mgr_;
    std::atomic<bool>        is_running_{false};
    std::mutex               run_mutex_;
    uint64_t                 id_;
    std::string              name_;
    std::string              ip_;
    uint16_t                 port_;
    boost::asio::thread_pool pool_{4};
    std::unordered_set<struct ev_loop*> loops_;

    // ====== 消息缓冲 ======
    DeserQueue free_deser_queue_;
    DeserQueue to_raftnode_deser_queue_; // 存放Pipeline接收的消息
    DeserQueue to_pipeline_deser_queue_; // 存放RaftNode生产的（广播）消息
};
} // namespace embkv::raft