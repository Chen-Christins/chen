#include "H2Session.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"
#include "H2Stream.hpp"
#include "H2Util.hpp"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

H2Session::H2Session(Socket::ptr sock, IOManager* worker, ServletDispatch::ptr dispatch)
    : socket_(sock)
    , worker_(worker)
    , dispatch_(dispatch)
{
}

H2Session::~H2Session()
{
    close();
}

bool H2Session::init()
{
    nghttp2_session_callbacks* callbacks;
    int rv = nghttp2_session_callbacks_new(&callbacks);
    if (rv != 0) {
        ERROR(logger) << "nghttp2_session_callbacks_new failed: " << nghttp2_strerror(rv);
        return false;
    }

    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback2(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
    nghttp2_session_callbacks_set_error_callback2(
        callbacks, [](nghttp2_session* session, int lib_error_code, const char* msg, size_t len, void* user_data) -> int {
            ERROR(logger) << "nghttp2 error: " << std::string(msg, len) << " (code=" << lib_error_code << ")";
            return 0;
        });

    rv = nghttp2_session_server_new(&session_, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);

    if (rv != 0) {
        ERROR(logger) << "nghttp2_session_server_new failed: " << nghttp2_strerror(rv);
        return false;
    }

    // Configure server settings (will be sent after receiving client preface)
    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
    };
    rv = nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, iv, sizeof(iv) / sizeof(iv[0]));
    if (rv != 0) {
        ERROR(logger) << "nghttp2_submit_settings failed: " << nghttp2_strerror(rv);
        return false;
    }

    return true;
}

void H2Session::run()
{
    std::vector<uint8_t> buf(65536);
    bool has_active_streams = false;

    while (!stopping_) {
        processCommands();

        bool need_read = nghttp2_session_want_read(session_) != 0;

        if (need_read) {
            // 有在途 stream 或正在 drain 时用短超时，保证响应 / GOAWAY 及时处理；
            // 空闲时也不能无限阻塞：startDrain() 只是投递命令，需要本循环醒过来才能发出 GOAWAY
            const bool fast_wakeup = has_active_streams || draining_;
            // 流式数据由业务 fiber 投递，本循环靠 recv 超时醒来处理；活跃时 50ms 一醒，
            // 空闲时 500ms，避免为 SSE/流式响应引入空转线程
            socket_->setRecvTimeout(fast_wakeup ? 50 : 500);

            int len = socket_->recv(buf.data(), buf.size());
            if (len == 0) {
                DEBUG(logger) << "H2 connection closed by peer";
                break;
            }
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                    // Timeout, loop back to check commands
                } else {
                    DEBUG(logger) << "H2 recv error: " << strerror(errno);
                    break;
                }
            } else {
                ssize_t readlen = nghttp2_session_mem_recv(session_, buf.data(), len);
                if (readlen < 0) {
                    WARN(logger) << "nghttp2_session_mem_recv error: " << nghttp2_strerror((int)readlen);
                    break;
                }
            }
        }

        processCommands();

        if (nghttp2_session_want_write(session_)) {
            doSend();
            pending_bodies_.clear();
        }

        {
            std::shared_lock lock(stream_mutex_);
            has_active_streams = !streams_.empty();
        }

        if (draining_) {
            // GOAWAY 已发出：等所有在途 stream 关闭后结束会话
            if (!has_active_streams) {
                INFO(logger) << "H2 session drained, closing connection";
                break;
            }
            continue;
        }

        if (need_read == 0 && nghttp2_session_want_write(session_) == 0) {
            if (!has_active_streams) {
                DEBUG(logger) << "H2 session idle, closing";
                break;
            }
        }
    }

    // 正常退出时补发 GOAWAY；drain 路径已经在 startDrain() 里发过，不重复提交
    if (!stopping_ && !draining_) {
        nghttp2_submit_goaway(
            session_, NGHTTP2_FLAG_NONE, nghttp2_session_get_last_proc_stream_id(session_), 0, nullptr, 0);
        doSend();
    }
}

void H2Session::startDrain()
{
    bool expected = false;
    if (!draining_.compare_exchange_strong(expected, true)) {
        return;
    }
    INFO(logger) << "H2 session drain requested, sending GOAWAY";

    Command cmd;
    cmd.type = Command::GOAWAY;
    cmd.stream_id = 0;
    cmd.end_stream = false;
    postCommand(std::move(cmd));
}

void H2Session::close()
{
    stopping_ = true;
    {
        std::unique_lock lock(stream_mutex_);
        for (auto& kv : stream_states_) {
            kv.second->closed = true;
        }
        stream_states_.clear();
    }
    if (session_) {
        nghttp2_session_del(session_);
        session_ = nullptr;
    }
}

