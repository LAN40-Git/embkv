#pragma once
#include "peer/peer.h"

namespace embkv::raft::detail
{
class ConnectionManager {
    using PeerMap = std::unordered_map<uint64_t, Peer>;
public:
    explicit ConnectionManager(PeerMap&& peers)
        : peers_(std::move(peers)) {}
    ~ConnectionManager();

private:
    // 连接映射
    /* Raft节点连接 */
    PeerMap peers_;
    /* 客户端连接映射 */
};
}
