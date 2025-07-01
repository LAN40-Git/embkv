#include "raft/log.h"
#include <iostream>

auto embkv::raft::detail::RaftLog::entry_at(uint64_t index) -> boost::optional<EntryMeta> {
    if (index > entries_.size() + start_index_ || index == 0) {
        return boost::none;
    }
    return entries_.at(index-1);
}

void embkv::raft::detail::RaftLog::append_entry(const EntryMeta& entry) {
    entries_.emplace_back(entry);
    std::cout << "Append entry " << entries_.size() << "\n";
}