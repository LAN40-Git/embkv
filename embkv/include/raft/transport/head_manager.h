#pragma once
#include <bit>
#include <array>
#include <cstdint>
#include <cstring>
#include <boost/optional.hpp>
#include <boost/utility/string_view.hpp>
#include <netinet/in.h>

namespace embkv::raft::detail {
/**线程安全
 * 魔数默认使用网络字节序，无需手动转换
 * 序列化时需要将 length 转为网络字节序
 * 反序列化时 length 自动由网络字节序转为主机字节序
 */

constexpr uint32_t constexpr_htonl(uint32_t x) {
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x000000FF) << 24);
}

class HeadManager {
public:
    constexpr static uint32_t MAGIC = constexpr_htonl(0xCD54A23E); // 网络字节序
    constexpr static uint8_t VERSION = 1;
    using Buffer = std::array<uint8_t, 14>;
    enum class Flags : uint8_t {
        /* 节点类型标志 */
        kRaftNode = 0,
        kClientNode = 1,
        kAdminNode = 2,
        kDataNode = 3,
        /* 节点类型标志 */
        kNone = 4
    };

    // 头部结构 (14字节)
    #pragma pack(push, 1)
    struct Header {
        uint32_t magic{MAGIC}; // 网络字节序
        uint8_t version{VERSION};
        Flags flags{Flags::kNone};
        uint64_t length{0};
        bool is_valid() const noexcept {
            return magic == MAGIC && version == VERSION;
        }
    };
    #pragma pack(pop)
    static_assert(sizeof(Header) == 14, "Header size must be 14 bytes");

    // 获取线程局部缓冲区
    static auto buffer() noexcept -> Buffer& {
        thread_local Buffer buf;
        return buf;
    }

    // 序列化头部到缓冲区（需要将 length 转为网络字节序）
    static auto serialize(const Header& header) noexcept -> bool {
        if (!header.is_valid()) {
            return false;
        }

        std::memcpy(buffer().data(), &header, sizeof(header));
        return true;
    }

    // 从缓冲区反序列化头部（length 自动转为主机字节序）
    static auto deserialize() noexcept -> boost::optional<Header> {
        Header header;
        std::memcpy(&header, buffer().data(), sizeof(header));
        header.length = be64toh(header.length);
        return header.is_valid() ? boost::optional<Header>(header) : boost::optional<Header>();
    }

    // 重置头部缓冲区
    static void reset() noexcept {
        buffer().fill(0);
    }

    HeadManager() = delete;
    HeadManager(const HeadManager&) = delete;
    HeadManager& operator=(const HeadManager&) = delete;
};

} // namespace embkv::raft::detail