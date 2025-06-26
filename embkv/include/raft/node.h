#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>

#include "transport/transport.h"

namespace embkv::raft
{
namespace detail
{
class RaftStatus {
public:
    enum class Role { Follower, Candidate, Leader, Learner, Unknown};
    explicit RaftStatus(uint64_t cluster_id, uint64_t node_id)
        : cluster_id_(cluster_id), node_id_(node_id) {}

public:
    void increase_term_to(uint64_t new_term);


public:
    uint64_t                  cluster_id_{0};
    uint64_t                  node_id_{0};
    // 持久化状态
    uint64_t                  current_term_{0};
    boost::optional<uint64_t> voted_for_{boost::none};

    // 易失性状态
    Role                      role_{Role::Follower};
    boost::optional<uint64_t> leader_id_{boost::none};
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
    using Priority = util::detail::Priority;
public:
    struct ElectionData {
        RaftNode& node;
        uint64_t  start{0};
        explicit ElectionData(RaftNode& n, uint64_t s) : node(n), start(s) {}
    };

    struct HeartbeatData {
        RaftNode& node;
        explicit HeartbeatData(RaftNode& n) : node(n) {}
    };

    explicit RaftNode(const std::shared_ptr<Transport>& transport)
        : st_(transport->cluster_id(), transport->node_id())
        , transport_(transport) {
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
    // status
    auto cluster_id() const noexcept -> uint64_t { return st_.cluster_id_; }
    auto node_id() const noexcept -> uint64_t { return st_.node_id_; }
    auto current_term() const noexcept -> uint64_t { return st_.current_term_; }
    auto last_applied() const noexcept -> uint64_t { return st_.last_applied_; }
    auto last_log_index() const noexcept -> uint64_t { return st_.last_log_index_; }
    auto last_log_term() const noexcept -> uint64_t { return st_.last_log_term_; }

public:
    // timer func
    static void handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    void event_loop();
    void start_election();
    void try_heartbeat() const;

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
