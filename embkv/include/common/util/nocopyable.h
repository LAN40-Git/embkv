#pragma once

namespace embkv::util
{
class Nocopyable {
public:
    Nocopyable(const Nocopyable &) = delete;
    Nocopyable &operator=(const Nocopyable &) = delete;

protected:
    Nocopyable() = default;
    ~Nocopyable() noexcept = default;
};
} // namespace embkv::util
