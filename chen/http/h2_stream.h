/**
 * @file h2_stream.h
 * @brief HTTP/2 流管理
 */
#pragma once

#include "h2_session.h"
#include "http_session.h"

namespace chen::http {

class H2Stream : public HttpSession {
public:
    typedef std::shared_ptr<H2Stream> ptr;

    H2Stream(H2Session::ptr h2, int32_t stream_id);
    ~H2Stream();

    int32_t getStreamId() const { return m_streamId; }
    H2Session::ptr getH2Session() const { return m_h2; }

    // Override HttpSession
    int sendResponse(HttpResponse::ptr rsp) override;
    HttpRequest::ptr recvRequest() override { return nullptr; }

    // Write data to this stream (for streaming responses)
    int sendData(const std::string& data, bool end_stream = false);

private:
    H2Session::ptr m_h2;
    int32_t m_streamId;
};

} // namespace chen::http
