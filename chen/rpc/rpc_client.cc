#include "rpc_client.h"

#include <vector>

#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen::rpc {

static Logger::ptr logger = LOG_NAME("system");

RpcClient::RpcClient(bool auto_heartbeat) : m_auto_heartbeat(auto_heartbeat) {}

RpcClient::~RpcClient() { close(); }

bool RpcClient::connect(Address::ptr address, uint64_t timeout_ms) {
    m_sock = Socket::CreateTCP(address);
    if (!m_sock) {
        return false;
    }
    if (!m_sock->connect(address, timeout_ms)) {
        return false;
    }
    m_session.reset(new RpcConnection(m_sock));

    // 启动接收协程
    IOManager::GetThis()->schedule(
        std::bind(&RpcClient::recvLoop, std::weak_ptr<RpcClient>(shared_from_this()), m_session));

    // 启动心跳
    m_hbMissed = 0;
    if (m_auto_heartbeat) {
        std::lock_guard lock(m_mutex);
        if (!m_hb_timer) {
            std::weak_ptr<RpcClient> weak_this = shared_from_this();
            m_hb_timer = IOManager::GetThis()->addTimer(m_heartbeat_ms, [weak_this]() {
                auto self = weak_this.lock();
                if (!self) {
                    DEBUG(logger) << "heartbeat timer exit";
                    return;
                }
                RpcConnection::ptr session;
                bool should_close = false;
                {
                    std::lock_guard lock(self->m_mutex);
                    session = self->m_session;
                    if (++self->m_hbMissed > self->MAX_HB_MISSED) {
                        WARN(logger) << "heartbeat timeout (" << self->m_hbMissed
                            << " missed), closing connection";
                        should_close = true;
                    }
                }
                if (should_close) {
                    self->close();
                    return;
                }
                if (!session || !session->isConnected()) {
                    DEBUG(logger) << "heartbeat session is closed";
                    return;
                }
                Protocol::ptr hb(new Protocol);
                hb->type = MessageType::HEARTBEAT;
                hb->sequence = 0;
                hb->body = "";
                self->sendProtocol(hb);
            }, true);
        }
    }
    return true;
}

void RpcClient::close() {
    std::lock_guard lock(m_mutex);
    // 先取消 socket 上的 pending 事件（特别是 recvLoop 在等的事件）
    // 确保 recvLoop fiber 被唤醒，否则它会永远卡在 epoll 里
    if (m_sock) {
        m_sock->cancelAll();
        m_sock = nullptr;
    }
    if (m_session) {
        m_session->close();
        m_session = nullptr;
    }
    if (m_hb_timer) {
        m_hb_timer->cancel();
        m_hb_timer = nullptr;
    }
}

bool RpcClient::registerService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id
        ,const std::vector<uint32_t>& bind_ids) {
    try {
        call<void>("@register", peer_id, func_id, instance_id, bind_ids);
        return true;
    } catch (std::exception& e) {
        ERROR(logger) << "registerService failed, peer_id=" << peer_id
            << " func_id=" << func_id << " instance_id=" << instance_id
            << " err=" << e.what();
        return false;
    }
}

bool RpcClient::sendProtocol(Protocol::ptr pkt) {
    m_writeSem.wait();
    bool ok = m_session && m_session->sendProtocol(pkt) > 0;
    m_writeSem.notify();
    return ok;
}

Protocol::ptr RpcClient::callInternal(Protocol::ptr req, uint64_t timeout_ms) {
    ResponseContext::ptr ctx(new ResponseContext);
    ctx->fiber = Fiber::GetThis();
    ctx->scheduler = Scheduler::GetThis();

    {
        std::lock_guard lock(m_mutex);
        if (m_pending_requests.size() >= MAX_PENDING_REQUESTS) {
            throw std::runtime_error("too many pending requests");
        }
        m_pending_requests[req->sequence] = ctx;
    }

    Timer::ptr timer;
    if (timeout_ms > 0) {
        std::weak_ptr<RpcClient> weak_this = shared_from_this();
        uint32_t seq = req->sequence;
        timer = IOManager::GetThis()->addTimer(timeout_ms, [weak_this, seq]() {
            auto self = weak_this.lock();
            if (!self) {
                return;
            }
            auto ctx = self->takePending(seq);
            if (ctx) {
                self->completeRequest(ctx, RpcStatus::TIMEOUT, nullptr);
            }
        }, false);
    }

    if (!sendProtocol(req)) {
        if (timer) {
            timer->cancel();
        }
        // 当前协程尚未挂起，不能调用 completeRequest（会误调度自身），仅移除登记
        takePending(req->sequence);
        throw std::runtime_error("send request failed");
    }

    Fiber::YieldToHold();

    if (timer) {
        timer->cancel();
    }

    if (ctx->rsp) {
        return ctx->rsp;
    }

    throw std::runtime_error("request timeout");
}

