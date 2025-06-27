#include "raft/node.h"
#include "common/util/random.h"
#include "common/util/time.h"

void embkv::raft::detail::RaftStatus::increase_term_to(uint64_t new_term) {
    current_term_ = new_term;
}

void embkv::raft::RaftNode::run() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    boost::asio::post(pool_, [this]() {
        event_loop();
    });
}

void embkv::raft::RaftNode::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (!is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    ev_break(loop_, EVBREAK_ALL);
    pool_.wait();
}

void embkv::raft::RaftNode::handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto random_delay = []() {
        return util::FastRand::instance().rand_range(150, 300) / 1000.0;
    };
    auto* el_data = static_cast<ElectionData*>(w->data);

    if (revents & EV_ERROR) {
        log::console().error("handle_election_timeout error : {}", strerror(errno));
    } else if (revents & EV_TIMEOUT) {
        auto now = util::current_ms();
        if (now - el_data->node.st_.last_heartbeat_ >= now - el_data->start) {
            // 只有当上次心跳与当前时间的间隔大于等于选举超时间隔时才进行选举
            el_data->node.start_election();
        }
        el_data->start = now;
    }

    // 重启定时器
    ev_timer_set(w, random_delay(), 0.0);
    ev_timer_start(loop, w);
}

void embkv::raft::RaftNode::handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* hb_data = static_cast<HeartbeatData*>(w->data);

    if (revents & EV_ERROR) {
        log::console().error("handle_heartbeat_timeout error : {}", strerror(errno));
    } else if (revents & EV_TIMEOUT) {

    }
}

void embkv::raft::RaftNode::event_loop() {
    uint64_t delay = util::FastRand::instance().rand_range(150, 300) / 1000.0;
    auto* el_data = new ElectionData{*this, delay};
    auto* hb_data = new HeartbeatData{*this};
    election_watcher_.data = el_data;
    heartbeat_watcher_.data = hb_data;
    ev_timer_init(&election_watcher_, handle_election_timeout, delay, 0);
    ev_timer_init(&heartbeat_watcher_, handle_heartbeat_timeout, 0, 0.05);
    ev_timer_start(loop_, &election_watcher_);
    ev_timer_start(loop_, &heartbeat_watcher_);
    ev_run(loop_, 0);
    ev_timer_stop(loop_, &election_watcher_);
    ev_timer_stop(loop_, &heartbeat_watcher_);
    delete el_data;
    delete hb_data;
}

void embkv::raft::RaftNode::start_election() {
    if (role() != detail::RaftStatus::Role::Follower &&
        role() != detail::RaftStatus::Role::Candidate) {
        return;
    }
    // 更新状态
    st_.increase_term_to(st_.current_term_++);
    st_.role_ = detail::RaftStatus::Role::Candidate;
    st_.voted_for_ = node_id();
    st_.votes_ = 1;

    // 构造投票请求消息
    std::unique_ptr<Message> msg;
    if (free_deser_queue_.try_dequeue(msg)) {
        msg->Clear();
    } else {
        msg = std::make_unique<Message>();
    }
    msg->set_cluster_id(cluster_id());
    msg->set_node_id(node_id());
    auto* request_vote_request = msg->mutable_request_vote_request();
    request_vote_request->set_term(current_term());
    request_vote_request->set_last_log_index(last_log_index());
    request_vote_request->set_last_log_term(last_log_term());
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}

void embkv::raft::RaftNode::try_heartbeat() {
    if (st_.role_ != detail::RaftStatus::Role::Leader) {
        return;
    }
    std::unique_ptr<Message> msg;
    if (free_deser_queue_.try_dequeue(msg)) {
        msg->Clear();
    } else {
        msg = std::make_unique<Message>();
    }
    msg->set_cluster_id(cluster_id());
    msg->set_node_id(node_id());
    auto* append_entries_request = msg->mutable_append_entries_request();
    append_entries_request->set_term(current_term());
    append_entries_request->set_is_heartbeat(true);
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}
