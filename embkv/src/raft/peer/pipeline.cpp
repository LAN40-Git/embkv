#include "raft/peer/pipeline.h"

void embkv::raft::detail::Pipeline::run(socket::net::TcpStream&& stream) noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    stream_ = std::move(stream);
}

void embkv::raft::detail::Pipeline::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(false, std::memory_order_release);
    stream_.close();
}

void embkv::raft::detail::Pipeline::read_cb(struct ev_loop* loop, struct ev_io* w, int revents) {

}

void embkv::raft::detail::Pipeline::write_cb(struct ev_loop* loop, struct ev_io* w, int revents) {

}
