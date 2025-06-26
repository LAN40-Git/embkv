#include "../../../include/raft/transport/session_manager.h"

auto embkv::raft::detail::SessionManager::peer_at(uint64_t id) -> Peer* {
    boost::shared_lock<boost::shared_mutex> lock(peers_mutex_);
    auto it = peers_.find(id);
    return it != peers_.end() ? it->second.get() : nullptr;
}

void embkv::raft::detail::SessionManager::add_peer(std::unique_ptr<Peer> peer) noexcept {
    boost::unique_lock<boost::shared_mutex> lock(peers_mutex_);
    peers_.emplace(peer->id(), std::move(peer));
}

void embkv::raft::detail::SessionManager::remove_peer(uint64_t peer_id) noexcept {
    boost::unique_lock<boost::shared_mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
}
