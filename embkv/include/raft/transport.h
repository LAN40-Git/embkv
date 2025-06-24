#pragma once
#include <ev.h>
#include <utility>
#include "raft/session_manager.h"

namespace embkv::raft
{
class Transport {
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    explicit Transport(uint64_t id, std::string name, std::string ip, uint16_t port, detail::SessionManager::PeerMap peers)
        : id_(id), name_(std::move(name)), ip_(std::move(ip)), port_(port), sess_mgr_(std::move(peers)) {}

private:
    void accept_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    void try_connect_to_peer(uint64_t peer_id);

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
};
} // namespace embkv::raft