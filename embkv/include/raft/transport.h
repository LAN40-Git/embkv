#pragma once
#include <ev.h>
#include <utility>
#include "raft/session_manager.h"
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
        explicit HandshakeData(socket::net::TcpStream&& s, Transport& t)
            : stream(std::move(s)), transport(t) {}
    };
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    explicit Transport(uint64_t id, std::string name, std::string ip, uint16_t port, detail::SessionManager::PeerMap peers)
        : id_(id), name_(std::move(name)), ip_(std::move(ip)), port_(port), sess_mgr_(std::move(peers)) {}

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool { return is_running_.load(std::memory_order_relaxed); }

private:
    static void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handshake_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handle_handshake_timeout(struct ev_loop* loop, struct ev_io* w, int revents);

private:
    void start_handshake(socket::net::TcpStream&& stream, socket::net::SocketAddr addr) noexcept;

private:
    std::atomic<bool> is_running_{false};
    std::mutex        run_mutex_;

    // 本地节点信息
    uint64_t id_;
    std::string name_;
    std::string ip_;
    uint16_t port_;

    // 会话管理
    detail::SessionManager sess_mgr_;

    struct ev_loop* loop{nullptr};
    ev_io accept_watcher_{};
};
} // namespace embkv::raft