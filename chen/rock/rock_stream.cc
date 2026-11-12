#include "rock_stream.h"

#include <algorithm>

#include "../config/config.h"
#include "../iomanager/worker.h"
#include "../socket/socket.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>::ptr g_rock_services =
    Config::Lookup("rock_services", std::unordered_map<std::string, std::unordered_map<std::string, std::string>>(), "rock_services");

static ConfigVar<std::string>::ptr g_rock_service_worker =
    Config::Lookup("rock.service_worker", std::string(), "worker scheduler name for rock service connections, empty means current scheduler");

static ConfigVar<uint32_t>::ptr g_rock_request_max_retry =
    Config::Lookup("rock.request_max_retry", (uint32_t)2, "max retry count when rock request fails without response");

RockResult::RockResult(int32_t _result, int32_t _used, RockResponse::ptr rsp, RockRequest::ptr req)
    : result(_result)
    , used(_used)
    , response(rsp)
    , request(req)
{
}

std::string RockResult::toString() const
{
    std::stringstream ss;
    ss << "[RockResult result=" << result << " used=" << used << " response=" << (response ? response->toString() : "null")
       << " request=" << (request ? request->toString() : "null") << "]";
    return ss.str();
}

RockStream::RockStream(Socket::ptr sock)
    : AsyncSocketStream(sock, true)
    , m_decoder(new RockMessageDecoder)
{
    DEBUG(logger) << "RockStream::RockStream " << this << " " << (sock ? sock->toString() : "");
}

RockStream::~RockStream()
{
    DEBUG(logger) << "RockStream::~RockStream " << this << " " << (m_socket ? m_socket->toString() : "");
}

int32_t RockStream::sendMessage(Message::ptr msg)
{
    if (isConnected()) {
        RockSendCtx::ptr ctx(new RockSendCtx);
        ctx->msg = msg;
        enqueue(ctx);
        return 1;
    } else {
        return -1;
    }
}

RockResult::ptr RockStream::request(RockRequest::ptr req, uint32_t timeout_ms)
{
    if (isConnected()) {
        RockCtx::ptr ctx(new RockCtx);
        ctx->request = req;
        ctx->sn = req->getSn();
        ctx->timeout = timeout_ms;
        ctx->scheduler = Scheduler::GetThis();
        ctx->fiber = Fiber::GetThis();
        addCtx(ctx);
        uint64_t ts = TimeUtil::GetCurrentMs();
        ctx->timer = IOManager::GetThis()->addTimer(timeout_ms, std::bind(&RockStream::onTimeOut, shared_from_this(), ctx));
        enqueue(ctx);
        Fiber::YieldToHold();
        return std::make_shared<RockResult>(ctx->result, TimeUtil::GetCurrentMs() - ts, ctx->response, req);
    } else {
        return std::make_shared<RockResult>(AsyncSocketStream::NOT_CONNECT, 0, nullptr, req);
    }
}

bool RockStream::RockSendCtx::doSend(AsyncSocketStream::ptr stream)
{
    return std::dynamic_pointer_cast<RockStream>(stream)->m_decoder->serializeTo(stream, msg) > 0;
}

bool RockStream::RockCtx::doSend(AsyncSocketStream::ptr stream)
{
    return std::dynamic_pointer_cast<RockStream>(stream)->m_decoder->serializeTo(stream, request) > 0;
}

AsyncSocketStream::Ctx::ptr RockStream::doRecv()
{
    // INFO(logger) << "doRecv " << this;
    auto msg = m_decoder->parseFrom(shared_from_this());
    if (!msg) {
        innerClose();
        return nullptr;
    }

    int type = msg->getType();
    if (type == Message::RESPONSE) {
        auto rsp = std::dynamic_pointer_cast<RockResponse>(msg);
        if (!rsp) {
            WARN(logger) << "RockStream doRecv response not RockResponse: " << msg->toString();
            return nullptr;
        }
        RockCtx::ptr ctx = getAndDelCtxAs<RockCtx>(rsp->getSn());
        if (!ctx) {
            WARN(logger) << "RockStream request timeout reponse=" << rsp->toString();
            return nullptr;
        }
        ctx->result = rsp->getResult();
        ctx->response = rsp;
        return ctx;
    } else if (type == Message::REQUEST) {
        auto req = std::dynamic_pointer_cast<RockRequest>(msg);
        if (!req) {
            WARN(logger) << "RockStream doRecv request not RockRequest: " << msg->toString();
            return nullptr;
        }
        if (m_requestHandler) {
            m_worker->schedule(std::bind(&RockStream::handleRequest, std::dynamic_pointer_cast<RockStream>(shared_from_this()), req));
        } else {
            WARN(logger) << "unhandle request " << req->toString();
        }
    } else if (type == Message::NOTIFY) {
        auto nty = std::dynamic_pointer_cast<RockNotify>(msg);
        if (!nty) {
            WARN(logger) << "RockStream doRecv notify not RockNotify: " << msg->toString();
            return nullptr;
        }

        if (m_notifyHandler) {
            m_worker->schedule(std::bind(&RockStream::handleNotify, std::dynamic_pointer_cast<RockStream>(shared_from_this()), nty));
        } else {
            WARN(logger) << "unhandle notify " << nty->toString();
        }
    } else {
        WARN(logger) << "RockStream recv unknow type=" << type << " msg: " << msg->toString();
    }
    return nullptr;
}

