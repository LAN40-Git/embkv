#pragma once

namespace embkv::util
{
class Noncopyable {
public:
    Noncopyable(const Noncopyable &) = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;

protected:
    Noncopyable() = default;
    ~Noncopyable() noexcept = default;
};
} // namespace embkv::util
