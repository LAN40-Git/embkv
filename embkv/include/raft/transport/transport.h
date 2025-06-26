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
        socket::net::SocketAddr peer_addr;

        explicit HandshakeData(socket::net::TcpStream&& s, Transport& t, socket::net::SocketAddr addr)
            : stream(std::move(s)), transport(t), peer_addr(addr) {}

        ~HandshakeData() {
            ev_io_stop(transport.loop, &io_watcher);
            ev_timer_stop(transport.loop, &timer_watcher);
        }
    };
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    explicit Transport(uint64_t id, std::string name, std::string ip, uint16_t port, detail::SessionManager::PeerMap peers)
        : id_(id)
        , name_(std::move(name))
        , ip_(std::move(ip))
        , port_(port)
        , sess_mgr_(std::move(peers)) {
        loop_ = EV_DEFAULT;
    }

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
    static void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handle_handshake_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    void try_connect_to_peer(uint64_t id) noexcept;
    void start_handshake(socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept;

private:
    detail::SessionManager        sess_mgr_;
    std::atomic<bool>             is_running_{false};
    std::mutex                    run_mutex_;
    uint64_t                      id_;
    std::string                   name_;
    std::string                   ip_;
    uint16_t                      port_;
    struct ev_loop*               loop_{nullptr};
    std::unordered_set<ev_io*>    io_watchers_;
    std::unordered_set<ev_timer*> timer_watchers_;
};
} // namespace embkv::raft