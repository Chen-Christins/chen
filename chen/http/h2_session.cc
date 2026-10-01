#include "h2_session.h"

#include <cstring>

#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "h2_stream.h"
#include "h2_util.h"

namespace chen::http {

static Logger::ptr logger = LOG_NAME("system");

H2Session::H2Session(Socket::ptr sock, IOManager* worker, ServletDispatch::ptr dispatch)
    : m_socket(sock)
    , m_worker(worker)
    , m_dispatch(dispatch) {
}

H2Session::~H2Session() {
    close();
}

bool H2Session::init() {
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
    nghttp2_session_callbacks_set_error_callback2(callbacks,
        [](nghttp2_session* session, int lib_error_code, const char* msg,
           size_t len, void* user_data) -> int {
            ERROR(logger) << "nghttp2 error: " << std::string(msg, len)
                         << " (code=" << lib_error_code << ")";
            return 0;
        });

    rv = nghttp2_session_server_new(&m_session, callbacks, this);
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
    rv = nghttp2_submit_settings(m_session, NGHTTP2_FLAG_NONE, iv,
                                  sizeof(iv) / sizeof(iv[0]));
    if (rv != 0) {
        ERROR(logger) << "nghttp2_submit_settings failed: " << nghttp2_strerror(rv);
        return false;
    }

    return true;
}

void H2Session::run() {
    std::vector<uint8_t> buf(65536);
    bool has_active_streams = false;

    while (!m_stopping) {
        processCommands();

        bool need_read = nghttp2_session_want_read(m_session) != 0;

        if (need_read) {
            // Short timeout when streams are active so responses get flushed promptly
            m_socket->setRecvTimeout(has_active_streams ? 250 : UINT64_MAX);

            int len = m_socket->recv(buf.data(), buf.size());
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
                ssize_t readlen = nghttp2_session_mem_recv(m_session, buf.data(), len);
                if (readlen < 0) {
                    WARN(logger) << "nghttp2_session_mem_recv error: "
                                 << nghttp2_strerror((int)readlen);
                    break;
                }
            }
        }

        processCommands();

        if (nghttp2_session_want_write(m_session)) {
            doSend();
            m_pendingBodies.clear();
        }

        {
            std::shared_lock lock(m_streamMutex);
            has_active_streams = !m_streams.empty();
        }

        if (need_read == 0 && nghttp2_session_want_write(m_session) == 0) {
            if (!has_active_streams) {
                DEBUG(logger) << "H2 session idle, closing";
                break;
            }
        }
    }

    // Graceful shutdown
    if (!m_stopping) {
        nghttp2_submit_goaway(m_session, NGHTTP2_FLAG_NONE,
                              nghttp2_session_get_last_proc_stream_id(m_session),
                              0, nullptr, 0);
        doSend();
    }
}

void H2Session::close() {
    m_stopping = true;
    if (m_session) {
        nghttp2_session_del(m_session);
        m_session = nullptr;
    }
}

void H2Session::postCommand(Command cmd) {
    {
        std::lock_guard lock(m_cmdMutex);
        m_commands.push(std::move(cmd));
    }
}

