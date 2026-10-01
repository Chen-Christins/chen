#include "h2_stream.h"

#include "../log/log.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

H2Stream::H2Stream(H2Session::ptr h2, int32_t stream_id)
    : HttpSession(h2->getSocket(), false)
    , m_h2(h2)
    , m_streamId(stream_id) {
}

H2Stream::~H2Stream() {
}

int H2Stream::sendResponse(HttpResponse::ptr rsp) {
    H2Session::Command cmd;
    cmd.type = H2Session::Command::RESPONSE;
    cmd.stream_id = m_streamId;
    cmd.response = rsp;
    m_h2->postCommand(std::move(cmd));
    return 0;
}

int H2Stream::sendData(const std::string& data, bool end_stream) {
    H2Session::Command cmd;
    cmd.type = H2Session::Command::DATA;
    cmd.stream_id = m_streamId;
    cmd.data = data;
    cmd.end_stream = end_stream;
    m_h2->postCommand(std::move(cmd));
    return 0;
}

} // namespace chen::http