size_t H2Session::getStreamQueuedBytes(int32_t stream_id)
{
    std::shared_lock lock(stream_mutex_);
    auto it = stream_states_.find(stream_id);
    return it == stream_states_.end() ? 0 : it->second->queued_bytes;
}

bool H2Session::isStreamClosed(int32_t stream_id)
{
    std::shared_lock lock(stream_mutex_);
    auto it = stream_states_.find(stream_id);
    // 注意：state 尚不存在表示“HEADERS 命令还没被连接协程处理”，不能当成已关闭，
    // 否则 servlet 刚发出响应头就立刻 sendData 会被误判为流已关闭。
    return it != stream_states_.end() && it->second->closed;
}

void H2Session::postCommand(Command cmd)
{
    {
        std::lock_guard lock(cmd_mutex_);
        commands_.push(std::move(cmd));
    }
}

int H2Session::sendResponse(int32_t stream_id, HttpResponse::ptr rsp)
{
    std::list<std::string> temp_strs;
    auto nva = build_response_nva((int)rsp->getStatus(), rsp->getHeaders(), rsp->getCookies(), temp_strs);

    if (rsp->getBody().empty()) {
        int rv = nghttp2_submit_response(session_, stream_id, nva.data(), nva.size(), nullptr);
        if (rv != 0) {
            ERROR(logger) << "nghttp2_submit_response failed: " << nghttp2_strerror(rv);
        }
        return rv;
    }

    // Response has a body — use a shared body context
    struct BodyContext {
        std::string body;
        size_t offset = 0;
    };
    auto body_ctx = std::make_shared<BodyContext>();
    body_ctx->body = rsp->getBody();

    nghttp2_data_provider data_prd;
    data_prd.source.ptr = body_ctx.get();
    data_prd.read_callback = [](nghttp2_session* session, int32_t sid, uint8_t* out, size_t length, uint32_t* data_flags, nghttp2_data_source* source,
                                void* user_data) -> ssize_t {
        auto* bc = static_cast<BodyContext*>(source->ptr);
        size_t remaining = bc->body.size() - bc->offset;
        size_t to_copy = std::min(length, remaining);
        if (to_copy > 0) {
            memcpy(out, bc->body.data() + bc->offset, to_copy);
            bc->offset += to_copy;
        }
        if (bc->offset >= bc->body.size()) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }
        return (ssize_t)to_copy;
    };

    // Keep body_ctx alive via stream user data
    // We store a shared_ptr<void> via the raw ptr hack — actually,
    // nghttp2 doesn't manage lifetime of user_data after the call completes.
    // But the read_callback references source.ptr which points to body_ctx.
    // The body_ctx MUST stay alive until nghttp2 finishes sending.
    // Since nghttp2 calls read_callback during nghttp2_session_mem_send(),
    // and we call mem_send() in our connection fiber, body_ctx just needs
    // to stay alive for the duration of the doSend() call.
    // We store it in a local shared_ptr here and pass it to processCommands
    // context... Actually, this is simpler: we just need body_ctx to live
    // until doSend() is called. The Command queue processing handles this
    // synchronously in the connection fiber.

    // Keep body_ctx alive until doSend() drains it
    pending_bodies_.push_back(body_ctx);

    int rv = nghttp2_submit_response(session_, stream_id, nva.data(), nva.size(), &data_prd);
    if (rv != 0) {
        ERROR(logger) << "nghttp2_submit_response with body failed: " << nghttp2_strerror(rv);
    }
    return rv;
}

std::shared_ptr<H2Session::StreamState> H2Session::getOrCreateStreamState(int32_t stream_id)
{
    auto it = stream_states_.find(stream_id);
    if (it != stream_states_.end()) {
        return it->second;
    }
    auto st = std::make_shared<StreamState>();
    stream_states_[stream_id] = st;
    return st;
}

void H2Session::submitResponseHeaders(int32_t stream_id, HttpResponse::ptr rsp)
{
    std::list<std::string> temp_strs;
    auto nva = build_response_nva((int)rsp->getStatus(), rsp->getHeaders(), rsp->getCookies(), temp_strs);

    // 头与 data provider 必须一次性提交：nghttp2 只允许每条流同时存在一个待发 HEADERS/DATA，
    // 先 submit_headers 再 submit_data 会拿到 NGHTTP2_ERR_DATA_EXIST（HEADERS 还没发出去）。
    // provider 在队列空时返回 DEFERRED，发送由 sendData() 的 resume_data 驱动。
    nghttp2_data_provider prd;
    prd.source.ptr = this;
    prd.read_callback = &H2Session::onStreamDataRead;

    int rv = nghttp2_submit_response(session_, stream_id, nva.data(), nva.size(), &prd);
    if (rv != 0) {
        WARN(logger) << "nghttp2_submit_response (streaming) failed for stream " << stream_id << ": "
                     << nghttp2_strerror(rv);
    }
}

