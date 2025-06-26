#pragma once
#include "boost/asio/thread_pool.hpp"
#include <boost/asio/post.hpp>
#include "proto/rpc.pb.h"
#include "common/util/fd.h"
#include "common/util/priorityqueue.h"
#include "socket/net/stream.h"
#include <ev.h>

namespace embkv::raft::detail
{
class Pipeline {
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    Pipeline() = default;
    Pipeline(const Pipeline &) = delete;
    Pipeline(Pipeline &&) = delete;

public:
    void run(socket::net::TcpStream&& stream) noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool {
        return is_running_;
    }

private:
    static void read_cb(struct ev_loop* loop, struct ev_io* w, int revents);
    static void write_cb(struct ev_loop* loop, struct ev_io* w, int revents);

private:
    socket::net::TcpStream stream_{socket::detail::Socket{-1}};
    std::atomic<bool>      is_running_{false};
    std::mutex             run_mutex_{};
    bool                   is_connecting_{false};
    std::mutex             connect_mutex_{};
    DeserQueue             rx_deser_queue_;
    ev_io                  read_watcher_{0};
    ev_io                  write_watcher_{0};
};
} // namespace embkv::raft::detail