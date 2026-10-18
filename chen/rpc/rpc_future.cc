#include "rpc_future.h"

namespace chen::rpc {

const char* StatusToString(RpcStatus status)
{
    switch (status) {
    case RpcStatus::OK:
        return "ok";
    case RpcStatus::TIMEOUT:
        return "request timeout";
    case RpcStatus::SEND_FAILED:
        return "send request failed";
    case RpcStatus::CONNECTION_CLOSED:
        return "connection closed";
    case RpcStatus::DECODE_ERROR:
        return "decode response failed";
    }
    return "unknown error";
}

} // namespace chen::rpc
