#include "sse_session.h"

#include <sstream>

#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

SSESession::SSESession(Socket::ptr sock, bool owner)
    : HttpSession(sock, owner) {
}

bool SSESession::sendSSEHeaders() {
    std::stringstream ss;
    // HTTP/1.1 200 OK
    ss << "HTTP/1.1 200 OK\r\n"
       << "Content-Type: text/event-stream\r\n"
       << "Cache-Control: no-cache\r\n"
       << "Connection: keep-alive\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "\r\n";
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size()) > 0;
}

int32_t SSESession::sendEvent(const std::string& data, const std::string& id, const std::string& event) {
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
    return writeFixSize(msg.c_str(), msg.size());
}

int32_t SSESession::sendComment(const std::string& comment) {
    std::string msg = ": " + comment + "\n\n";
    return writeFixSize(msg.c_str(), msg.size());
}

int32_t SSESession::sendRetry(uint32_t milliseconds) {
    std::string msg = "retry: " + std::to_string(milliseconds) + "\n\n";
    return writeFixSize(msg.c_str(), msg.size());
}

} // namespace chen::http
