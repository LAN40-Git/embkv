#include "raft/node.h"
#include "log.h"
#include "common/util/random.h"
#include "common/util/time.h"

void embkv::raft::detail::RaftStatus::increase_term_to(uint64_t new_term) {
    current_term_ = new_term;
}

void embkv::raft::detail::RaftStatus::become_leader() {
    role_ = Role::Leader;
}

void embkv::raft::detail::RaftStatus::handle_higher_term(uint64_t term) {
    increase_term_to(term);
    role_ = Role::Follower;
    votes_ = 0;
    voted_for_ = boost::none;
}

void embkv::raft::detail::RaftStatus::voted_for(boost::optional<uint64_t> id) {
    if (!id.has_value()) {
        voted_for_.reset();
        return;
    }

    voted_for_ = id;
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

    el_data->node.reset_election_timer();
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
    auto& src_queue = node.transport_->to_raftnode_deser_queue();

    if (revents & EV_TIMEOUT) {
        auto count = src_queue.try_dequeue_bulk(buf.data(), buf.size());
        for (auto i = 0; i < count; ++i) {
            auto& msg = buf[i];
            switch (msg.content_case()) {
                case Message::kRequestVoteRequest: {
                    node.handle_request_vote_request(msg);
                    break;
                }
                case Message::kRequestVoteResponse: {
                    node.handle_request_vote_response(msg);
                    break;
                }
                case Message::kAppendEntriesRequest: {
                    node.handle_append_entries_request(msg);
                    break;
                }
                case Message::kAppendEntriesResponse: {
                    node.handle_append_entries_response(msg);
                    break;
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

void embkv::raft::RaftNode::handle_request_vote_request(Message& msg) {
    auto request_vote_request = msg.mutable_request_vote_request();
    auto node_id = msg.node_id();
    auto term = request_vote_request->term();
    auto last_log_index = request_vote_request->last_log_index();
    auto last_log_term = request_vote_request->last_log_term();

    if (msg.cluster_id() != cluster_id() || term < current_term()) {
        return;
    }

    if (term > current_term()) {
        st_.handle_higher_term(term);
    }

    // 检查投票资格
    bool vote_granted = false;
    bool vote_available = !st_.voted_for_.has_value() || st_.voted_for_.value() == node_id;
    bool log_up_to_date = (last_log_term > this->last_log_term()) ||
                         (last_log_term == this->last_log_term() &&
                          last_log_index >= this->last_log_index());

    if (vote_available && log_up_to_date) {
        vote_granted = true;
        st_.voted_for(boost::optional<uint64_t>(node_id));
        reset_election_timer(); // 重置选举超时
        // TODO: 持久化状态
    }

    log::console().info("node_id {} term {} granted {}", node_id, term, vote_granted);

    // 构造并发送投票回复
    Message response;
    response.set_cluster_id(cluster_id());
    response.set_node_id(this->node_id());
    auto request_vote_response = response.mutable_request_vote_response();
    request_vote_response->set_term(term);
    request_vote_response->set_granted(vote_granted);
    transport_->to_pipeline_deser_queue().enqueue(std::move(response), Priority::Critical);
}

void embkv::raft::RaftNode::handle_request_vote_response(Message& msg) {
    auto request_vote_response = msg.mutable_request_vote_response();
    auto node_id = msg.node_id();
    auto term = request_vote_response->term();
    auto granted = request_vote_response->granted();

    if (msg.cluster_id() != cluster_id() || term < current_term()) {
        return;
    }

    if (term > current_term()) {
        st_.handle_higher_term(term);
        return;
    }

    if (granted) {
        st_.votes_++;
        if (st_.votes_ >= transport_->session_manager().peer_count()/2 +1) {
            st_.become_leader();
            log::console().info("Become leader");
        }
    }

}

void embkv::raft::RaftNode::handle_append_entries_request(Message& msg) {
    auto& append_entries_request = msg.append_entries_request();
    auto node_id = msg.node_id();
    auto term = append_entries_request.term();
    auto prev_log_index = append_entries_request.prev_log_index();
    auto prev_log_term = append_entries_request.prev_log_term();
    auto& entries = append_entries_request.entries();
    auto leader_commit = append_entries_request.leader_commit();
    auto is_heartbeat = append_entries_request.is_heartbeat();

    if (msg.cluster_id() != cluster_id() || term < current_term()) {
        return;
    }

    if (term > current_term()) {
        st_.handle_higher_term(term);
    }

    if (is_heartbeat) {
        st_.last_heartbeat_ = util::current_ms();
        st_.leader_id_ = node_id;
        return;
    }

    // TODO: 处理日志复制请求

}

void embkv::raft::RaftNode::handle_append_entries_response(Message& msg) {
    auto& append_entries_response = msg.append_entries_response();
    auto node_id = msg.node_id();
    auto term = append_entries_response.term();
    auto success = append_entries_response.success();
    auto conflict_index = append_entries_response.conflict_index();
    auto last_log_index = append_entries_response.last_log_index();

    // TODO: 处理日志复制回复

}

void embkv::raft::RaftNode::event_loop() {
    uint64_t delay = util::FastRand::instance().rand_range(150, 300) / 1000.0;
    auto* el_data = new ElectionData{*this, delay};
    auto* hb_data = new HeartbeatData{*this};
    auto* pr_data = new ParseData{*this};
    election_watcher_.data = el_data;
    heartbeat_watcher_.data = hb_data;
    parse_watcher_.data = pr_data;
    ev_timer_init(&election_watcher_, handle_election_timeout, delay, 0);
    ev_timer_init(&heartbeat_watcher_, handle_heartbeat_timeout, 0, 0.05);
    ev_timer_init(&parse_watcher_, handle_parse_timeout, 0, 0.003);
    ev_timer_start(loop_, &election_watcher_);
    ev_timer_start(loop_, &heartbeat_watcher_);
    ev_timer_start(loop_, &parse_watcher_);
    ev_run(loop_, 0);
    ev_timer_stop(loop_, &election_watcher_);
    ev_timer_stop(loop_, &heartbeat_watcher_);
    ev_timer_stop(loop_, &parse_watcher_);
    delete el_data;
    delete hb_data;
}

void embkv::raft::RaftNode::start_election() {
    if (role() != detail::RaftStatus::Role::Follower &&
        role() != detail::RaftStatus::Role::Candidate) {
        return;
    }
    // 更新状态
    st_.increase_term_to(current_term()+1);
    st_.role_ = detail::RaftStatus::Role::Candidate;
    st_.voted_for_ = node_id();
    st_.votes_ = 1; // 为自己投票

    // 构造投票请求消息
    Message msg;
    msg.set_cluster_id(cluster_id());
    msg.set_node_id(node_id());
    auto request_vote_request = msg.mutable_request_vote_request();
    request_vote_request->set_term(current_term());
    request_vote_request->set_last_log_index(last_log_index());
    request_vote_request->set_last_log_term(last_log_term());
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}

void embkv::raft::RaftNode::heartbeat() {
    Message msg;
    msg.set_cluster_id(cluster_id());
    msg.set_node_id(node_id());
    auto append_entries_request = msg.mutable_append_entries_request();
    append_entries_request->set_term(current_term());
    append_entries_request->set_is_heartbeat(true);
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}

void embkv::raft::RaftNode::reset_election_timer() {
    auto random_delay = []() {
        return util::FastRand::instance().rand_range(150, 300) / 1000.0;
    };
    ev_timer_set(&election_watcher_, random_delay(), 0.0);
    ev_timer_start(loop_, &election_watcher_);
}
