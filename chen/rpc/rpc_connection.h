/**
 * @file rpc_connection.h
 * @brief RPC 连接层（协议帧收发）
 */
#pragma once

#include <memory>

#include "../socket/socket_stream.h"
#include "protocol.h"

namespace chen::rpc {

class RpcConnection : public SocketStream {
public:
    typedef std::shared_ptr<RpcConnection> ptr;

    RpcConnection(Socket::ptr sock, bool owner = true);
    Protocol::ptr recvProtocol();
    int sendProtocol(Protocol::ptr resp);
};

} // namespace chen::rpc
