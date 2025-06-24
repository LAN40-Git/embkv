#include "raft/node.h"

void embkv::raft::RaftNode::run() noexcept {
    if (is_running_.load(std::memory_order_relaxed)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
}

void embkv::raft::RaftNode::stop() noexcept {
    if (!is_running_.load(std::memory_order_relaxed)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
}

void embkv::raft::RaftNode::handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {

}

void embkv::raft::RaftNode::handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
}
