/**
 * @file RockStream.hpp
 * @brief Rock 协议流定义
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include "../Socket/SocketStream.hpp"
#include "../Streams/LoadBalance.hpp"
#include "RockProtocol.hpp"

namespace chen {

struct RockResult {
    typedef std::shared_ptr<RockResult> ptr;

    RockResult(int32_t _result, int32_t _used, RockResponse::ptr rsp, RockRequest::ptr req);

    int32_t result;
    int32_t used;
    RockResponse::ptr response;
    RockRequest::ptr request;

    std::string toString() const;
};

class RockStream : public AsyncSocketStream {
public:
    typedef std::shared_ptr<RockStream> ptr;
    typedef std::function<bool(RockRequest::ptr, RockResponse::ptr, RockStream::ptr)> request_handler;
    typedef std::function<bool(RockNotify::ptr, RockStream::ptr)> notify_handler;
    /// 请求作用域工厂：返回的对象在整个「处理请求 + 回包」期间存活，析构即表示该请求结束
    typedef std::function<std::shared_ptr<void>()> request_scope_factory;

    RockStream(Socket::ptr sock);
    ~RockStream();

    int32_t sendMessage(Message::ptr msg);
    RockResult::ptr request(RockRequest::ptr req, uint32_t timeout_ms);

    request_handler getRequestHandler() const { return request_handler_; }
    notify_handler getNotifyHandler() const { return notify_handler_; }

    void setRequestHandler(request_handler v) { request_handler_ = v; }
    void setNotifyHandler(notify_handler v) { notify_handler_ = v; }

    /**
     * @brief 设置请求作用域工厂（用于 drain 的在途计数）
     * @details RockServer 用它把 TcpServer::RequestGuard 挂到每个请求的处理期间：
     *          handleRequest 开头创建、回包之后析构，因此 draining 时连接会在回包后才关闭。
     */
    void setRequestScopeFactory(request_scope_factory v) { request_scope_factory_ = std::move(v); }

    /**
     * @brief 等待读写协程退出（连接关闭后返回）
     * @details handleClient 用它把连接的生命周期和 TcpServer 的 active_clients_ 对齐：
     *          只要连接还在，处理函数就不返回，drain 的在途计数与 closeAllClients() 才能生效。
     */
    void waitClose() { waitFiber(); }

    template <class T>
    void setData(const T& v)
    {
        data_ = v;
    }

    template <class T>
    T getData()
    {
        try {
            return boost::any_cast<T>(data_);
        } catch (...) {
        }
        return T();
    }

protected:
    struct RockSendCtx : public SendCtx {
        typedef std::shared_ptr<RockSendCtx> ptr;
        Message::ptr msg;

        virtual bool doSend(AsyncSocketStream::ptr stream) override;
    };

    struct RockCtx : public Ctx {
        typedef std::shared_ptr<RockCtx> ptr;
        RockRequest::ptr request;
        RockResponse::ptr response;

        virtual bool doSend(AsyncSocketStream::ptr stream) override;
    };

    virtual Ctx::ptr doRecv() override;

    void handleRequest(RockRequest::ptr req);
    void handleNotify(RockNotify::ptr nty);

private:
    RockMessageDecoder::ptr decoder_;
    request_handler request_handler_;
    notify_handler notify_handler_;
    request_scope_factory request_scope_factory_;
    boost::any data_;
};

class RockSession : public RockStream {
public:
    typedef std::shared_ptr<RockSession> ptr;
    RockSession(Socket::ptr sock);
};

class RockConnection : public RockStream {
public:
    typedef std::shared_ptr<RockConnection> ptr;
    RockConnection();
    bool connect(Address::ptr addr);
};

class RockSDLoadBalance : public SDLoadBalance {
public:
    typedef std::shared_ptr<RockSDLoadBalance> ptr;
    RockSDLoadBalance(IServiceDiscovery::ptr sd);

    virtual void start();
    virtual void stop();
    void start(const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& confs);

    RockResult::ptr request(const std::string& domain, const std::string& service, RockRequest::ptr req, uint32_t timeout_ms, uint64_t idx = -1);
};

} // namespace chen
