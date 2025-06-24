#pragma once
#include "peer/peer.h"

namespace embkv::raft
{
class Transport {
public:
    using DeserQueue = util::PriorityQueue<Message>;

private:
    
};
} // embkv::raft