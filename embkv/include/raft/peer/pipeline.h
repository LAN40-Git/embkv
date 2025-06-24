#pragma once
#include "proto/rpc.pb.h"
#include "common/util/fd.h"
#include "common/util/priorityqueue.h"

namespace embkv::raft
{
class Pipeline {
public:
    using DeserQueue = util::PriorityQueue<std::unique_ptr<Message>>;
    Pipeline() = default;
    Pipeline(const Pipeline &) = delete;
    Pipeline(Pipeline &&) = delete;

public:
    void run(int fd) noexcept;
    void stop() noexcept;
    auto is_running() const noexcept -> bool {
        return is_running_;
    }

private:


private:
    util::Fd          fd_;
    std::atomic<bool> is_running_{false};
    std::mutex        run_mutex_{};
    bool              is_connecting_{false};
    std::mutex        connect_mutex_{};
    DeserQueue        rx_deser_queue_;

};
} // namespace embkv::raft