ssize_t H2Session::onStreamDataRead(nghttp2_session* session,
    int32_t stream_id,
    uint8_t* buf,
    size_t length,
    uint32_t* data_flags,
    nghttp2_data_source* source,
    void* user_data)
{
    (void)session;
    (void)source;
    auto* self = static_cast<H2Session*>(user_data);

    std::unique_lock lock(self->stream_mutex_);
    auto it = self->stream_states_.find(stream_id);
    if (it == self->stream_states_.end()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }

    auto& st = *it->second;
    if (st.chunks.empty()) {
        if (st.eof) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        }
        // 暂时没有数据：挂起，等 sendData() 入队后 resume
        return NGHTTP2_ERR_DEFERRED;
    }

    auto& chunk = st.chunks.front();
    const size_t n = std::min(length, chunk.size());
    if (n > 0) {
        memcpy(buf, chunk.data(), n);
        st.queued_bytes -= n;
    }
    if (n == chunk.size()) {
        st.chunks.pop_front();
    } else {
        chunk.erase(0, n);
    }

    if (st.chunks.empty() && st.eof) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return (ssize_t)n;
}

void H2Session::handleStreamData(const Command& cmd)
{
    std::unique_lock lock(stream_mutex_);
    // state 可能还没创建（servlet 先发 DATA 后由连接协程补建），这里补齐；
    // 只有显式标记 closed 的流才丢弃，避免给已结束的流提交数据帧。
    auto st_ptr = getOrCreateStreamState(cmd.stream_id);
    if (st_ptr->closed) {
        return;
    }

    auto& st = *st_ptr;
    if (!cmd.data.empty()) {
        st.queued_bytes += cmd.data.size();
        st.chunks.push_back(cmd.data);
    }
    if (cmd.end_stream) {
        st.eof = true;
    }

    // provider 是随 HEADERS 一起提交的，这里只需要把 deferred 的流唤醒；
    // provider 还没被调用过时 resume 会返回 NGHTTP2_ERR_INVALID_ARGUMENT，忽略即可
    if (!st.chunks.empty() || st.eof) {
        int rv = nghttp2_session_resume_data(session_, cmd.stream_id);
        if (rv != 0 && rv != NGHTTP2_ERR_INVALID_ARGUMENT) {
            WARN(logger) << "nghttp2_session_resume_data failed for stream " << cmd.stream_id << ": "
                         << nghttp2_strerror(rv);
        }
    }
}

void H2Session::processCommands()
{
    std::queue<Command> cmds;
    {
        std::lock_guard lock(cmd_mutex_);
        cmds.swap(commands_);
    }

    while (!cmds.empty()) {
        auto& cmd = cmds.front();
        switch (cmd.type) {
        case Command::RESPONSE:
            sendResponse(cmd.stream_id, cmd.response);
            break;
        case Command::HEADERS: {
            if (cmd.response) {
                {
                    std::unique_lock lock(stream_mutex_);
                    getOrCreateStreamState(cmd.stream_id);
                }
                submitResponseHeaders(cmd.stream_id, cmd.response);
            }
            break;
        }
        case Command::DATA:
            handleStreamData(cmd);
            break;
        case Command::GOAWAY:
            nghttp2_submit_goaway(
                session_, NGHTTP2_FLAG_NONE, nghttp2_session_get_last_proc_stream_id(session_), 0, nullptr, 0);
            break;
        }
        cmds.pop();
    }
}

void H2Session::doSend()
{
    for (;;) {
        const uint8_t* data = nullptr;
        ssize_t rv = nghttp2_session_mem_send(session_, &data);
        if (rv < 0) {
            ERROR(logger) << "nghttp2_session_mem_send error: " << nghttp2_strerror((int)rv);
            break;
        }
        if (rv == 0) {
            break;
        }
        int written = socket_->send(data, rv);
        if (written < 0) {
            ERROR(logger) << "H2 socket send error: " << strerror(errno);
            break;
        }
        if (written != rv) {
            ERROR(logger) << "H2 partial write: " << written << "/" << rv;
            break;
        }
    }
}

