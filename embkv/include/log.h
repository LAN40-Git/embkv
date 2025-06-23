#pragma once
#include "log/logger.h"
#include "common/util/singleton.h"

namespace embkv::log
{
    auto& console = util::Singleton<ConsoleLogger>::instance();
}
