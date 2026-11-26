#include "HttpServer.hpp"

#include <cstring>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"
#include "H2Session.hpp"
#include "HttpSession.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

// HTTP/2 connection preface magic bytes
static const char H2_PREFACE[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
static const size_t H2_PREFACE_LEN = 24;

HttpServer::HttpServer(bool keepalive, IOManager* worker, IOManager* io_worker, IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
    , is_keep_alive_(keepalive)
{
    dispatch_.reset(new ServletDispatch);

    type_ = "http";
}

void HttpServer::setName(const std::string& v)
{
    TcpServer::setName(v);
    dispatch_->setDefault(std::make_shared<NotFoundServlet>(v));
}

static bool detectH2Preface(Socket::ptr client)
{
    uint8_t buf[H2_PREFACE_LEN];
    int len = client->recv(buf, H2_PREFACE_LEN, MSG_PEEK);
    if (len != (int)H2_PREFACE_LEN) {
        return false;
    }
    return memcmp(buf, H2_PREFACE, H2_PREFACE_LEN) == 0;
}

void HttpServer::handleClient(Socket::ptr client)
{
    DEBUG(logger) << "handleClient " << *client;

    // If this is a dedicated HTTP/2 server (type="http2"), always use HTTP/2.
    // If negotiateH2 is true on a type="http" server, try to detect H2 preface.
    bool use_h2 = (getType() == "http2");
    if (!use_h2 && negotiate_h2_) {
        use_h2 = detectH2Preface(client);
    }

    if (use_h2) {
        DEBUG(logger) << "Starting HTTP/2 session";
        auto h2 = std::make_shared<H2Session>(client, worker_, dispatch_);
        if (h2->init()) {
            h2->run();
        }
        h2->close();
        return;
    }

    // HTTP/1.1 path
    HttpSession::ptr session(new HttpSession(client));
    do {
        // draining 之后不再复用连接读取下一个请求：当前请求处理完即退出
        if (isDraining()) {
            DEBUG(logger) << "http server draining, stop keep-alive client=" << *client;
            break;
        }

        std::string leftover;
        auto req = session->recvRequestHeader(&leftover);
        if (!req) {
            // EAGAIN/EWOULDBLOCK 在非阻塞 socket 上是正常行为，表示当前无数据可读。
            // 通常由 hook 机制 (do_io) 通过 epoll yield 自动处理，但在极端并发下
            // (fd 快速复用竞态) 或连接正常关闭时 (recv 返回 0，errno 保持旧值)
            // 可能泄漏至此。这不是真正的错误，降级为 DEBUG 避免日志噪声。
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                DEBUG(logger) << "recv request EAGAIN (normal for non-blocking socket)"
                              << " client:" << *client << " keep-alive=" << is_keep_alive_;
            } else {
                WARN(logger) << "recv http request fail, errno=" << errno << " errstr=" << strerror(errno) << " client:" << *client << " keep-alive=" << is_keep_alive_;
            }
            break;
        }

        req->init();

        // draining 时响应带 Connection: close，当前请求处理完即断开
        const bool close_conn = req->isClose() || !is_keep_alive_ || isDraining();
        HttpResponse::ptr rsp(new HttpResponse(req->getVersion(), close_conn));
        rsp->setHeader("Server", getName());

        {
            // 请求作用域：构造时 in_flight++，析构时 in_flight--，
            // 若此时已 draining 且连接空闲，顺手关闭连接（见 TcpServer::RequestGuard）
            TcpServer::RequestGuard guard(this, client);

            auto slt = dispatch_->matchForRequest(req);
            if (slt && slt->isStreamingBody()) {
                session->setPendingBody(leftover);
                session->setBodyRemaining(req->getHeaderAs<uint64_t>("content-length", 0));
                slt->handle(req, rsp, session);

                // servlet 可能提前返回（如 404/403），未消费完 body；排空以保持 keep-alive 一致
                if (!rsp->isStreaming()) {
                    session->drainBody();
                }
            } else {
                if (session->readBodyInto(req, leftover) < 0) {
                    WARN(logger) << "read http request body fail"
                                 << " client:" << *client;
                    break;
                }
                slt->handle(req, rsp, session);
            }

            if (rsp->isStreaming()) {
                break;
            }

            session->sendResponse(rsp);
        }

        if (close_conn) {
            break;
        }
    } while (true);
    session->close();
}

} // namespace chen::http
