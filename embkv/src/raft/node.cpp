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

void embkv::raft::detail::RaftStatus::update_next_index(uint64_t id, uint64_t index) {
    next_index_[id] = index;
}

void embkv::raft::detail::RaftStatus::update_match_index(uint64_t id, uint64_t index) {
    match_index_[id] = index;
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
            // 首先尝试推进状态机
            std::vector<uint64_t> values;
            for (auto& match_index : node.st_.match_index_) {
                values.push_back(match_index.second);
            }
            if (values.empty()) {
                return;
            }
            std::sort(values.begin(), values.end());
            size_t size = values.size();
            size_t index = node.transport_->session_manager().peer_count()-(size/2+1);
            uint64_t value = values[index];
            if (value > node.commit_index()) {
                node.st_.commit_index_ = value;
                node.apply_to_state_machine(value);
            }
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
                case Message::kRequestVoteRequest:
                    node.handle_request_vote_request(msg); break;
                case Message::kRequestVoteResponse:
                    node.handle_request_vote_response(msg); break;
                case Message::kAppendEntriesRequest:
                    node.handle_append_entries_request(msg); break;
                case Message::kAppendEntriesResponse:
                    node.handle_append_entries_response(msg); break;
                case Message::kSnapshotRequest:
                    node.handle_install_snapshot_request(msg); break;
                case Message::kSnapshotResponse:
                    node.handle_install_snapshot_response(msg); break;
                case Message::kClientRequest:
                    node.handle_client_request(msg); break;
                default: break;
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_parse_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::RaftNode::handle_reissue_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* re_data = static_cast<ReissueData*>(w->data);
    auto& node = re_data->node;

    if (revents & EV_TIMEOUT) {
        if (node.role() != detail::RaftStatus::Role::Leader) {
            return;
        }
        // 每30ms为一个节点补发最多16条日志
        for (auto& next_index : node.st_.next_index_) {
            if (next_index.second <= node.last_log_index()) {
                // 准备请求的基础部分
                Message request;
                request.set_cluster_id(node.cluster_id());
                request.set_node_id(node.node_id());
                auto append_request = request.mutable_append_entries_request();
                append_request->set_term(node.current_term());
                append_request->set_is_heartbeat(false);
                append_request->set_leader_commit(node.commit_index());

                // 批量发送日志条目
                std::vector<EntryMeta> entries;
                for (auto i = next_index.second; i <= node.last_log_index(); ++i) {
                    auto entry = node.log_.entry_at(i);
                    if (entry.has_value()) {
                        entries.push_back(entry.value());
                    }
                }

                if (!entries.empty()) {
                    auto prev_index = next_index.second - 1;
                    auto prev_entry = node.log_.entry_at(prev_index);
                    append_request->set_prev_log_index(prev_index);
                    append_request->set_prev_log_term(prev_entry ? prev_entry->term() : 0);

                    for (const auto& entry : entries) {
                        auto* new_entry = append_request->add_entries();
                        *new_entry = entry;
                    }

                    node.send_to_pipeline(next_index.first, request);
                }
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_reissue_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::RaftNode::handle_request_vote_request(Message& msg) {
    auto request_vote_request = msg.mutable_request_vote_request();
    auto cluster_id = msg.cluster_id();
    auto node_id = msg.node_id();
    auto term = request_vote_request->term();
    auto last_log_index = request_vote_request->last_log_index();
    auto last_log_term = request_vote_request->last_log_term();

    if (cluster_id != this->cluster_id() || term < current_term()) {
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

    // 构造并发送投票回复
    Message response;
    response.set_cluster_id(this->cluster_id());
    response.set_node_id(this->node_id());
    auto request_vote_response = response.mutable_request_vote_response();
    request_vote_response->set_term(term);
    request_vote_response->set_granted(vote_granted);
    transport_->to_pipeline_deser_queue().enqueue(std::move(response), Priority::Critical);
}

void embkv::raft::RaftNode::handle_request_vote_response(Message& msg) {
    auto request_vote_response = msg.mutable_request_vote_response();
    auto cluster_id = msg.cluster_id();
    auto node_id = msg.node_id();
    auto term = request_vote_response->term();
    auto granted = request_vote_response->granted();

    if (cluster_id != this->cluster_id() || term < current_term()) {
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
            auto& peers = transport_->session_manager().peers();
            for (auto& peer : peers) {
                st_.match_index_[peer.first] = commit_index();
                st_.next_index_[peer.first] = commit_index()+1;
            }
            log::console().info("Become leader");
        }
    }

}

void embkv::raft::RaftNode::handle_append_entries_request(Message& msg) {
    auto append_entries_request = msg.mutable_append_entries_request();
    auto cluster_id = msg.cluster_id();
    auto node_id = msg.node_id();
    auto term = append_entries_request->term();
    auto prev_log_index = append_entries_request->prev_log_index();
    auto prev_log_term = append_entries_request->prev_log_term();
    auto& entries = append_entries_request->entries();
    auto leader_commit = append_entries_request->leader_commit();
    auto is_heartbeat = append_entries_request->is_heartbeat();

    if (cluster_id != this->cluster_id() || term < current_term()) {
        return;
    }

    if (term > current_term()) {
        st_.handle_higher_term(term);
    }

    if (leader_commit > commit_index()) {
        apply_to_state_machine(leader_commit);
    }

    if (is_heartbeat) {
        st_.last_heartbeat_ = util::current_ms();
        st_.leader_id_ = node_id;
        return;
    }

    Message response;
    response.set_cluster_id(this->cluster_id());
    response.set_node_id(this->node_id());
    auto append_entries_response = response.mutable_append_entries_response();
    append_entries_response->set_term(term);


    // 检查前一条日志是否存在
    if (prev_log_index != 0) {
        auto prev_entry = log_.entry_at(prev_log_index);
        if (!prev_entry.has_value() || prev_log_term != prev_entry.value().term()) {
            append_entries_response->set_success(false);
            append_entries_response->set_conflict_index(prev_log_index);
            append_entries_response->set_last_log_index(log_.last_log_index());
            transport_->to_pipeline_deser_queue().enqueue(std::move(response), Priority::High);
            return;
        }
    }

    for (auto& entry : entries) {
        log_.append_entry(std::move(entry));
    }

    // 回复
    append_entries_response->set_success(true);
    append_entries_response->set_last_log_index(log_.last_log_index());
    transport_->to_pipeline_deser_queue().enqueue(std::move(response), Priority::High);
}

void embkv::raft::RaftNode::handle_append_entries_response(Message& msg) {
    auto append_entries_response = msg.mutable_append_entries_response();
    auto cluster_id = msg.cluster_id();
    auto node_id = msg.node_id();
    auto term = append_entries_response->term();
    auto success = append_entries_response->success();
    auto conflict_index = append_entries_response->conflict_index();
    auto last_log_index = append_entries_response->last_log_index();

    if (cluster_id != this->cluster_id() || term < current_term()) {
        return;
    }

    if (term > current_term()) {
        st_.handle_higher_term(term);
        return;
    }

    if (!success) {
        // 处理日志冲突
        log::console().info("Append entries failed");
        st_.update_next_index(node_id, conflict_index); // 等待补发日志
        return;
    }

    // 更新对应的状态
    st_.update_next_index(node_id, last_log_index+1);
    st_.update_match_index(node_id, last_log_index);
}

void embkv::raft::RaftNode::handle_install_snapshot_request(Message& msg) {

}

void embkv::raft::RaftNode::handle_install_snapshot_response(Message& msg) {

}

void embkv::raft::RaftNode::handle_client_request(Message& msg) {
    auto node_id = msg.node_id();
    auto client_request = msg.mutable_client_request();
    auto request_id = client_request->request_id();

    // 非 Leader 处理
    if (role() != detail::RaftStatus::Role::Leader) {
        Message response;
        response.set_cluster_id(cluster_id());
        response.set_node_id(this->node_id());
        auto client_response = response.mutable_client_response();
        client_response->set_request_id(request_id);
        client_response->set_success(false);
        client_response->set_error("Not leader");
        client_response->set_leader_hint(leader_id());
        transport_->send_to_client(node_id, response);
        return;
    }

    // Leader 处理
    auto last_index = last_log_index();
    EntryMeta entry;
    entry.set_term(current_term());
    entry.set_index(last_index + 1);
    entry.set_client_request(client_request->SerializeAsString());

    // 追加日志
    log_.append_entry(entry);
    st_.update_next_index(node_id, last_log_index()+1);
    st_.update_match_index(node_id, last_log_index());
    requests_.emplace(last_log_index(), Request{node_id, request_id});

    // 构造 AppendEntries 请求
    Message request;
    request.set_cluster_id(this->cluster_id());
    request.set_node_id(this->node_id());
    auto append_request = request.mutable_append_entries_request();
    append_request->set_term(current_term());
    append_request->set_leader_commit(commit_index());
    append_request->set_is_heartbeat(false);
    append_request->set_prev_log_index(last_index);
    if (last_index == 0) {
        append_request->set_prev_log_term(0);
    } else {
        if (auto last_entry = log_.entry_at(last_index)) {
            append_request->set_prev_log_term(last_entry->term());
        } else {
            return;
        }
    }

    // 添加新条目
    auto* new_entry = append_request->add_entries();
    *new_entry = entry;

    // 广播给所有节点
    transport_->to_pipeline_deser_queue().enqueue(std::move(request), Priority::Medium);
}

void embkv::raft::RaftNode::event_loop() {
    uint64_t delay = util::FastRand::instance().rand_range(150, 300) / 1000.0;
    auto* el_data = new ElectionData{*this, delay};
    auto* hb_data = new HeartbeatData{*this};
    auto* pr_data = new ParseData{*this};
    auto* re_data = new ReissueData{*this};
    election_watcher_.data = el_data;
    heartbeat_watcher_.data = hb_data;
    parse_watcher_.data = pr_data;
    reissue_watcher_.data = re_data;
    ev_timer_init(&election_watcher_, handle_election_timeout, delay, 0);
    ev_timer_init(&heartbeat_watcher_, handle_heartbeat_timeout, 0, 0.05);
    ev_timer_init(&parse_watcher_, handle_parse_timeout, 0, 0.003);
    ev_timer_init(&reissue_watcher_, handle_reissue_timeout, 0, 0.03); // 每30ms补发一次日志
    ev_timer_start(loop_, &election_watcher_);
    ev_timer_start(loop_, &heartbeat_watcher_);
    ev_timer_start(loop_, &parse_watcher_);
    ev_timer_start(loop_, &reissue_watcher_);
    ev_run(loop_, 0);
    ev_timer_stop(loop_, &election_watcher_);
    ev_timer_stop(loop_, &heartbeat_watcher_);
    ev_timer_stop(loop_, &parse_watcher_);
    ev_timer_stop(loop_, &reissue_watcher_);
    delete el_data;
    delete hb_data;
    delete pr_data;
    delete re_data;
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
    append_entries_request->set_leader_commit(commit_index());
    transport_->to_pipeline_deser_queue().enqueue(std::move(msg), Priority::Critical);
}

void embkv::raft::RaftNode::reset_election_timer() {
    auto random_delay = []() {
        return util::FastRand::instance().rand_range(150, 300) / 1000.0;
    };
    ev_timer_set(&election_watcher_, random_delay(), 0.0);
    ev_timer_start(loop_, &election_watcher_);
}

void embkv::raft::RaftNode::send_to_pipeline(uint64_t id, Message& msg) {
    auto& sess_mgr = transport_->session_manager();
    auto peer = sess_mgr.peer_at(id);
    if (!peer) {
        log::console().error("Peer {} is not exist");
        return;
    }
    peer->pipeline()->from_ser_queue().enqueue(std::make_shared<std::string>(msg.SerializeAsString()));
}

void embkv::raft::RaftNode::apply_to_state_machine(uint64_t commit_index) {
    st_.commit_index_ = commit_index;
    while (last_applied() < commit_index) {
        st_.last_applied_++;
        auto entry = log_.entry_at(last_applied());
        if (entry.has_value()) {
            // 构造回复并发送给客户端
            auto& request = requests_[last_applied()];
            Message response;
            response.set_cluster_id(cluster_id());
            response.set_node_id(this->node_id());
            auto client_response = response.mutable_client_response();
            client_response->set_request_id(request.request_id);
            client_response->set_success(true);
            auto value = state_machine_.apply(std::move(entry.value()));
            if (value.has_value()) {
                client_response->set_value(std::move(value.value()));
            }
            transport_->send_to_client(request.client_id, response);
        }
    }
}
