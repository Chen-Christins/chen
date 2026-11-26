/**
 * @file H2Session.hpp
 * @brief HTTP/2 会话管理
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include <nghttp2/nghttp2.h>

#include "Http.hpp"
#include "Servlet.hpp"

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

    /**
     * @brief 请求会话进入 draining：发送 GOAWAY，等在途 stream 结束后退出
     * @details 线程安全：只置标志 + 投递命令，由连接协程在 run() 里实际发送；
     *          期间 recv 超时会被缩短，保证空闲会话也能及时感知。
     */
    void startDrain();

    /**
     * @brief 是否处于 draining
     */
    bool isDraining() const { return draining_; }

    Socket::ptr getSocket() const { return socket_; }
    ServletDispatch::ptr getDispatch() const { return dispatch_; }
    IOManager* getWorker() const { return worker_; }

    // Command posting from stream fibers (thread-safe)
    struct Command {
        enum Type {
            RESPONSE,
            HEADERS,
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

    /// 某条流已入队但尚未发出的字节数（背压水位）
    size_t getStreamQueuedBytes(int32_t stream_id);
    /// 流是否已关闭（对端 RST 或发送完毕）
    bool isStreamClosed(int32_t stream_id);
    /// 会话是否已停止
    bool isStopping() const { return stopping_; }

private:
    // nghttp2 callbacks
    static int on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);

    static int on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data);

    static int on_header(nghttp2_session* session,
        const nghttp2_frame* frame,
        nghttp2_rcbuf* name,
        nghttp2_rcbuf* value,
        uint8_t flags,
        void* user_data);

    static int on_data_chunk_recv(nghttp2_session* session,
        uint8_t flags,
        int32_t stream_id,
        const uint8_t* data,
        size_t len,
        void* user_data);

    static int on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data);

    // Internal
    void processCommands();
    void doSend();
    void processStream(int32_t stream_id);

    /// 流式响应的待发送状态（只在连接协程里改，生产者只读水位/关闭标志）
    struct StreamState {
        std::deque<std::string> chunks;
        size_t queued_bytes = 0;
        bool eof = false;
        bool closed = false;
    };

    /// 取/建某个 stream 的流式状态（调用方需持有 stream_mutex_）
    std::shared_ptr<StreamState> getOrCreateStreamState(int32_t stream_id);

    /// 提交响应头 + data provider（不带 END_STREAM），后续数据由 sendData() 驱动
    void submitResponseHeaders(int32_t stream_id, HttpResponse::ptr rsp);
    /// 处理 Command::DATA
    void handleStreamData(const Command& cmd);

    /// DATA provider 的读回调（连接协程内调用）
    static ssize_t onStreamDataRead(nghttp2_session* session,
        int32_t stream_id,
        uint8_t* buf,
        size_t length,
        uint32_t* data_flags,
        nghttp2_data_source* source,
        void* user_data);

    struct StreamCtx {
        HttpRequest::ptr request;
        std::string body;
        bool headers_done = false;
        bool body_done = false;
    };

    Socket::ptr socket_;
    IOManager* worker_;
    ServletDispatch::ptr dispatch_;
    nghttp2_session* session_ = nullptr;

    std::unordered_map<int32_t, StreamCtx> streams_;
    /// 流式响应状态；与 streams_ 共用 stream_mutex_
    std::unordered_map<int32_t, std::shared_ptr<StreamState>> stream_states_;
    std::shared_mutex stream_mutex_;

    std::queue<Command> commands_;
    std::mutex cmd_mutex_;

    std::vector<uint8_t> send_buffer_;
    std::vector<std::shared_ptr<void>> pending_bodies_;
    std::atomic<bool> stopping_{false};
    /// 是否已请求 draining（GOAWAY 已投递）
    std::atomic<bool> draining_{false};
};

} // namespace chen::http
