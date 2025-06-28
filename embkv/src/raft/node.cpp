#include "raft/node.h"
#include "common/util/random.h"
#include "common/util/time.h"

void embkv::raft::detail::RaftStatus::increase_term_to(uint64_t new_term) {
    current_term_ = new_term;
}

void embkv::raft::detail::RaftStatus::become_leader() {
    role_ = Role::Leader;
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
    auto& node = hb_data->node;

    if (revents & EV_TIMEOUT) {
        if (node.role() == detail::RaftStatus::Role::Leader) {
            node.heartbeat();
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_heartbeat_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::RaftNode::handle_parse_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    thread_local std::array<Message, 64> buf;
    auto* pr_data = static_cast<ParseData*>(w->data);
    auto& node = pr_data->node;
    auto& to_raftnode_deser_queue = node.transport_->to_raftnode_deser_queue();
    auto& to_pipeline_deser_queue = node.transport_->to_pipeline_deser_queue();

    if (revents & EV_TIMEOUT) {
        auto count = to_raftnode_deser_queue.try_dequeue_bulk(buf.data, buf.size());
        for (auto i = 0; i < count; ++i) {
            auto& msg = buf[i];
            switch (msg.content_case()) {
                case Message::kRequestVoteRequest: {
                    node.handle_request_vote_request(msg, to_pipeline_deser_queue);
                }
                case Message::kRequestVoteResponse: {
                    node.handle_request_vote_response(msg, to_pipeline_deser_queue);
                }
                case Message::kAppendEntriesRequest: {
                    node.handle_append_entries_request(msg, to_pipeline_deser_queue);
                }
                case Message::kAppendEntriesResponse: {
                    node.handle_append_entries_response(msg, to_pipeline_deser_queue);
                }
                default: break;
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_parse_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::RaftNode::handle_request_vote_request(Message& msg, DeserQueue& queue) {
    if (msg.cluster_id() != config_.cluster_id()) {
        return;
    }
    auto request_vote_request = msg.mutable_request_vote_request();
    auto term = request_vote_request->term();
    auto last_log_index = request_vote_request->last_log_index();
    auto last_log_term = request_vote_request->last_log_term();


}

void embkv::raft::RaftNode::handle_request_vote_response(Message& msg, DeserQueue& queue) {

}

void embkv::raft::RaftNode::handle_append_entries_request(Message& msg, DeserQueue& queue) {

}

void embkv::raft::RaftNode::handle_append_entries_response(Message& msg, DeserQueue& queue) {

}

void embkv::raft::RaftNode::event_loop() {
    uint64_t delay = util::FastRand::instance().rand_range(150, 300) / 1000.0;
    auto* el_data = new ElectionData{*this, delay};
    auto* hb_data = new HeartbeatData{*this};
    auto* pr_data = new ParseData{*this};
    struct ev_timer election_watcher{};
    struct ev_timer heartbeat_watcher{};
    struct ev_timer parse_watcher{};
    election_watcher.data = el_data;
    heartbeat_watcher.data = hb_data;
    parse_watcher.data = pr_data;
    ev_timer_init(&election_watcher, handle_election_timeout, delay, 0);
    ev_timer_init(&heartbeat_watcher, handle_heartbeat_timeout, 0, 0.05);
    ev_timer_init(&parse_watcher, handle_parse_timeout, 0, 0.003);
    ev_timer_start(loop_, &election_watcher);
    ev_timer_start(loop_, &heartbeat_watcher);
    ev_timer_start(loop_, &parse_watcher);
    ev_run(loop_, 0);
    ev_timer_stop(loop_, &election_watcher);
    ev_timer_stop(loop_, &heartbeat_watcher);
    ev_timer_stop(loop_, &parse_watcher);
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
   Message msg;
    msg.set_cluster_id(cluster_id());
    msg.set_node_id(node_id());
    auto* request_vote_request = msg.mutable_request_vote_request();
    request_vote_request->set_term(current_term());
    request_vote_request->set_last_log_index(last_log_index());
    request_vote_request->set_last_log_term(last_log_term());
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}

void embkv::raft::RaftNode::heartbeat() {
    Message msg;
    msg.set_cluster_id(cluster_id());
    msg.set_node_id(node_id());
    auto* append_entries_request = msg.mutable_append_entries_request();
    append_entries_request->set_term(current_term());
    append_entries_request->set_is_heartbeat(true);
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}
