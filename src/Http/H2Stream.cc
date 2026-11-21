#include "H2Stream.hpp"

#include "../Log/Log.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

H2Stream::H2Stream(H2Session::ptr h2, int32_t stream_id)
    : HttpSession(h2->getSocket(), false)
    , h2_(h2)
    , stream_id_(stream_id)
{
}

H2Stream::~H2Stream()
{
}

HttpRequest::ptr H2Stream::recvRequest()
{
    return nullptr;
}

int H2Stream::sendResponse(HttpResponse::ptr rsp)
{
    H2Session::Command cmd;
    cmd.type = H2Session::Command::RESPONSE;
    cmd.stream_id = stream_id_;
    cmd.response = rsp;
    h2_->postCommand(std::move(cmd));
    return 0;
}

int H2Stream::sendData(const std::string& data, bool end_stream)
{
    H2Session::Command cmd;
    cmd.type = H2Session::Command::DATA;
    cmd.stream_id = stream_id_;
    cmd.data = data;
    cmd.end_stream = end_stream;
    h2_->postCommand(std::move(cmd));
    return 0;
}

} // namespace chen::http
