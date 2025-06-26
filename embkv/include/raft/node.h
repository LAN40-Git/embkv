#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "transport/transport.h"

namespace embkv::raft
{
namespace detail
{
class RaftStatus {
public:
    enum class Role { Follower, Candidate, Leader, Learner, Unknown};
    explicit RaftStatus(uint64_t id) : id_(id) {};

public:
    uint64_t                  id_{0};
    // 持久化状态
    uint64_t                  current_term_{0};
    std::pair<uint64_t, bool> voted_for_{std::make_pair(0, false)};

    // 易失性状态
    Role                      role_{Role::Follower};
    std::pair<uint64_t, bool> leader_id_{std::make_pair(0, false)};
    uint64_t                  commit_index{0};
    uint64_t                  last_applied_{0};
    uint64_t                  votes_{0};
    uint64_t                  last_heartbeat_{0};
    uint64_t                  last_log_index_{0};
    uint64_t                  last_log_term_{0};

    // Leader 状态
    std::unordered_map<uint64_t, uint64_t> next_index_{};
    std::unordered_map<uint64_t, uint64_t> match_index_{};
};
} // detail

class RaftNode {
public:
    struct ElectionData {
        RaftNode& node;
        uint64_t  start{0};
        explicit ElectionData(RaftNode& n, uint64_t s) : node(n), start(s) {}
    };

    explicit RaftNode(uint64_t id, const std::shared_ptr<Transport>& transport)
        : st_(id), transport_(transport) {
        ev_init(&election_watcher_, handle_election_timeout);
        ev_init(&heartbeat_watcher_, handle_heartbeat_timeout);
        loop_ = ev_loop_new(EVFLAG_AUTO);
    }

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool {
        return is_running_.load(std::memory_order_relaxed);
    }

public:
    static void handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    static auto election_data() noexcept -> std::unordered_set<ElectionData*>;
    static void add_election_data(ElectionData* data) noexcept;
    static void remove_election_data(ElectionData* data) noexcept;


private:
    void event_loop();
    void start_election();

private:
    std::atomic<bool>          is_running_{false};
    std::mutex                 run_mutex_;
    detail::RaftStatus         st_;
    std::shared_ptr<Transport> transport_;
    boost::asio::thread_pool   pool_{1};
    struct ev_timer            election_watcher_{};
    struct ev_timer            heartbeat_watcher_{};
    struct ev_loop*            loop_{nullptr};
};
} // namespace embkv::raft