void H2Session::processStream(int32_t stream_id)
{
    // 只取副本，不持有 streams_ 内的引用：processStream 跑在 worker 线程池，
    // 而对端 RST_STREAM 会在会话线程的 on_stream_close 里 erase 该节点
    std::shared_ptr<HttpRequest> req;
    std::string body;
    {
        std::shared_lock lock(stream_mutex_);
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            return;
        }
        req = it->second.request;
        body = it->second.body;
    }

    if (!req) {
        return;
    }

    if (!body.empty()) {
        req->setBody(body);
    }
    req->init();

    auto rsp = std::make_shared<HttpResponse>(HttpRequest::kHttp2Version, false);
    auto stream = std::make_shared<H2Stream>(shared_from_this(), stream_id);

    if (dispatch_) {
        dispatch_->handle(req, rsp, stream);
    }

    if (rsp->isStreaming()) {
        return;
    }

    {
        // 处理期间流可能已被对端关闭，给已关闭的流提交响应没有意义
        std::shared_lock lock(stream_mutex_);
        if (streams_.find(stream_id) == streams_.end()) {
            DEBUG(logger) << "H2 stream " << stream_id << " closed before response, dropping";
            return;
        }
    }

    Command cmd;
    cmd.type = Command::RESPONSE;
    cmd.stream_id = stream_id;
    cmd.response = rsp;
    postCommand(cmd);
}

int H2Session::on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    auto* h2 = static_cast<H2Session*>(user_data);
    if (frame->hd.type != NGHTTP2_HEADERS)
        return 0;
    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        std::unique_lock lock(h2->stream_mutex_);
        auto& ctx = h2->streams_[frame->hd.stream_id];
        ctx.request = std::make_shared<HttpRequest>(HttpRequest::kHttp2Version, false);
        ctx.request->setStreamId(frame->hd.stream_id);
        ctx.headers_done = false;
        ctx.body_done = false;
        ctx.body.clear();
    }
    return 0;
}

int H2Session::on_header(
    nghttp2_session* session,
    const nghttp2_frame* frame,
    nghttp2_rcbuf* name,
    nghttp2_rcbuf* value,
    uint8_t flags,
    void* user_data)
{
    auto* h2 = static_cast<H2Session*>(user_data);
    std::shared_lock lock(h2->stream_mutex_);
    auto it = h2->streams_.find(frame->hd.stream_id);
    if (it == h2->streams_.end())
        return 0;

    auto& ctx = it->second;
    if (!ctx.request)
        return 0;

    auto name_str = rcbuf_to_string(name);
    auto value_str = rcbuf_to_string(value);

    if (name_str == ":method") {
        ctx.request->setMethod(StringToHttpMethod(value_str));
    } else if (name_str == ":path") {
        auto pos = value_str.find('?');
        if (pos != std::string::npos) {
            ctx.request->setPath(value_str.substr(0, pos));
            ctx.request->setQuery(value_str.substr(pos + 1));
        } else {
            ctx.request->setPath(value_str);
        }
    } else if (name_str == ":scheme") {
        ctx.request->setScheme(value_str);
    } else if (name_str == ":authority") {
        ctx.request->setAuthority(value_str);
        ctx.request->setHeader("host", value_str);
    } else if (name_str[0] != ':') {
        ctx.request->setHeader(name_str, value_str);
    }
    return 0;
}

int H2Session::on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data)
{
    auto* h2 = static_cast<H2Session*>(user_data);

    switch (frame->hd.type) {
    case NGHTTP2_HEADERS: {
        if (frame->headers.cat != NGHTTP2_HCAT_REQUEST)
            break;

        bool end_stream = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;

        // Mark state under lock
        {
            std::shared_lock lock(h2->stream_mutex_);
            auto it = h2->streams_.find(frame->hd.stream_id);
            if (it != h2->streams_.end()) {
                it->second.headers_done = true;
                if (end_stream) {
                    it->second.body_done = true;
                }
            }
        }

        // Spawn stream fiber
        auto self = h2->shared_from_this();
        int32_t sid = frame->hd.stream_id;
        h2->worker_->schedule([self, sid]() {
            self->processStream(sid);
        });
        break;
    }
    case NGHTTP2_DATA: {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            std::shared_lock lock(h2->stream_mutex_);
            auto it = h2->streams_.find(frame->hd.stream_id);
            if (it != h2->streams_.end()) {
                it->second.body_done = true;
            }
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int H2Session::on_data_chunk_recv(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data)
{
    auto* h2 = static_cast<H2Session*>(user_data);
    std::shared_lock lock(h2->stream_mutex_);
    auto it = h2->streams_.find(stream_id);
    if (it != h2->streams_.end()) {
        it->second.body.append((const char*)data, len);
    }
    return 0;
}

int H2Session::on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data)
{
    auto* h2 = static_cast<H2Session*>(user_data);
    if (error_code != 0) {
        WARN(logger) << "H2 stream " << stream_id << " closed with error: " << nghttp2_http2_strerror(error_code);
    }
    std::unique_lock lock(h2->stream_mutex_);
    auto it = h2->stream_states_.find(stream_id);
    if (it != h2->stream_states_.end()) {
        it->second->closed = true;
        h2->stream_states_.erase(it);
    }
    h2->streams_.erase(stream_id);
    return 0;
}

} // namespace chen::http
