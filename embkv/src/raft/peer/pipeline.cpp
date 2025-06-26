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
    thread_local char read_buf_[10 * 1024]; // 10kb 缓冲区，过滤长度大于此缓冲的消息
    auto* rd_data = static_cast<ReadData*>(w->data);
    auto& stream = rd_data->pipeline.stream_;

    if (revents & EV_READ) {
        // 接收头部
        auto& buffer = HeadManager::buffer();
        stream.read_exact(buffer.data(), buffer.size());
        auto header = HeadManager::deserialize();
        if (!header.has_value()) {
            return;
        }
        // 接收消息体
        auto length = header.value().length;
        if (length > sizeof(read_buf_)) {
            log::console().error("Received too many bytes while reading.");
            return;
        }
        // TODO: 使用缓冲区池和智能指针优化
        stream.read_exact(read_buf_, length);
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("read_cb error : {}", strerror(errno));
    }
}

void embkv::raft::detail::Pipeline::handle_write_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    auto* wr_data = static_cast<WriteData*>(w->data);

}

void embkv::raft::detail::Pipeline::event_loop() noexcept {
    auto* rd_data = new ReadData(*this);
    auto* wr_data = new WriteData(*this);
    read_watcher_.data = rd_data;
    write_watcher_.data = wr_data;
    ev_io_init(&read_watcher_, read_cb, stream_.fd(), EV_READ);
    ev_timer_init(&write_watcher_, handle_write_timeout, 0, 0.001);
    ev_io_start(loop_, &read_watcher_);
    ev_timer_start(loop_, &write_watcher_);
    ev_run(loop_, 0);
    delete rd_data;
    delete wr_data;
}
