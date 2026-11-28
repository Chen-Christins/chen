#include "H2Stream.hpp"

#include <unistd.h>

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
    // 业务忘记 end_stream 时兜底结束这条流，避免对端一直等 END_STREAM
    if (h2_ && headers_sent_ && !ended_) {
        sendData(nullptr, 0, true);
    }
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

int H2Stream::sendResponseHeader(HttpResponse::ptr rsp)
{
    if (!h2_) {
        return -1;
    }
    H2Session::Command cmd;
    cmd.type = H2Session::Command::HEADERS;
    cmd.stream_id = stream_id_;
    cmd.response = rsp;
    h2_->postCommand(std::move(cmd));
    headers_sent_ = true;
    return 0;
}

int H2Stream::sendData(const void* buffer, size_t length, bool end_stream)
{
    static constexpr size_t kMaxQueuedBytes = 1024 * 1024;
    static constexpr int kMaxWaitMs = 5000;

    if (!h2_ || ended_) {
        return -1;
    }
    if (!headers_sent_) {
        WARN(logger) << "H2Stream::sendData before sendResponseHeader, stream=" << stream_id_;
        return -1;
    }

    // 背压：队列超过水位就让出 fiber，等连接协程把数据交给 nghttp2
    // （结束帧不带数据，不参与水位等待，避免析构里卡住）
    int waited_ms = 0;
    while (length > 0 && h2_->getStreamQueuedBytes(stream_id_) > kMaxQueuedBytes) {
        if (h2_->isStreamClosed(stream_id_) || h2_->isStopping()) {
            return -1;
        }
        if (waited_ms >= kMaxWaitMs) {
            WARN(logger) << "H2Stream::sendData backpressure timeout, stream=" << stream_id_
                         << " queued=" << h2_->getStreamQueuedBytes(stream_id_);
            return -1;
        }
        // usleep 被 hook 过：睡眠期间让出 io 线程
        usleep(1000);
        waited_ms += 1;
    }

    if (h2_->isStreamClosed(stream_id_)) {
        return -1;
    }

    H2Session::Command cmd;
    cmd.type = H2Session::Command::DATA;
    cmd.stream_id = stream_id_;
    if (buffer && length > 0) {
        cmd.data.assign(static_cast<const char*>(buffer), length);
    }
    cmd.end_stream = end_stream;
    h2_->postCommand(std::move(cmd));

    if (end_stream) {
        ended_ = true;
    }
    return static_cast<int>(length);
}

} // namespace chen::http
