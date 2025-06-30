#pragma once
#include "raft/peer/peer.h"
#include <boost/thread/shared_mutex.hpp>

namespace embkv::raft::detail
{
class SessionManager {
public:
    struct ClientSession {
        uint64_t id;
        socket::net::TcpStream stream;
        explicit ClientSession(uint64_t id, socket::net::TcpStream&& stream)
            : id(id), stream(std::move(stream)) {}
    };
    using PeerMap = std::unordered_map<uint64_t, std::unique_ptr<Peer>>;
    using ClientMap = std::unordered_map<uint64_t, std::unique_ptr<ClientSession>>;
    explicit SessionManager(PeerMap&& peers)
        : peers_(std::move(peers)) {}
    ~SessionManager() = default;

public:
    // ====== peers ======
    auto peers() noexcept -> PeerMap& { return peers_; }
    auto peer_at(uint64_t id) -> Peer*;
    void add_peer(std::unique_ptr<Peer> peer) noexcept;
    void remove_peer(uint64_t peer_id) noexcept;
    auto peer_count() const noexcept -> std::size_t { return peers_.size(); }
    void peer_clear() noexcept { peers_.clear(); }

    // ======clients ======
    auto clients() noexcept -> ClientMap& { return clients_; }
    auto client_at(uint64_t id) -> ClientSession*;
    void add_client(std::unique_ptr<ClientSession> client) noexcept;
    void remove_client(uint64_t client_id) noexcept;
    auto client_count() const noexcept -> std::size_t { return clients_.size(); }
    void client_clear() noexcept { clients_.clear(); }

private:
    boost::shared_mutex peers_mutex_;
    boost::shared_mutex clients_mutex_;
    // 连接映射
    /* Raft节点连接 */
    PeerMap peers_;
    /* 客户端连接映射 */
    ClientMap clients_;
};
} // namespace embkv::raft::detail
