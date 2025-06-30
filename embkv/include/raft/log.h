#pragma once
#include "proto/rpc.pb.h"
#include <vector>
#include <boost/optional/optional.hpp>

namespace embkv::raft::detail
{
class RaftLog {
public:
    explicit RaftLog(uint64_t start_index = 0)
        : start_index_(start_index) {}
    ~RaftLog() = default;

public:
    auto last_log_index() const noexcept -> uint64_t { return last_log_index_; }
    auto last_log_term() const noexcept -> uint64_t { return last_log_term_; }

public:
    auto entry_at(uint64_t index) -> boost::optional<EntryMeta>;
    void append_entry(const EntryMeta&& entry);

private:
    std::vector<EntryMeta> entries_;
    uint64_t start_index_{0};
    uint64_t last_log_index_{0};
    uint64_t last_log_term_{0};
};
} // namespace embkv::raft::detail
