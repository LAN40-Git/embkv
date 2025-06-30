#include "raft/log.h"

auto embkv::raft::detail::RaftLog::entry_at(uint64_t index) -> boost::optional<EntryMeta> {
    if (index > entries_.size() + start_index_) {
        return boost::none;
    }
    return entries_.at(index-1);
}

void embkv::raft::detail::RaftLog::append_entry(const EntryMeta&& entry) {
    entries_.emplace_back(std::move(entry));
}
