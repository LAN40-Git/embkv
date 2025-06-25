#include "common/util/fd.h"


auto embkv::util::FD::operator=(FD&& other) noexcept -> FD& {
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
}

auto embkv::util::FD::release() noexcept -> int {
    auto fd = fd_;
    fd_ = -1;
    return fd;
}

void embkv::util::FD::close() {
    if (is_valid()) {
        ::close(fd_);
    }
    fd_ = -1;
}
