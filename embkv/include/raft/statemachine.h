#pragma once
#include "store.h"
#include "proto/rpc.pb.h"

namespace embkv::raft
{
class StateMachine {
public:
    auto apply(EntryMeta entry) -> boost::optional<std::string> {
        auto client_request = entry.client_request();
        ClientRequest request;
        request.ParseFromString(client_request);
        auto command = request.command();
        KVOperation op;
        op.ParseFromString(command);
        auto op_type = op.op_type();
        switch (op_type) {
            case KVOperation_OperationType_PUT: {
                store_.put(op.key(), op.value());
                return boost::none;
            }
            case KVOperation_OperationType_GET: {
                return store_.get(op.key());
            }
            case KVOperation_OperationType_DELETE: {
                store_.del(op.key());
                return boost::none;
            }
            default: std::cerr << "Unknown operation type: " << op_type << std::endl; break;
        }
        return boost::none;
    }
private:
    RaftStore store_;
};
} // namespace embkv::raft
