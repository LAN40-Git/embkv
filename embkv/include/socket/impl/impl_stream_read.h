#pragma once

namespace embkv::socket::detail
{
template <typename Stream>
struct ImplStreamRead {
    auto read(void* buf, size_t len) {
        return ::read(static_cast<Stream*>(this)->fd(), buf, len);
    }

    auto read_exact(void* buf, size_t len) -> size_t {
        size_t total_read = 0;
        while (total_read < len) {
            auto read_bytes = read(buf + total_read, len - total_read);
            if (read_bytes <= 0) {
                break;
            }
            total_read += read_bytes;
        }
        return total_read;
    }
};
} // namespace embkv::socket::detail
