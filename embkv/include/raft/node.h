#pragma once
#include "raft/log.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include "transport/transport.h"
#include "statemachine.h"

namespace embkv::raft
{
namespace detail
{
class RaftStatus {
public:
    enum class Role { Follower, Candidate, Leader, Learner, Unknown};
    RaftStatus() = default;

public:
    void increase_term_to(uint64_t new_term);
    void become_leader();
    void handle_higher_term(uint64_t term);
    void voted_for(boost::optional<uint64_t> id);
    void update_next_index(uint64_t id, uint64_t index);
    void update_match_index(uint64_t id, uint64_t index);


public:
    // 持久化状态
    uint64_t                  current_term_{0};
    boost::optional<uint64_t> voted_for_{boost::none};

    // 易失性状态
    Role                      role_{Role::Follower};
    boost::optional<uint64_t> leader_id_{boost::none};
    uint64_t                  commit_index_{0};
    uint64_t                  last_applied_{0};
    uint64_t                  votes_{0};
    uint64_t                  last_heartbeat_{0};

    // Leader 状态
    std::unordered_map<uint64_t, uint64_t> next_index_{};
    std::unordered_map<uint64_t, uint64_t> match_index_{};
};
} // detail

class RaftNode {
    using Priority = util::detail::Priority;
    using DeserQueue = util::PriorityQueue<Message>;
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

    struct ParseData {
        RaftNode& node;
        explicit ParseData(RaftNode& n) : node(n) {}
    };

    struct ReissueData {
        RaftNode& node;
        explicit ReissueData(RaftNode& n) : node(n) {}
    };

    struct Request {
        uint64_t client_id{0};
        uint64_t request_id{0};
        Request() = default;
        explicit Request(uint64_t c, uint64_t r) : client_id(c), request_id(r) {}
    };

    explicit RaftNode(std::shared_ptr<Transport> transport)
        : config_(Config::load())
        , transport_(transport) {
        loop_ = ev_loop_new(EVFLAG_AUTO);
    }
    ~RaftNode() noexcept {
        stop();
        ev_loop_destroy(loop_);
    }

public:
    void run() noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool {
        return is_running_.load(std::memory_order_relaxed);
    }

public:
    // status func
    auto cluster_id() const noexcept -> uint64_t { return config_.cluster_id; }
    auto node_id() const noexcept -> uint64_t { return config_.node_id; }
    auto current_term() const noexcept -> uint64_t { return st_.current_term_; }
    auto leader_id() const noexcept -> uint64_t { return st_.leader_id_.value_or(0); }
    auto role() const noexcept -> detail::RaftStatus::Role { return st_.role_; }
    auto last_applied() const noexcept -> uint64_t { return st_.last_applied_; }
    auto commit_index() const noexcept -> uint64_t { return st_.commit_index_; }
    auto last_log_index() const noexcept -> uint64_t { return log_.last_log_index(); }
    auto last_log_term() const noexcept -> uint64_t { return log_.last_log_term(); }

public:
    // timer func
    static void handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_parse_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);
    static void handle_reissue_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    // rpc func
    void handle_request_vote_request(Message& msg);
    void handle_request_vote_response(Message& msg);
    void handle_append_entries_request(Message& msg);
    void handle_append_entries_response(Message& msg);
    void handle_install_snapshot_request(Message& msg);
    void handle_install_snapshot_response(Message& msg);
    void handle_client_request(Message& msg);

private:
    // raft func
    void event_loop();
    void start_election();
    void heartbeat();
    void reset_election_timer();
    void send_to_pipeline(uint64_t id, Message& msg);
    void apply_to_state_machine(uint64_t commit_index);

private:
    const Config&              config_;
    std::atomic<bool>          is_running_{false};
    std::mutex                 run_mutex_;
    detail::RaftStatus         st_;
    // TODO: 从持久化的日志中加载起点
    detail::RaftLog            log_;
    std::unordered_map<uint64_t, Request> requests_;
    std::shared_ptr<Transport> transport_;
    boost::asio::thread_pool   pool_{1};
    StateMachine               state_machine_{};
    struct ev_loop*            loop_{nullptr};
    struct ev_timer            election_watcher_{};
    struct ev_timer            heartbeat_watcher_{};
    struct ev_timer            parse_watcher_{};
    struct ev_timer            reissue_watcher_{}; // 补发日志计时器
};
} // namespace embkv::raft
