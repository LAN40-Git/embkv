#pragma once
#include "../peer/peer.h"
#include <boost/thread/shared_mutex.hpp>

namespace embkv::raft::detail
{
class SessionManager {
public:
    using PeerMap = std::unordered_map<uint64_t, std::unique_ptr<Peer>>;
    explicit SessionManager(PeerMap&& peers)
        : peers_(std::move(peers)) {}
    ~SessionManager() = default;

public:
    // ====== peers ======
    auto peers() -> PeerMap& { return peers_; }
    auto peer_at(uint64_t id) -> Peer*;
    void add_peer(std::unique_ptr<Peer> peer) noexcept;
    void remove_peer(uint64_t peer_id) noexcept;
    auto peer_count() const noexcept -> std::size_t { return peers_.size(); }

    // ======clients ======

private:
    boost::shared_mutex peers_mutex_;
    boost::shared_mutex clients_mutex_;
    // 连接映射
    /* Raft节点连接 */
    PeerMap peers_;
    /* 客户端连接映射 */

};
} // namespace embkv::raft::detail
