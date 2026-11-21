#include "RpcClient.hpp"

#include <vector>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"

namespace chen::rpc {

static Logger::ptr logger = LOG_NAME("system");

void RpcClient::unregisterMethod(const std::string& name)
{
    std::lock_guard lock(mutex_);
    request_handlers_.erase(HandlerKey{name});
}

void RpcClient::unregisterMethod(uint32_t cmd)
{
    std::lock_guard lock(mutex_);
    request_handlers_.erase(HandlerKey{cmd});
}

void RpcClient::applyRouting(Protocol::ptr req, const RpcRouting& routing)
{
    req->routing = static_cast<uint8_t>(routing.method);
    req->dst_peer_id = routing.dst_peer_id;
    req->func_id = routing.func_id;
    req->group_id = routing.group_id;
    req->bind_id = routing.bind_id;
    req->real_random = routing.real_random ? 1 : 0;
}

RpcClient::RpcClient(bool auto_heartbeat)
    : auto_heartbeat_(auto_heartbeat)
{
}

RpcClient::~RpcClient()
{
    close();
}

bool RpcClient::connect(Address::ptr address, uint64_t timeout_ms)
{
    sock_ = Socket::CreateTCP(address);
    if (!sock_) {
        return false;
    }
    if (!sock_->connect(address, timeout_ms)) {
        return false;
    }
    session_.reset(new RpcConnection(sock_));

    // 启动接收协程
    IOManager::GetThis()->schedule(
        std::bind(&RpcClient::recvLoop, std::weak_ptr<RpcClient>(shared_from_this()), session_));

    // 启动心跳
    hb_missed_ = 0;
    if (auto_heartbeat_) {
        std::lock_guard lock(mutex_);
        if (!hb_timer_) {
            std::weak_ptr<RpcClient> weak_this = shared_from_this();
            hb_timer_ = IOManager::GetThis()->addTimer(
                heartbeat_ms_,
                [weak_this]() {
                    auto self = weak_this.lock();
                    if (!self) {
                        DEBUG(logger) << "heartbeat timer exit";
                        return;
                    }
                    RpcConnection::ptr session;
                    bool should_close = false;
                    {
                        std::lock_guard lock(self->mutex_);
                        session = self->session_;
                        if (++self->hb_missed_ > self->kMaxHbMissed) {
                            WARN(logger) << "heartbeat timeout (" << self->hb_missed_ << " missed), closing connection";
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
                },
                true);
        }
    }
    return true;
}

void RpcClient::close()
{
    std::lock_guard lock(mutex_);
    // 先取消 socket 上的 pending 事件（特别是 recvLoop 在等的事件）
    // 确保 recvLoop fiber 被唤醒，否则它会永远卡在 epoll 里
    if (sock_) {
        if (sock_->isValid()) {
            sock_->cancelAll();
        }
        sock_ = nullptr;
    }
    if (session_) {
        session_->close();
        session_ = nullptr;
    }
    if (hb_timer_) {
        hb_timer_->cancel();
        hb_timer_ = nullptr;
    }
}

bool RpcClient::registerService(uint32_t peer_id, uint32_t func_id, uint32_t instance_id, const std::vector<uint32_t>& bind_ids)
{
    try {
        call<void>("@register", peer_id, func_id, instance_id, bind_ids);
        return true;
    } catch (std::exception& e) {
        ERROR(logger) << "registerService failed, peer_id=" << peer_id << " func_id=" << func_id << " instance_id=" << instance_id
                      << " err=" << e.what();
        return false;
    }
}

bool RpcClient::sendProtocol(Protocol::ptr pkt)
{
    write_sem_.wait();
    bool ok = session_ && session_->sendProtocol(pkt) > 0;
    write_sem_.notify();
    return ok;
}

Protocol::ptr RpcClient::callInternal(Protocol::ptr req, uint64_t timeout_ms)
{
    ResponseContext::ptr ctx(new ResponseContext);
    ctx->fiber = Fiber::GetThis();
    ctx->scheduler = Scheduler::GetThis();

    {
        std::lock_guard lock(mutex_);
        if (pending_requests_.size() >= kMaxPendingRequests) {
            throw std::runtime_error("too many pending requests");
        }
        pending_requests_[req->sequence] = ctx;
    }

    Timer::ptr timer;
    if (timeout_ms > 0) {
        std::weak_ptr<RpcClient> weak_this = shared_from_this();
        uint32_t seq = req->sequence;
        timer = IOManager::GetThis()->addTimer(
            timeout_ms,
            [weak_this, seq]() {
                auto self = weak_this.lock();
                if (!self) {
                    return;
                }
                auto ctx = self->takePending(seq);
                if (ctx) {
                    self->completeRequest(ctx, RpcStatus::TIMEOUT, nullptr);
                }
            },
            false);
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

void RpcClient::callInternalAsync(Protocol::ptr req, uint64_t timeout_ms, std::function<void(RpcStatus, Protocol::ptr)> callback)
{
    ResponseContext::ptr ctx(new ResponseContext);
    ctx->scheduler = Scheduler::GetThis();
    if (!ctx->scheduler) {
        ctx->scheduler = IOManager::GetThis();
    }
    ctx->callback = std::move(callback);

    bool too_many = false;
    {
        std::lock_guard lock(mutex_);
        if (pending_requests_.size() >= kMaxPendingRequests) {
            too_many = true;
        } else {
            pending_requests_[req->sequence] = ctx;
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
        timer = IOManager::GetThis()->addTimer(
            timeout_ms,
            [weak_this, seq]() {
                auto self = weak_this.lock();
                if (!self) {
                    return;
                }
                auto ctx = self->takePending(seq);
                if (ctx) {
                    self->completeRequest(ctx, RpcStatus::TIMEOUT, nullptr);
                }
            },
            false);
        std::lock_guard lock(mutex_);
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

RpcClient::ResponseContext::ptr RpcClient::takePending(uint32_t sequence)
{
    std::lock_guard lock(mutex_);
    auto it = pending_requests_.find(sequence);
    if (it == pending_requests_.end()) {
        return nullptr;
    }
    ResponseContext::ptr ctx = it->second;
    pending_requests_.erase(it);
    return ctx;
}

void RpcClient::completeRequest(ResponseContext::ptr ctx, RpcStatus status, Protocol::ptr rsp)
{
    // 取消超时定时器（由定时器自身触发时取消失败，无副作用）
    Timer::ptr timer;
    {
        std::lock_guard lock(mutex_);
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

void RpcClient::recvLoop(std::weak_ptr<RpcClient> weak_this, RpcConnection::ptr session)
{
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
                std::lock_guard lock(self->mutex_);
                self->hb_missed_ = 0;
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
                std::lock_guard lock(self->mutex_);
                auto it = self->request_handlers_.find(key);
                if (it != self->request_handlers_.end()) {
                    handler = it->second;
                }
            }

            Protocol::ptr rsp;
            if (handler) {
                ByteArray::ptr body_ba(new ByteArray);
                body_ba->writeStringWithoutLength(resp->body);
                body_ba->setPosition(0);
                if (resp->cmd == 0) {
                    body_ba->readStringVint(); // skip method name
                }
                rsp = handler(body_ba, resp->sequence);
            } else if (need_rsp) {
                std::string err_msg = resp->cmd != 0 ? "Cmd not found: " + std::to_string(resp->cmd) : "Method not found: " + method;
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
            DEBUG(logger) << "recvLoop: response for sequence " << resp->sequence << " dropped (already timed out)";
        }
    }

    auto self = weak_this.lock();
    if (self) {
        // 取消心跳定时器并取出所有待响应请求（必须释放锁后再完成，避免回调重入死锁）
        std::vector<ResponseContext::ptr> pending;
        {
            std::lock_guard lock(self->mutex_);
            if (self->hb_timer_) {
                self->hb_timer_->cancel();
                self->hb_timer_ = nullptr;
            }
            for (auto& i : self->pending_requests_) {
                pending.push_back(i.second);
            }
            self->pending_requests_.clear();
        }
        for (auto& ctx : pending) {
            self->completeRequest(ctx, RpcStatus::CONNECTION_CLOSED, nullptr);
        }
        // 连接已断开，清理 socket/session 使后续 callInternal 立即失败而非超时
        self->close();
    }
}

} // namespace chen::rpc
