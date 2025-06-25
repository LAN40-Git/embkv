#pragma once
#include <unistd.h>

namespace embkv::util
{
class FD {
public:
    explicit FD(int fd = -1)
        : fd_{fd} {}
    ~FD() {
        close();
    }

    FD(const FD &) = delete;
    FD &operator=(const FD &) = delete;
    FD(FD && other) noexcept {
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    auto operator=(FD && other) noexcept -> FD&;

public:
    auto fd() const noexcept { return fd_; }
    auto is_valid() const noexcept { return fd_ >= 0; }
    auto release() noexcept -> int;
    void close();

protected:
    int fd_;
};
}
