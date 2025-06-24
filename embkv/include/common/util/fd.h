#pragma once
#include <unistd.h>

namespace embkv::util
{
class Fd {
public:
    explicit Fd(int fd = -1)
        : fd_{fd} {}

    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd && other) noexcept {
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    auto operator=(Fd && other) noexcept -> Fd&;

public:
    auto fd() const noexcept { return fd_; }
    auto is_valid() const noexcept { return fd_ >= 0; }
    auto release() noexcept -> int;
    void close();

protected:
    int fd_;
};
}
