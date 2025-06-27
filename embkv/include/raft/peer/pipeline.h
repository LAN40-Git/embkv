#pragma once
#include "log.h"
#include "config.h"
#include "proto/rpc.pb.h"
#include "common/util/fd.h"
#include "common/util/nocopyable.h"
#include "common/util/priorityqueue.h"
#include "raft/transport/head_manager.h"
#include "socket/net/stream.h"
#include <ev.h>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

namespace embkv::raft::detail
{
class Pipeline : util::Nocopyable {
    struct ReadData {
        Pipeline &pipeline;
        explicit ReadData(Pipeline &p) : pipeline(p) {}
    };
    struct WriteData {
        Pipeline &pipeline;
        explicit WriteData(Pipeline &p) : pipeline(p) {}
    };
public:
    using SerQueue = moodycamel::ConcurrentQueue<std::shared_ptr<std::string>>;
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    using FreeQueue = moodycamel::ConcurrentQueue<std::unique_ptr<Message>>;
    using Priority = util::detail::Priority;
    Pipeline() = default;
    ~Pipeline() noexcept {
        stop();
        ev_loop_destroy(loop_);
    }
    Pipeline(Pipeline &&) = delete;
    Pipeline &operator=(Pipeline &&) = delete;

public:
    void run(socket::net::TcpStream&& stream) noexcept;
    void stop() noexcept;

public:
    auto is_running() const noexcept -> bool {
        return is_running_.load(std::memory_order_relaxed);
    }
    auto try_get_connect_mutex() noexcept -> bool {
        return connect_mutex_.try_lock();
    }
    void release_connect_mutex() noexcept {
        connect_mutex_.unlock();
    }

public:
    auto from_ser_queue() -> SerQueue& { return from_ser_queue_; }
    auto to_deser_queue() -> DeserQueue& { return to_deser_queue_; }

private:
    static void read_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void handle_write_timeout(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
    void event_loop() noexcept;

private:
    socket::net::TcpStream   stream_{socket::detail::Socket{-1}};
    std::atomic<bool>        is_running_{false};
    std::mutex               run_mutex_{};
    std::mutex               connect_mutex_{};
    SerQueue                 from_ser_queue_;
    DeserQueue               to_deser_queue_;
    FreeQueue                free_deser_queue_;
    ev_io                    read_watcher_{0};
    ev_timer                 write_watcher_{0};
    struct ev_loop*          loop_{nullptr};
    boost::asio::thread_pool pool_{1};
};
} // namespace embkv::raft::detail