#pragma once

namespace embkv::socket::detail
{
template <typename Stream>
struct ImplStreamWrite {
    auto write(const void* buf, size_t len) {
        return ::write(static_cast<const Stream*>(this)->fd(), buf, len);
    }

    auto write_exact(const void* buf, size_t len) -> size_t {
        size_t total_write = 0;
        while (total_write < len) {
            auto write_bytes = write(static_cast<const char*>(buf) + total_write, len - total_write);
            if (write_bytes <= 0) {
                break;
            }
            total_write += write_bytes;
        }
        return total_write;
    }
};
}