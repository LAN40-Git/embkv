#include "raft/peer/pipeline.h"

void embkv::raft::detail::Pipeline::run(socket::net::TcpStream&& stream) noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    is_running_.store(true, std::memory_order_release);
    stream_ = std::move(stream);
    boost::asio::post(pool_, [this]() {
        event_loop();
    });
}

void embkv::raft::detail::Pipeline::stop() noexcept {
    std::lock_guard<std::mutex> lock(run_mutex_);
    if (is_running_.load(std::memory_order_acquire)) {
        return;
    }
    stream_.close();
    pool_.wait();
}

void embkv::raft::detail::Pipeline::read_cb(struct ev_loop* loop, struct ev_io* w, int revents) {
    thread_local char read_buf[10 * 1024]; // 10kb 缓冲区，过滤长度大于此缓冲的消息
    auto* rd_data = static_cast<ReadData*>(w->data);
    auto& stream = rd_data->pipeline.stream_;
    auto& to_deser_queue = rd_data->pipeline.to_deser_queue_;

    if (revents & EV_READ) {
        // 接收头部
        auto& buffer = HeadManager::buffer();
        auto size = stream.read_exact(buffer.data(), buffer.size());
        if (size < sizeof(HeadManager::Header)) {
            ev_break(loop, EVBREAK_ALL);
            return;
        }
        auto header = HeadManager::deserialize();
        if (header.has_value()) {
            // 接收消息体
            auto length = header.value().length;
            if (length > sizeof(read_buf)) {
                log::console().error("Received too many bytes while reading.");
                ev_break(loop, EVBREAK_ALL);
                return;
            }
            // TODO: 使用缓冲区池优化
            size = stream.read_exact(read_buf, length);

            Message msg;
            if (!msg.ParseFromArray(read_buf, size)) {
                log::console().error("Failed to parse message.");
            }

            switch (msg.content_case()) {
                case Message::kRequestVoteRequest:
                case Message::kRequestVoteResponse:
                    to_deser_queue.enqueue(std::move(msg), Priority::Critical);
                    break;
                case Message::kAppendEntriesRequest:
                case Message::kAppendEntriesResponse:
                case Message::kSnapshotRequest:
                case Message::kSnapshotResponse:
                    to_deser_queue.enqueue(std::move(msg), Priority::High);
                    break;
                case Message::kClientRequest:
                case Message::kClientResponse:
                    to_deser_queue.enqueue(std::move(msg), Priority::Medium);
                    break;
                default: break;
            }
        }

        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("read_cb error : {}", strerror(errno));
    }
}

void embkv::raft::detail::Pipeline::handle_write_timeout(struct ev_loop* loop, struct ev_timer* w, int revents) {
    thread_local std::array<std::shared_ptr<std::string>, 64> buf;
    auto* wr_data = static_cast<WriteData*>(w->data);
    auto& stream = wr_data->pipeline.stream_;
    auto& from_ser_queue = wr_data->pipeline.from_ser_queue_;

    if (revents & EV_TIMEOUT) {
        auto count = from_ser_queue.try_dequeue_bulk(buf.data(), buf.size());
        for (auto i = 0; i < count; ++i) {
            HeadManager::Header header{
                .length = htobe64(buf[i]->size())
            };
            if (HeadManager::serialize(header)) {
                auto& buffer = HeadManager::buffer();
                stream.write_exact(buffer.data(), buffer.size());
                stream.write_exact(buf[i]->data(), buf[i]->size());
            }
        }
        return;
    }

    if (revents & EV_ERROR) {
        log::console().error("handle_write_timeout error : {}", strerror(errno));
    }
}

void embkv::raft::detail::Pipeline::event_loop() noexcept {
    if (!stream_.is_valid()) {
        log::console().error("Pipeline stream is not valid.");
        return;
    }
    auto* rd_data = new ReadData(*this);
    auto* wr_data = new WriteData(*this);
    struct ev_loop* loop = ev_loop_new(EVFLAG_AUTO);
    ev_io read_watcher{};
    ev_timer write_watcher{};
    read_watcher.data = rd_data;
    write_watcher.data = wr_data;
    ev_io_init(&read_watcher, read_cb, stream_.fd(), EV_READ);
    ev_timer_init(&write_watcher, handle_write_timeout, 0, 0.003);
    ev_io_start(loop, &read_watcher);
    ev_timer_start(loop, &write_watcher);
    ev_run(loop, 0);
    ev_io_stop(loop, &read_watcher);
    ev_timer_stop(loop, &write_watcher);
    delete rd_data;
    delete wr_data;
    stream_.close();
    is_running_.store(false, std::memory_order_release);
}