int H2Session::sendResponse(int32_t stream_id, HttpResponse::ptr rsp) {
    std::list<std::string> temp_strs;
    auto nva = build_response_nva((int)rsp->getStatus(), rsp->getHeaders(),
                                   rsp->getCookies(), temp_strs);

    if (rsp->getBody().empty()) {
        int rv = nghttp2_submit_response(m_session, stream_id, nva.data(), nva.size(), nullptr);
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
    data_prd.read_callback = [](nghttp2_session* session, int32_t sid,
                                 uint8_t* out, size_t length, uint32_t* data_flags,
                                 nghttp2_data_source* source, void* user_data) -> ssize_t {
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
    m_pendingBodies.push_back(body_ctx);

    int rv = nghttp2_submit_response(m_session, stream_id, nva.data(), nva.size(), &data_prd);
    if (rv != 0) {
        ERROR(logger) << "nghttp2_submit_response with body failed: " << nghttp2_strerror(rv);
    }
    return rv;
}

void H2Session::processCommands() {
    std::queue<Command> cmds;
    {
        std::lock_guard lock(m_cmdMutex);
        cmds.swap(m_commands);
    }

    while (!cmds.empty()) {
        auto& cmd = cmds.front();
        switch (cmd.type) {
        case Command::RESPONSE:
            sendResponse(cmd.stream_id, cmd.response);
            break;
        case Command::DATA:
            // TODO: streaming data via nghttp2_submit_data
            break;
        case Command::GOAWAY:
            nghttp2_submit_goaway(m_session, NGHTTP2_FLAG_NONE,
                                  nghttp2_session_get_last_proc_stream_id(m_session),
                                  0, nullptr, 0);
            break;
        }
        cmds.pop();
    }
}

void H2Session::doSend() {
    for (;;) {
        const uint8_t* data = nullptr;
        ssize_t rv = nghttp2_session_mem_send(m_session, &data);
        if (rv < 0) {
            ERROR(logger) << "nghttp2_session_mem_send error: "
                         << nghttp2_strerror((int)rv);
            break;
        }
        if (rv == 0) {
            break;
        }
        int written = m_socket->send(data, rv);
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

void H2Session::processStream(int32_t stream_id) {
    StreamCtx* ctx_ptr;
    {
        std::shared_lock lock(m_streamMutex);
        auto it = m_streams.find(stream_id);
        if (it == m_streams.end()) return;
        ctx_ptr = &it->second;
    }

    auto& ctx = *ctx_ptr;
    auto req = ctx.request;

    if (!ctx.body.empty()) {
        req->setBody(ctx.body);
    }
    req->init();

    auto rsp = std::make_shared<HttpResponse>(HttpRequest::HTTP2_VERSION, false);
    auto stream = std::make_shared<H2Stream>(shared_from_this(), stream_id);

    if (m_dispatch) {
        m_dispatch->handle(req, rsp, stream);
    }

    if (rsp->isStreaming()) {
        return;
    }

    Command cmd;
    cmd.type = Command::RESPONSE;
    cmd.stream_id = stream_id;
    cmd.response = rsp;
    postCommand(cmd);
}

int H2Session::on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame,
                                 void* user_data) {
    auto* h2 = static_cast<H2Session*>(user_data);
    if (frame->hd.type != NGHTTP2_HEADERS) return 0;
    if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        std::unique_lock lock(h2->m_streamMutex);
        auto& ctx = h2->m_streams[frame->hd.stream_id];
        ctx.request = std::make_shared<HttpRequest>(HttpRequest::HTTP2_VERSION, false);
        ctx.request->setStreamId(frame->hd.stream_id);
        ctx.headers_done = false;
        ctx.body_done = false;
        ctx.body.clear();
    }
    return 0;
}

int H2Session::on_header(nghttp2_session* session, const nghttp2_frame* frame,
                          nghttp2_rcbuf* name, nghttp2_rcbuf* value,
                          uint8_t flags, void* user_data) {
    auto* h2 = static_cast<H2Session*>(user_data);
    std::shared_lock lock(h2->m_streamMutex);
    auto it = h2->m_streams.find(frame->hd.stream_id);
    if (it == h2->m_streams.end()) return 0;

    auto& ctx = it->second;
    if (!ctx.request) return 0;

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

int H2Session::on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame,
                              void* user_data) {
    auto* h2 = static_cast<H2Session*>(user_data);

    switch (frame->hd.type) {
    case NGHTTP2_HEADERS: {
        if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) break;

        bool end_stream = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;

        // Mark state under lock
        {
            std::shared_lock lock(h2->m_streamMutex);
            auto it = h2->m_streams.find(frame->hd.stream_id);
            if (it != h2->m_streams.end()) {
                it->second.headers_done = true;
                if (end_stream) {
                    it->second.body_done = true;
                }
            }
        }

        // Spawn stream fiber
        auto self = h2->shared_from_this();
        int32_t sid = frame->hd.stream_id;
        h2->m_worker->schedule([self, sid]() {
            self->processStream(sid);
        });
        break;
    }
    case NGHTTP2_DATA: {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            std::shared_lock lock(h2->m_streamMutex);
            auto it = h2->m_streams.find(frame->hd.stream_id);
            if (it != h2->m_streams.end()) {
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

int H2Session::on_data_chunk_recv(nghttp2_session* session, uint8_t flags,
                                   int32_t stream_id, const uint8_t* data,
                                   size_t len, void* user_data) {
    auto* h2 = static_cast<H2Session*>(user_data);
    std::shared_lock lock(h2->m_streamMutex);
    auto it = h2->m_streams.find(stream_id);
    if (it != h2->m_streams.end()) {
        it->second.body.append((const char*)data, len);
    }
    return 0;
}

int H2Session::on_stream_close(nghttp2_session* session, int32_t stream_id,
                                uint32_t error_code, void* user_data) {
    auto* h2 = static_cast<H2Session*>(user_data);
    if (error_code != 0) {
        WARN(logger) << "H2 stream " << stream_id << " closed with error: "
                     << nghttp2_http2_strerror(error_code);
    }
    std::unique_lock lock(h2->m_streamMutex);
    h2->m_streams.erase(stream_id);
    return 0;
}

} // namespace chen::http
