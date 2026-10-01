#include "rpc_server.h"

#include "../log/log.h"
#include "rpc_connection.h"

namespace chen::rpc {

static Logger::ptr logger = LOG_NAME("system");

RpcServer::RpcServer(IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
        : TcpServer(worker, io_worker, accept_worker) {
    m_type = "rpc_server";
}

void RpcServer::prepareDispatch() {
    // 保存旧 handler 到 pending，清空 active 供模块注册新 handler
    m_pendingHandlers.swap(m_handlers);
    // m_handlers 现在为空，模块通过 registerMethod 注册到这里
}

void RpcServer::commitDispatch() {
    // 新 handler 已在 m_handlers 中
    // 释放旧 handler（最后一个 in-flight fiber 完成后通过引用计数自然销毁）
    m_pendingHandlers.clear();
}

void RpcServer::sendCode(RpcConnection::ptr conn, uint32_t sequence, int32_t code, const std::string& msg) {
    ByteArray::ptr ba(new ByteArray);
    ba->writeFint32(code);
    if (code != 0) {
        ba->writeStringVint(msg);
    }
    ba->setPosition(0);

    Protocol::ptr out(new Protocol);
    out->type = MessageType::RESPONSE;
    out->sequence = sequence;
    out->body = ba->toString();
    relaySend(conn, out);
}

std::shared_ptr<RpcServer::ConnRelayState> RpcServer::getRelayState(RpcConnection::ptr conn) {
    std::lock_guard lock(m_connRelayMutex);
    auto it = m_connRelay.find(conn);
    if (it != m_connRelay.end()) {
        return it->second;
    }
    auto state = std::make_shared<ConnRelayState>();
    state->write_sem = std::make_shared<FiberSemaphore>(1);
    m_connRelay[conn] = state;
    return state;
}

uint32_t RpcServer::nextOutSeq(RpcConnection::ptr conn) {
    auto state = getRelayState(conn);
    std::lock_guard lock(state->mtx);
    return ++state->out_seq;
}

bool RpcServer::relaySend(RpcConnection::ptr conn, Protocol::ptr pkt) {
    auto state = getRelayState(conn);
    state->write_sem->wait();
    bool ok = conn->sendProtocol(pkt) > 0;
    state->write_sem->notify();
    return ok;
}

void RpcServer::handleClient(Socket::ptr client) {
    if (m_relay) {
        handleRelayClient(client);
        return;
    }
    DEBUG(logger) << "handleClient " << *client;
    RpcConnection::ptr session(new RpcConnection(client));

    // 发送错误响应（直接编码到 body，无需 protobuf）
    auto sendError = [&](uint32_t sequence, int32_t code, const std::string& msg) {
        sendCode(session, sequence, code, msg);
    };

    do {
        errno = 0;
        Protocol::ptr req = session->recvProtocol();
        if (!req) {
            int err = errno;
            if (err == 0) {
                INFO(logger) << "peer closed rpc connection client:" << *client;
                break;
            }
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN || err == EWOULDBLOCK) {
                DEBUG(logger) << "recvProtocol would block, close session to avoid stuck fiber"
                    << ", errno=" << err
                    << " errstr=" << strerror(err)
                    << " client:" << *client;
                break;
            }
            TRACE(logger) << "recvProtocol error" << ", errno=" << err
                << " errstr=" << strerror(err) << " client:" << *client;
            break;
        }

        // 心跳请求：直接回包并继续
        if (req->type == MessageType::HEARTBEAT) {
            TRACE(logger) << "heartbeat request received" << " from client:" << *client;
            Protocol::ptr hb(new Protocol);
            hb->type = MessageType::HEARTBEAT;
            hb->sequence = req->sequence;
            hb->body = "";
            if (session->sendProtocol(hb) <= 0) {
                WARN(logger) << "send heartbeat response failed"
                    << ", errno=" << errno
                    << " errstr=" << strerror(errno)
                    << " client:" << *client;
                break;
            }
            continue;
        }

        // 按 CmdID 或方法名分派（cmd!=0 时 body 直接为参数，无方法名字符串）
        ByteArray::ptr body_ba(new ByteArray);
        body_ba->writeStringWithoutLength(req->body);
        body_ba->setPosition(0);

        HandlerKey key{req->cmd};
        std::string method;
        if (req->cmd == 0) {
            method = Serializer::DecodeMethod(req->body);
            key = HandlerKey{method};
        }
        auto it = m_handlers.find(key);
        if (it == m_handlers.end()) {
            if (req->cmd != 0) {
                WARN(logger) << "Cmd not found: " << req->cmd;
                sendError(req->sequence, 404, "Cmd not found: " + std::to_string(req->cmd));
            } else {
                WARN(logger) << "Method not found: " << method;
                sendError(req->sequence, 404, "Method not found: " + method);
            }
            continue;
        }
        if (req->cmd == 0) {
            body_ba->readStringVint();  // skip method name
        }
        Protocol::ptr rsp = it->second(body_ba, req->sequence);
        if (session->sendProtocol(rsp) <= 0) {
            WARN(logger) << "sendProtocol failed"
                << ", errno=" << errno
                << " errstr=" << strerror(errno)
                << " client:" << *client;
            break;
        }
    } while (true);
    session->close();
}

void RpcServer::handleRelayClient(Socket::ptr client) {
    DEBUG(logger) << "handleRelayClient " << *client;
    RpcConnection::ptr session(new RpcConnection(client));

    do {
        errno = 0;
        Protocol::ptr frame = session->recvProtocol();
        if (!frame) {
            int err = errno;
            if (err == 0) {
                INFO(logger) << "peer closed relay connection client:" << *client;
                break;
            }
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN || err == EWOULDBLOCK) {
                DEBUG(logger) << "recvProtocol would block, close relay session"
                    << ", errno=" << err
                    << " errstr=" << strerror(err)
                    << " client:" << *client;
                break;
            }
            TRACE(logger) << "recvProtocol error" << ", errno=" << err
                << " errstr=" << strerror(err) << " client:" << *client;
            break;
        }

        switch (frame->type) {
        case MessageType::HEARTBEAT: {
            Protocol::ptr hb(new Protocol);
            hb->type = MessageType::HEARTBEAT;
            hb->sequence = frame->sequence;
            if (!relaySend(session, hb)) {
                break;
            }
            continue;
        }
        case MessageType::RESPONSE: {
            // 目标服务返回的中继响应：还原调用方 seq 后直接发回
            handleRelayResponse(frame, session);
            continue;
        }
        case MessageType::REQUEST:
        default: {
            // 路由请求优先：非阻塞转发，读循环继续收包（同一连接可多 in-flight）
            if (frame->routing != static_cast<uint8_t>(RoutingMethod::NONE)) {
                forward(frame, session);
                continue;
            }
            // 按 CmdID 或方法名分派（cmd!=0 时 body 直接为参数）
            ByteArray::ptr body_ba(new ByteArray);
            body_ba->writeStringWithoutLength(frame->body);
            body_ba->setPosition(0);

            HandlerKey key{frame->cmd};
            std::string method;
            if (frame->cmd == 0) {
                method = Serializer::DecodeMethod(frame->body);
                if (method == "@register") {
                    handleRegister(frame, session);
                    continue;
                }
                key = HandlerKey{method};
            }
            auto it = m_handlers.find(key);
            if (it == m_handlers.end()) {
                if (frame->cmd != 0) {
                    WARN(logger) << "Cmd not found: " << frame->cmd;
                    sendCode(session, frame->sequence, 404, "Cmd not found: " + std::to_string(frame->cmd));
                } else {
                    WARN(logger) << "Method not found: " << method;
                    sendCode(session, frame->sequence, 404, "Method not found: " + method);
                }
                continue;
            }
            if (frame->cmd == 0) {
                body_ba->readStringVint();  // skip method name
            }
            Protocol::ptr rsp = it->second(body_ba, frame->sequence);
            if (!relaySend(session, rsp)) {
                break;
            }
            continue;
        }
        }
    } while (true);

