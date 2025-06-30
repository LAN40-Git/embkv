#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <unordered_map>
#include "raft/peer/peer.h"
#include "socket/net/stream.h"

namespace embkv::client
{
class Client {
public:
    struct RaftNodeInfo {
        std::string ip;
        uint16_t port;
        explicit RaftNodeInfo(std::string ip, uint16_t port) : ip{ip}, port{port} {}
    };

    struct ReadData {
        Client& client;
        explicit ReadData(Client& client) : client{client} {}
    };
    using EndPointMap = std::unordered_map<uint64_t, std::unique_ptr<RaftNodeInfo>>;
    explicit Client(uint64_t id, EndPointMap&& endpoints)
        : id_{id}, endpoints_{std::move(endpoints)} {
        loop_ = ev_loop_new(EVFLAG_AUTO);
    }
    ~Client() {
        ev_io_stop(loop_, &read_watcher_);
        ev_loop_destroy(loop_);
    }

public:
    void run();
    void stop();
    auto is_running() const noexcept -> bool { return is_running_.load(std::memory_order_relaxed); }
    void event_loop();

public:
    auto id() const noexcept -> uint64_t { return id_; }

public:
    static void read_cb(struct ev_loop* loop, struct ev_io* w, int revents);

public:
    auto connect_to_server(uint64_t id) const -> socket::net::TcpStream;
    void redirect_to_leader(uint64_t leader_hint);
    auto put(const std::string& key, const std::string& value) -> bool;
    auto get(const std::string& key) -> boost::optional<std::string>;
    auto del(const std::string& key) -> bool;

private:
    std::atomic<bool>        is_running_{false};
    std::mutex               run_mutex_;
    uint64_t                 id_;
    uint64_t                 leader_id_{0};
    socket::net::TcpStream   stream_{socket::detail::Socket{-1}};
    std::mutex               mutex_{};
    EndPointMap              endpoints_;
    struct ev_loop*          loop_{nullptr};
    ev_io                    read_watcher_{};
    boost::asio::thread_pool pool_{1};
};
} // namespace embkv::client
