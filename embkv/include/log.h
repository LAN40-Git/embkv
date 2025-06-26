#pragma once
#include "log/logger.h"
#include "common/util/singleton.h"

namespace embkv::log
{
inline ConsoleLogger& console() {
    return util::Singleton<ConsoleLogger>::instance();
}
} // namespace embkv::log
