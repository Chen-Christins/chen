#include "http_session.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <strings.h>

#include "../fiber/fiber.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "http_parser.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner) {
}

HttpRequest::ptr HttpSession::recvRequestHeader(std::string* leftover) {
    HttpRequestParser::ptr parser(new HttpRequestParser);
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();

    std::shared_ptr<char> buffer(
        new char[buff_size], [](char* ptr) {
        delete [] ptr;
    });

    char* data = buffer.get();
    int offset = 0;
    int eagain_retries = 0;
    static const int MAX_EAGAIN_RETRIES = 3;
    do {
        /* 一直读 */
        int len = read(data + offset, buff_size - offset);
        /* 读取失败了 */
        if (len <= 0) {
            if (len == -1 && errno == EAGAIN
                    && eagain_retries < MAX_EAGAIN_RETRIES
                    && isConnected()) {
                ++eagain_retries;
                auto iom = chen::IOManager::GetThis();
                if (iom) {
                    iom->addEvent(getSocket()->getSocket(), chen::IOManager::READ);
                    chen::Fiber::YieldToHold();
                }
                continue;
            }
            int read_errno = errno;
            close();
            errno = read_errno;
            return nullptr;
        }
        len += offset;
        size_t nparse = parser->execute(data, len);
        if (parser->hasError()) {
            close();
            return nullptr;
        }
        offset = len - nparse;
        if (offset == (int)buff_size) {
            close();
            return nullptr;
        }
        if (parser->isFinished()) {
            break;
        }
    } while (true);

    if (leftover) {
        leftover->assign(data, offset);
    }
    return parser->getData();
}

int HttpSession::readBodyInto(HttpRequest::ptr req, const std::string& leftover) {
    int64_t length = req->getHeaderAs<uint64_t>("content-length", 0);
    if (length <= 0) {
        return 0;
    }

    std::string body;
    body.resize(length);

    size_t copied = std::min<int64_t>(length, leftover.size());
    if (copied > 0) {
        memcpy(&body[0], leftover.data(), copied);
    }

    int64_t remaining = length - copied;
    if (remaining > 0) {
        if (readFixSize(&body[copied], remaining) <= 0) {
            close();
            return -1;
        }
    }
    req->setBody(body);
    return 0;
}

int HttpSession::readBodyStreaming(uint64_t totalLen, BodyCallback cb) {
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    std::shared_ptr<char> buffer(
        new char[buff_size], [](char* ptr) {
        delete [] ptr;
    });
    char* data = buffer.get();

    uint64_t consumed = 0;

    // 回放 header 之后已多读到的 body 字节
    if (!m_pendingBody.empty()) {
        uint64_t n = std::min<uint64_t>(totalLen, m_pendingBody.size());
        if (n > 0) {
            if (cb && !cb(m_pendingBody.data(), n)) {
                return -1;
            }
            consumed = n;
        }
        m_pendingBody.clear();
    }

    while (consumed < totalLen) {
        uint64_t want = std::min<uint64_t>(buff_size, totalLen - consumed);
        int len = readFixSize(data, want);
        if (len <= 0) {
            close();
            return -1;
        }
        consumed += len;
        if (cb && !cb(data, len)) {
            return -1;
        }
    }

    // 已消费部分从剩余量中扣除（下限 0）
    m_bodyRemaining = (m_bodyRemaining > consumed) ? (m_bodyRemaining - consumed) : 0;
    return 0;
}

int HttpSession::drainBody() {
    if (m_bodyRemaining == 0) {
        return 0;
    }

    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    std::shared_ptr<char> buffer(
        new char[buff_size], [](char* ptr) {
        delete [] ptr;
    });
    char* data = buffer.get();

    while (m_bodyRemaining > 0) {
        uint64_t want = std::min<uint64_t>(buff_size, m_bodyRemaining);
        int len = readFixSize(data, want);
        if (len <= 0) {
            close();
            return -1;
        }
        m_bodyRemaining -= len;
    }
    return 0;
}

HttpRequest::ptr HttpSession::recvRequest() {
    std::string leftover;
    auto req = recvRequestHeader(&leftover);
    if (!req) {
        return nullptr;
    }
    if (readBodyInto(req, leftover) < 0) {
        return nullptr;
    }
    req->init();
    return req;
}

int HttpSession::sendResponseHeader(HttpResponse::ptr rsp) {
    std::stringstream ss;

    uint8_t version = rsp->getVersion();
    if (version == HttpRequest::HTTP2_VERSION) {
        ss << "[HTTP/2] :status=" << (uint32_t)rsp->getStatus()
           << " "
           << (rsp->getReason().empty() ? HttpStatusToString(rsp->getStatus()) : rsp->getReason())
           << "\r\n";
    } else {
        ss << "HTTP/"
           << ((uint32_t)(version >> 4))
           << "."
           << ((uint32_t)(version & 0x0F))
           << " "
           << (uint32_t)rsp->getStatus()
           << " "
           << (rsp->getReason().empty() ? HttpStatusToString(rsp->getStatus()) : rsp->getReason())
           << "\r\n";
    }

    for (auto& i : rsp->getHeaders()) {
        if (!rsp->isWebsocket() && strcasecmp(i.first.c_str(), "connection") == 0) {
            continue;
        }
        ss << i.first << ": " << i.second << "\r\n";
    }
    for (auto& i : rsp->getCookies()) {
        ss << "Set-Cookie: " << i << "\r\n";
    }
    if (!rsp->isWebsocket()) {
        ss << "connection: " << (rsp->isClose() ? "close" : "keep-alive") << "\r\n";
    }
    ss << "\r\n";

    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size()) > 0 ? 0 : -1;
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

} // namespace chen::http
