#pragma once
#include "raft/peer/pipeline.h"
#include <cstdint>
#include <string>

namespace embkv::raft
{
class Peer {
public:
    explicit Peer(uint64_t id, std::string name,
                 std::string ip, uint16_t port) noexcept
        : id_(id)
        , name_(std::move(name))
        , ip_(std::move(ip))
        , port_(port)
        , pipeline_(std::make_unique<detail::Pipeline>()) {}
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&& other) noexcept = default;
    Peer& operator=(Peer&&) noexcept = default;

public:
    auto id() const noexcept -> uint64_t { return id_; }
    auto name() const noexcept -> const std::string& { return name_; }
    auto ip() const noexcept -> const std::string& { return ip_; }
    auto port() const noexcept -> uint16_t { return port_; }
    auto pipeline() noexcept -> detail::Pipeline* { return pipeline_.get(); }

private:
    uint64_t                          id_;
    std::string                       name_;
    std::string                       ip_;
    uint16_t                          port_;
    std::unique_ptr<detail::Pipeline> pipeline_;
};
}
