#include "common/util/fd.h"


auto embkv::util::Fd::operator=(Fd&& other) noexcept -> Fd& {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
}

auto embkv::util::Fd::release() noexcept -> int {
    auto fd = fd_;
    fd_ = -1;
    return fd;
}

void embkv::util::Fd::close() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
}