    // 连接断开：注销注册 + 失败挂在该连接上的未完成中继 + 清理连接 relay 状态
    if (m_registry) {
        m_registry->unregisterByConn(session);
    }
    failPendingRelays(session);
    {
        std::lock_guard lock(m_connRelayMutex);
        m_connRelay.erase(session);
    }
    session->close();
}

void RpcServer::handleRegister(Protocol::ptr frame, RpcConnection::ptr conn) {
    // body: [method_vint][peer_id][func_id][instance_id][bind_count][bind_ids...]
    ByteArray::ptr ba(new ByteArray);
    ba->writeStringWithoutLength(frame->body);
    ba->setPosition(0);
    ba->readStringVint();  // skip method

    uint32_t peer_id = ba->readFuint32();
    uint32_t func_id = ba->readFuint32();
    uint32_t instance_id = ba->readFuint32();
    uint32_t count = ba->readUint32();
    std::vector<uint32_t> bind_ids;
    bind_ids.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        bind_ids.push_back(ba->readFuint32());
    }

    bool ok = m_registry->registerService(peer_id, func_id, instance_id, bind_ids, conn);
    if (!ok) {
        WARN(logger) << "register failed, peer_id duplicated, peer_id=" << peer_id;
    }
    sendCode(conn, frame->sequence, ok ? 0 : 500, ok ? "" : "register failed: peer_id duplicated");
}

