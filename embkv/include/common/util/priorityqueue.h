#pragma once
#include "thirdparty/concurrentqueue.h"

namespace embkv::util
{
template <typename T>
class PriorityQueue {
public:
    enum class Priority : uint8_t {
        Critical = 0, // 心跳、选举消息
        High = 1,     // 日志、快照消息
        Medium = 2,   // 客户端消息
        Low = 3,      // 后台管理消息
        Count = 4,
    };
    constexpr static uint8_t MAX_PRIORITY = static_cast<uint8_t>(Priority::Count);
    using Queue = moodycamel::ConcurrentQueue<T>;
    std::array<Queue, MAX_PRIORITY> queues_;

public:
    // 单次入队（指定优先级）
    auto enqueue(T&& item, Priority prio = Priority::Medium) -> bool {
        moodycamel::ConcurrentQueue<T> queue;
        return queues_[static_cast<size_t>(prio)].enqueue(std::forward<T>(item));
    }

    // 批量入队（指定优先级）
    template <typename It>
    auto enqueue_bulk(It begin, std::size_t count, Priority prio = Priority::Medium) -> bool {
        return queues_[static_cast<size_t>(prio)].enqueue_bulk(begin, count);
    }

    // 批量出队（按优先级轮询，若高优先级消息过多可能导致低优先级消息无法被处理）
    template <typename It>
    auto try_dequeue_bulk(It begin, std::size_t max) -> std::size_t {
        std::size_t count = 0;
        for (std::size_t prio = 0; prio < MAX_PRIORITY; prio++) {
            if (count >= max) {
                break;
            }
            count += queues_[prio].try_dequeue_bulk(begin + count, max - count);
        }
        return count;
    }

    // 获取队列大小（指定优先级）
    auto size_approx(Priority prio = Priority::Medium) const -> std::size_t {
        return queues_[static_cast<size_t>(prio)].size_approx();
    }

    // 队列判空（指定优先级）
    auto empty(Priority prio = Priority::Medium) const -> bool {
        return queues_[static_cast<size_t>(prio)].size_approx() == 0;
    }
};
} // namespace embkv::util
