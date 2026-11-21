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
    HttpRequest::ptr recvRequest() override;

    // Write data to this stream (for streaming responses)
    int sendData(const std::string& data, bool end_stream = false);

private:
    H2Session::ptr h2_;
    int32_t stream_id_;
};

} // namespace chen::http