RpcConnection::ptr RpcServer::resolveTarget(Protocol::ptr req, RoutingMethod rm) {
    switch (rm) {
    case RoutingMethod::DIRECT:
        return m_registry->getByPeerId(req->dst_peer_id);
    case RoutingMethod::GROUPID:
        return m_registry->getByGroupId(req->func_id, req->group_id, req->real_random != 0);
    case RoutingMethod::BIND_ID:
        return m_registry->getByBindId(req->func_id, req->bind_id);
    case RoutingMethod::BROADCAST:
    case RoutingMethod::NONE:
    default:
        return nullptr;
    }
}

Protocol::ptr RpcServer::buildForwardFrame(Protocol::ptr req, RpcConnection::ptr caller) {
    Protocol::ptr out(new Protocol(*req));
    out->routing = static_cast<uint8_t>(RoutingMethod::NONE);
    out->real_random = 0;
    out->src_peer_id = m_registry->getPeerIdByConn(caller);
    out->dst_peer_id = 0;
    out->func_id = 0;
    out->group_id = 0;
    out->bind_id = 0;
    out->region = 0;
    return out;
}

void RpcServer::forward(Protocol::ptr req, RpcConnection::ptr caller) {
    if (!m_registry->isRegistered(caller)) {
        WARN(logger) << "forward from unregistered connection";
        sendCode(caller, req->sequence, 403, "unregistered caller");
        return;
    }

    RoutingMethod rm = static_cast<RoutingMethod>(req->routing);
    bool is_notify = req->type == MessageType::NOTIFY;

    // 广播：REQUEST 先回 code=0，再异步 fan-out（调用方不等 fan-out 完成）
    if (rm == RoutingMethod::BROADCAST) {
        auto targets = m_registry->getByFuncId(req->func_id);
        if (targets.empty()) {
            if (!is_notify) {
                sendCode(caller, req->sequence, 404, "no broadcast target");
            }
            return;
        }
        if (!is_notify) {
            sendCode(caller, req->sequence, 0, "");
        }
        auto self = std::dynamic_pointer_cast<RpcServer>(shared_from_this());
        IOManager::GetThis()->schedule([self, req, caller, targets]() {
            for (auto& target : targets) {
                Protocol::ptr out = self->buildForwardFrame(req, caller);
                out->sequence = self->nextOutSeq(target);
                self->relaySend(target, out);
            }
        });
        return;
    }

    RpcConnection::ptr target = resolveTarget(req, rm);
    if (!target) {
        WARN(logger) << "no route target, routing=" << static_cast<int>(rm);
        if (!is_notify) {
            sendCode(caller, req->sequence, 404, "no route target");
        }
        return;
    }

    uint32_t out_seq = nextOutSeq(target);

    // 单向通知：直接发送，不登记中继、不等回包
    if (is_notify) {
        Protocol::ptr out = buildForwardFrame(req, caller);
        out->sequence = out_seq;
        relaySend(target, out);
        return;
    }

    // 普通请求：登记中继 ctx + 超时定时器后立即返回（非阻塞，读循环继续）
    RelayCtx::ptr ctx(new RelayCtx);
    ctx->caller_conn = caller;
    ctx->caller_seq = req->sequence;

    auto state = getRelayState(target);
    {
        std::lock_guard lock(state->mtx);
        state->ctxs[out_seq] = ctx;
    }

    if (m_relayTimeout > 0) {
        std::weak_ptr<RpcServer> weak_this = std::dynamic_pointer_cast<RpcServer>(shared_from_this());
        ctx->timer = IOManager::GetThis()->addTimer(m_relayTimeout, [weak_this, state, out_seq]() {
            auto self = weak_this.lock();
            if (!self) {
                return;
            }
            RelayCtx::ptr relay_ctx;
            {
                std::lock_guard lock(state->mtx);
                auto it = state->ctxs.find(out_seq);
                if (it == state->ctxs.end()) {
                    return;
                }
                relay_ctx = it->second;
                state->ctxs.erase(it);
            }
            self->sendCode(relay_ctx->caller_conn, relay_ctx->caller_seq, 504, "relay timeout");
        }, false);
    }

    Protocol::ptr out = buildForwardFrame(req, caller);
    out->sequence = out_seq;
    relaySend(target, out);
}