void RockStream::handleRequest(RockRequest::ptr req)
{
    RockResponse::ptr rsp = req->createResponse();
    if (!m_requestHandler(req, rsp, std::dynamic_pointer_cast<RockStream>(shared_from_this()))) {
        sendMessage(rsp);
        // innerClose();
        close();
    } else {
        sendMessage(rsp);
    }
}

void RockStream::handleNotify(RockNotify::ptr nty)
{
    if (!m_notifyHandler(nty, std::dynamic_pointer_cast<RockStream>(shared_from_this()))) {
        // innerClose();
        close();
    }
}

RockSession::RockSession(Socket::ptr sock)
    : RockStream(sock)
{
    m_autoConnect = false;
}

RockConnection::RockConnection()
    : RockStream(nullptr)
{
    m_autoConnect = true;
}

bool RockConnection::connect(Address::ptr addr)
{
    m_socket = Socket::CreateTCP(addr);
    return m_socket->connect(addr);
}

RockSDLoadBalance::RockSDLoadBalance(IServiceDiscovery::ptr sd)
    : SDLoadBalance(sd)
{
}

static SocketStream::ptr create_rock_stream(ServiceItemInfo::ptr info)
{
    IPAddress::ptr addr = Address::LookupAnyIPAddress(info->getIp());
    if (!addr) {
        ERROR(logger) << "invalid service info: " << info->toString();
        return nullptr;
    }
    addr->setPort(info->getPort());

    RockConnection::ptr conn(new RockConnection);

    IOManager* io = nullptr;
    auto worker_name = g_rock_service_worker->getValue();
    if (!worker_name.empty()) {
        auto sched = WorkerMgr::GetInstance()->getAsIOManager(worker_name);
        if (sched) {
            io = sched.get();
        } else {
            WARN(logger) << "rock.service_worker=" << worker_name << " not found, fallback to current IOManager";
        }
    }
    if (!io) {
        io = IOManager::GetThis();
    }
    if (!io) {
        ERROR(logger) << "create rock stream fail: no IOManager, service=" << info->toString();
        return nullptr;
    }

    io->schedule([conn, addr]() {
        if (!conn->connect(addr)) {
            WARN(logger) << "rock connect fail, will retry, addr=" << *addr;
        }
        conn->start();
    });
    return conn;
}

void RockSDLoadBalance::start()
{
    m_cb = create_rock_stream;
    initConf(g_rock_services->getValue());
    SDLoadBalance::start();
}

void RockSDLoadBalance::start(const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& confs)
{
    m_cb = create_rock_stream;
    initConf(confs);
    SDLoadBalance::start();
}

void RockSDLoadBalance::stop()
{
    SDLoadBalance::stop();
}

RockResult::ptr RockSDLoadBalance::request(
    const std::string& domain,
    const std::string& service,
    RockRequest::ptr req,
    uint32_t timeout_ms,
    uint64_t idx)
{
    auto lb = get(domain, service);
    if (!lb) {
        return std::make_shared<RockResult>(ILoadBalance::NO_SERVICE, 0, nullptr, req);
    }

    const uint32_t max_retry = g_rock_request_max_retry->getValue();
    const uint64_t start_ms = TimeUtil::GetCurrentMs();
    RockResult::ptr last_result;

    for (uint32_t attempt = 0; attempt <= max_retry; ++attempt) {
        uint64_t now = TimeUtil::GetCurrentMs();
        int64_t remain = (int64_t)timeout_ms - (int64_t)(now - start_ms);
        if (remain <= 0 && attempt > 0) {
            break;
        }

        auto conn = lb->get(idx == (uint64_t)-1 ? (uint64_t)-1 : idx + attempt);
        if (!conn) {
            if (!last_result) {
                return std::make_shared<RockResult>(ILoadBalance::NO_CONNECTION, 0, nullptr, req);
            }
            break;
        }

        auto stream = conn->getStreamAs<RockStream>();
        if (!stream) {
            if (!last_result) {
                return std::make_shared<RockResult>(ILoadBalance::NO_CONNECTION, 0, nullptr, req);
            }
            break;
        }

        auto& stats = conn->get(now / 1000);
        stats.incDoing(1);
        stats.incTotal(1);

        // timeout_ms 是总预算，按剩余尝试次数切分，避免第一次超时吃光预算导致无法重试
        uint32_t attempts_left = max_retry - attempt + 1;
        uint32_t attempt_timeout = remain > 0 ? (uint32_t)std::max<int64_t>(1, remain / attempts_left) : 1;
        auto result = stream->request(req, attempt_timeout);
        uint64_t used = TimeUtil::GetCurrentMs() - now;

        if (result->response) {
            // 收到响应即视为业务结果（即使 result 非 0 也不重试，避免重复执行）
            stats.incOks(1);
            stats.incUsedTime(used);
            conn->resetFail();
            stats.decDoing(1);
            return result;
        }

        if (result->result == AsyncSocketStream::TIMEOUT) {
            stats.incTimeouts(1);
        } else if (result->result < 0) {
            stats.incErrs(1);
        }
        stats.decDoing(1);

        conn->incFail();
        last_result = result;

        // 只有传输层错误才换实例重试；业务错误不重试
        if (result->result != AsyncSocketStream::TIMEOUT && result->result != AsyncSocketStream::IO_ERROR
            && result->result != AsyncSocketStream::NOT_CONNECT) {
            break;
        }
    }

    if (!last_result) {
        return std::make_shared<RockResult>(ILoadBalance::NO_CONNECTION, 0, nullptr, req);
    }
    return last_result;
}

} // namespace chen
