/**
 * @file H2Stream.hpp
 * @brief HTTP/2 流管理
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include "H2Session.hpp"
#include "HttpSession.hpp"

namespace chen::http {

class H2Stream : public HttpSession {
public:
    typedef std::shared_ptr<H2Stream> ptr;

    H2Stream(H2Session::ptr h2, int32_t stream_id);
    ~H2Stream();

    int32_t getStreamId() const { return stream_id_; }
    H2Session::ptr getH2Session() const { return h2_; }

    // Override HttpSession
    int sendResponse(HttpResponse::ptr rsp) override;
    int sendResponseHeader(HttpResponse::ptr rsp) override;
    int sendData(const void* buffer, size_t length, bool end_stream = false) override;
    HttpRequest::ptr recvRequest() override;

private:
    H2Session::ptr h2_;
    int32_t stream_id_;
    /// 是否已提交响应头（流式响应必须先发头再发数据）
    bool headers_sent_ = false;
    /// 是否已发送 END_STREAM
    bool ended_ = false;
};

} // namespace chen::http