void RpcClient::callInternalAsync(Protocol::ptr req, uint64_t timeout_ms, std::function<void(RpcStatus, Protocol::ptr)> callback) {
    ResponseContext::ptr ctx(new ResponseContext);
    ctx->scheduler = Scheduler::GetThis();
    if (!ctx->scheduler) {
        ctx->scheduler = IOManager::GetThis();
    }
    ctx->callback = std::move(callback);

    bool too_many = false;
    {
        std::lock_guard lock(m_mutex);
        if (m_pending_requests.size() >= MAX_PENDING_REQUESTS) {
            too_many = true;
        } else {
            m_pending_requests[req->sequence] = ctx;
        }
    }
    if (too_many) {
        completeRequest(ctx, RpcStatus::SEND_FAILED, nullptr);
        return;
    }

    Timer::ptr timer;
    if (timeout_ms > 0) {
        std::weak_ptr<RpcClient> weak_this = shared_from_this();
        uint32_t seq = req->sequence;
        timer = IOManager::GetThis()->addTimer(timeout_ms, [weak_this, seq]() {
            auto self = weak_this.lock();
            if (!self) {
                return;
            }
            auto ctx = self->takePending(seq);
            if (ctx) {
                self->completeRequest(ctx, RpcStatus::TIMEOUT, nullptr);
            }
        }, false);
        std::lock_guard lock(m_mutex);
        ctx->timer = timer;
    }

    if (!sendProtocol(req)) {
        if (timer) {
            timer->cancel();
        }
        auto pending = takePending(req->sequence);
        if (pending) {
            completeRequest(pending, RpcStatus::SEND_FAILED, nullptr);
        }
    }
}

RpcClient::ResponseContext::ptr RpcClient::takePending(uint32_t sequence) {
    std::lock_guard lock(m_mutex);
    auto it = m_pending_requests.find(sequence);
    if (it == m_pending_requests.end()) {
        return nullptr;
    }
    ResponseContext::ptr ctx = it->second;
    m_pending_requests.erase(it);
    return ctx;
}

void RpcClient::completeRequest(ResponseContext::ptr ctx, RpcStatus status, Protocol::ptr rsp) {
    // 取消超时定时器（由定时器自身触发时取消失败，无副作用）
    Timer::ptr timer;
    {
        std::lock_guard lock(m_mutex);
        timer = ctx->timer;
        ctx->timer.reset();
    }
    if (timer) {
        timer->cancel();
    }

    if (ctx->callback) {
        auto callback = ctx->callback;
        if (ctx->scheduler) {
            ctx->scheduler->schedule([callback, status, rsp]() mutable {
                callback(status, rsp);
            });
        } else {
            callback(status, rsp);
        }
        return;
    }

    // 同步路径：写回响应并唤醒等待协程
    ctx->rsp = rsp;
    if (ctx->scheduler && ctx->fiber) {
        ctx->scheduler->schedule(ctx->fiber);
    }
}

void RpcClient::recvLoop(std::weak_ptr<RpcClient> weak_this, RpcConnection::ptr session) {
    while (true) {
        if (!session || !session->isConnected()) {
            break;
        }
        Protocol::ptr resp = session->recvProtocol();
        if (!resp) {
            break;
        }
        // 心跳响应：复位丢失计数
        if (resp->type == MessageType::HEARTBEAT) {
            auto self = weak_this.lock();
            if (self) {
                std::lock_guard lock(self->m_mutex);
                self->m_hbMissed = 0;
            }
            continue;
        }

        // hub 转发来的请求：按 CmdID 或方法名分发到本地处理器；REQUEST 回包，NOTIFY 单向不回
        if (resp->type == MessageType::REQUEST || resp->type == MessageType::NOTIFY) {
            auto self = weak_this.lock();
            if (!self) {
                break;
            }
            bool need_rsp = resp->type == MessageType::REQUEST;
            std::string method;
            std::function<Protocol::ptr(ByteArray::ptr, uint32_t)> handler;
            HandlerKey key{resp->cmd};
            if (resp->cmd == 0) {
                method = Serializer::DecodeMethod(resp->body);
                key = HandlerKey{method};
            }
            {
                std::lock_guard lock(self->m_mutex);
                auto it = self->m_requestHandlers.find(key);
                if (it != self->m_requestHandlers.end()) {
                    handler = it->second;
                }
            }

            Protocol::ptr rsp;
            if (handler) {
                ByteArray::ptr body_ba(new ByteArray);
                body_ba->writeStringWithoutLength(resp->body);
                body_ba->setPosition(0);
                if (resp->cmd == 0) {
                    body_ba->readStringVint();  // skip method name
                }
                rsp = handler(body_ba, resp->sequence);
            } else if (need_rsp) {
                std::string err_msg = resp->cmd != 0
                    ? "Cmd not found: " + std::to_string(resp->cmd)
                    : "Method not found: " + method;
                ByteArray::ptr rba(new ByteArray);
                rba->writeFint32(404);
                rba->writeStringVint(err_msg);
                rba->setPosition(0);
                rsp.reset(new Protocol);
                rsp->type = MessageType::RESPONSE;
                rsp->sequence = resp->sequence;
                rsp->body = rba->toString();
            }
            if (need_rsp && rsp && session) {
                self->sendProtocol(rsp);
            }
            continue;
        }

        auto self = weak_this.lock();
        if (!self) {
            break;
        }

        ResponseContext::ptr ctx = self->takePending(resp->sequence);
        if (ctx) {
            self->completeRequest(ctx, RpcStatus::OK, resp);
        } else {
            DEBUG(logger) << "recvLoop: response for sequence " << resp->sequence
                << " dropped (already timed out)";
        }
    }

    auto self = weak_this.lock();
    if (self) {
        // 取消心跳定时器并取出所有待响应请求（必须释放锁后再完成，避免回调重入死锁）
        std::vector<ResponseContext::ptr> pending;
        {
            std::lock_guard lock(self->m_mutex);
            if (self->m_hb_timer) {
                self->m_hb_timer->cancel();
                self->m_hb_timer = nullptr;
            }
            for (auto& i : self->m_pending_requests) {
                pending.push_back(i.second);
            }
            self->m_pending_requests.clear();
        }
        for (auto& ctx : pending) {
            self->completeRequest(ctx, RpcStatus::CONNECTION_CLOSED, nullptr);
        }
        // 连接已断开，清理 socket/session 使后续 callInternal 立即失败而非超时
        self->close();
    }
}

} // namespace chen::rpc
