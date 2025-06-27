#include "raft/node.h"

using namespace embkv::raft;

int main() {
    // 构造节点集群
    std::unordered_map<uint64_t, std::unique_ptr<Peer>> peers;
    peers.emplace(0, std::make_unique<Peer>(0, 0, "Node0", "127.0.0.0", 8080));
    peers.emplace(1, std::make_unique<Peer>(0, 1, "Node1", "127.0.0.0", 8081));
    peers.emplace(2, std::make_unique<Peer>(0, 2, "Node2", "127.0.0.0", 8081));
    auto transport = std::make_shared<Transport>(std::move(peers));
    transport->run();
    RaftNode node(transport);
    node.run();
    while (true) { std::this_thread::sleep_for(std::chrono::milliseconds(10000)); }
}