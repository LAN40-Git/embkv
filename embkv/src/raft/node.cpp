#include "raft/node.h"
#include "common/util/random.h"
#include "common/util/time.h"

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
}

void embkv::raft::RaftNode::handle_election_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto random_delay = []() {
        return static_cast<double>(util::FastRand::instance().rand_range(150, 300)) / 1000;
    };
    auto* el_data = static_cast<ElectionData*>(w->data);

    if (revents & EV_ERROR) {
        // TODO: 处理错误
    } else if (revents & EV_TIMEOUT) {
        auto now = util::current_ms();
        if (now - el_data->node.st_.last_heartbeat_ >= now - el_data->start) {
            // 只有当上次心跳与当前时间的间隔大于等于选举超时间隔时才进行选举
            el_data->node.start_election();
        }
    }

    ev_timer_set(w, random_delay(), 0.0);
    ev_timer_start(loop, w);
}

void embkv::raft::RaftNode::handle_heartbeat_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
}

auto embkv::raft::RaftNode::election_data() noexcept
-> std::unordered_set<ElectionData*> {
    thread_local std::unordered_set<ElectionData*> data;
    return data;
}

void embkv::raft::RaftNode::add_election_data(ElectionData* data) noexcept {
    election_data().emplace(data);
}

void embkv::raft::RaftNode::remove_election_data(ElectionData* data) noexcept {
    election_data().erase(data);
}

void embkv::raft::RaftNode::event_loop() {
    uint64_t delay = util::FastRand::instance().rand_range(150, 300) / 1000;
    auto* el_data = new ElectionData{*this, delay};
    election_watcher_.data = el_data;
    ev_timer_init(&election_watcher_, handle_election_timeout, delay, 0);
    ev_timer_init(&heartbeat_watcher_, handle_heartbeat_timeout, 0, 0.05);
    ev_timer_start(loop_, &election_watcher_);
    ev_timer_start(loop_, &heartbeat_watcher_);
    ev_run(loop_, 0);
}

void embkv::raft::RaftNode::start_election() {

}
