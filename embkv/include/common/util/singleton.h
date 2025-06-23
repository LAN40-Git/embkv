#pragma once
#include <utility>

namespace embkv::util
{
template <typename T>
class Singleton {
public:
    Singleton() = delete;
    ~Singleton() = delete;
    
public:
    template <typename... Args>
    static auto instance(Args&&... args) -> T& {
        static T instance(std::forward<Args>(args)...);
        return instance;
    }
};
} // namespace embkv::util
