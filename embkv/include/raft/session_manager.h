#pragma once
#include "peer/peer.h"

namespace embkv::raft::detail
{
class SessionManager {
public:
    using PeerMap = std::unordered_map<uint64_t, Peer>;
    explicit SessionManager(PeerMap&& peers)
        : peers_(std::move(peers)) {}
    ~SessionManager();

private:
    // 连接映射
    /* Raft节点连接 */
    PeerMap peers_;
    /* 客户端连接映射 */

};
}