void RpcServer::handleRelayResponse(Protocol::ptr frame, RpcConnection::ptr conn) {
    RelayCtx::ptr ctx;
    std::shared_ptr<ConnRelayState> state;
    {
        std::lock_guard lock(m_connRelayMutex);
        auto it = m_connRelay.find(conn);
        if (it == m_connRelay.end()) {
            DEBUG(logger) << "relay response dropped, seq=" << frame->sequence;
            return;
        }
        state = it->second;
    }
    {
        std::lock_guard lock(state->mtx);
        auto it = state->ctxs.find(frame->sequence);
        if (it == state->ctxs.end()) {
            DEBUG(logger) << "relay response dropped, seq=" << frame->sequence;
            return;
        }
        ctx = it->second;
        state->ctxs.erase(it);
    }

    if (ctx->timer) {
        ctx->timer->cancel();
    }
    frame->sequence = ctx->caller_seq;
    relaySend(ctx->caller_conn, frame);
}

void RpcServer::failPendingRelays(RpcConnection::ptr conn) {
    std::vector<RelayCtx::ptr> to_fail;
    std::shared_ptr<ConnRelayState> state;
    {
        std::lock_guard lock(m_connRelayMutex);
        auto it = m_connRelay.find(conn);
        if (it == m_connRelay.end()) {
            return;
        }
        state = it->second;
    }
    {
        std::lock_guard lock(state->mtx);
        for (auto& [seq, ctx] : state->ctxs) {
            to_fail.push_back(ctx);
        }
        state->ctxs.clear();
    }
    for (auto& ctx : to_fail) {
        if (ctx->timer) {
            ctx->timer->cancel();
        }
        sendCode(ctx->caller_conn, ctx->caller_seq, 502, "target disconnected");
    }
}

} // namespace chen::rpc