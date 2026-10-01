/**
 * @file h2_session.h
 * @brief HTTP/2 会话管理
 */
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <nghttp2/nghttp2.h>

#include "http.h"
#include "servlet.h"

namespace chen::http {

class H2Stream;

class H2Session : public std::enable_shared_from_this<H2Session> {
public:
    typedef std::shared_ptr<H2Session> ptr;

    H2Session(Socket::ptr sock, IOManager* worker, ServletDispatch::ptr dispatch);
    ~H2Session();

    bool init();
    void run();
    void close();

    Socket::ptr getSocket() const { return m_socket; }
    ServletDispatch::ptr getDispatch() const { return m_dispatch; }
    IOManager* getWorker() const { return m_worker; }

    // Command posting from stream fibers (thread-safe)
    struct Command {
        enum Type {
            RESPONSE,
            DATA,
            GOAWAY
        };
        Type type;
        int32_t stream_id;
        HttpResponse::ptr response;
        std::string data;
        bool end_stream = false;
    };
    void postCommand(Command cmd);

    // Called by H2Stream
    int sendResponse(int32_t stream_id, HttpResponse::ptr rsp);

private:
    // nghttp2 callbacks
    static int on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);

    static int on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);
    
    static int on_header(nghttp2_session* session, const nghttp2_frame* frame, nghttp2_rcbuf* name, nghttp2_rcbuf* value,
                         uint8_t flags, void* user_data);
    
    static int on_data_chunk_recv(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len,
                                  void* user_data);
    
    static int on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data);

    // Internal
    void processCommands();
    void doSend();
    void processStream(int32_t stream_id);

    struct StreamCtx {
        HttpRequest::ptr request;
        std::string body;
        bool headers_done = false;
        bool body_done = false;
    };

    Socket::ptr m_socket;
    IOManager* m_worker;
    ServletDispatch::ptr m_dispatch;
    nghttp2_session* m_session = nullptr;

    std::unordered_map<int32_t, StreamCtx> m_streams;
    std::shared_mutex m_streamMutex;

    std::queue<Command> m_commands;
    std::mutex m_cmdMutex;

    std::vector<uint8_t> m_sendBuffer;
    std::vector<std::shared_ptr<void>> m_pendingBodies;
    std::atomic<bool> m_stopping{false};
};

} // namespace chen::http
