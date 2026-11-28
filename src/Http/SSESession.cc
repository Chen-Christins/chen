#include "SSESession.hpp"

#include <sstream>

#include "../Log/Log.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

SSESession::SSESession(HttpSession::ptr session)
    : HttpSession(session ? session->getSocket() : nullptr, false)
    , session_(std::move(session))
{
}

SSESession::SSESession(Socket::ptr sock, bool owner)
    : HttpSession(sock, owner)
    , session_(std::make_shared<HttpSession>(sock, false))
{
}

bool SSESession::sendSSEHeaders()
{
    if (!session_) {
        WARN(logger) << "sendSSEHeaders without http session";
        return false;
    }

    auto rsp = std::make_shared<HttpResponse>();
    rsp->setStatus(HttpStatus::OK);
    rsp->setHeader("Content-Type", "text/event-stream");
    rsp->setHeader("Cache-Control", "no-cache");
    rsp->setHeader("Access-Control-Allow-Origin", "*");
    // SSE 没有 Content-Length，H1 只能靠关闭连接结束响应，明确告诉客户端
    rsp->setClose(true);

    return session_->sendResponseHeader(rsp) == 0;
}

int32_t SSESession::sendEvent(const std::string& data, const std::string& id, const std::string& event)
{
    if (!session_) {
        return -1;
    }

    std::stringstream ss;
    if (!id.empty()) {
        ss << "id: " << id << "\n";
    }
    if (!event.empty()) {
        ss << "event: " << event << "\n";
    }
    // data 字段可能包含多行，每行都需要 "data: " 前缀
    if (data.empty()) {
        ss << "data: \n";
    } else {
        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line)) {
            ss << "data: " << line << "\n";
        }
    }
    ss << "\n";
    std::string msg = ss.str();
    return session_->sendData(msg.data(), msg.size());
}

int32_t SSESession::sendComment(const std::string& comment)
{
    if (!session_) {
        return -1;
    }
    std::string msg = ": " + comment + "\n\n";
    return session_->sendData(msg.data(), msg.size());
}

int32_t SSESession::sendRetry(uint32_t milliseconds)
{
    if (!session_) {
        return -1;
    }
    std::string msg = "retry: " + std::to_string(milliseconds) + "\n\n";
    return session_->sendData(msg.data(), msg.size());
}

int32_t SSESession::closeStream()
{
    if (!session_) {
        return -1;
    }
    return session_->sendData(nullptr, 0, true);
}

} // namespace chen::http
