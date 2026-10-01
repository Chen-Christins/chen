#include "socket_stream.h"

#include "../log/log.h"
#include "../util/util.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/// 读取固定长度到内存
int Stream::readFixSize(void* buffer, size_t length) {
    // 偏移量
    size_t offset = 0;
    // 剩余字节数
    int64_t left = length;
    while (left > 0) {
        // 读取
        int64_t len = read((char*)buffer + offset, left);
        // 如果失败
        if (len <= 0) {
            return len;
        }
        // 更新偏移量
        offset += len;
        // 更新剩余的字节数
        left -= len;
    }
    return length;
}

int Stream::readFixSize(ByteArray::ptr ba, size_t length) {
    int64_t left = length;
    while (left > 0) {
        int64_t len = read(ba, left);
        if (len <= 0) {
            return len;
        }
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(const void* buffer, size_t length) {
    size_t offset = 0;
    int64_t left = length;
    while (left > 0) {
        int64_t len = write((const char*)buffer + offset, left);
        if (len <= 0) {
            return len;
        }
        offset += len;
        left -= len;
    }
    return length;
}

int Stream::writeFixSize(ByteArray::ptr ba, size_t length) {
    int64_t left = length;
    while (left > 0) {
        int64_t len = write(ba, left);
        if (len <= 0) {
            return len;
        }
        left -= len;
    }
    return length;
}

SocketStream::SocketStream(Socket::ptr sock, bool owner) 
    : m_socket(sock)
    , m_owner(owner) {
}

SocketStream::~SocketStream() {
    if (m_owner && m_socket) {
        m_socket->close();
    }
}

int SocketStream::read(void* buffer, size_t length) {
    if (!isConnected()) {
        return -1;
    }
    return m_socket->recv(buffer, length);
}

int SocketStream::read(ByteArray::ptr ba, size_t length) {
    if (!isConnected()) {
        return -1;
    }

    std::vector<iovec> iovs;
    // 获取可写内存，将数据写入bytearray
    ba->getWriteBuffers(iovs, length);
    // 读取数据，放到第一个元素中
    int rt = m_socket->recv(&iovs[0], iovs.size());
    if (rt > 0) {
        // 写入成功，将bytearray中的操作位置给设置到读完之后的位置
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

int SocketStream::write(const void* buffer, size_t length) {
    if (!isConnected()) {
        return -1;
    }
    return m_socket->send(buffer, length);
}

int SocketStream::write(ByteArray::ptr ba, size_t length) {
    if (!isConnected()) {
        return -1;
    }

    std::vector<iovec> iovs;
    // 获取可写内存，将数据写入bytearray
    ba->getReadBuffers(iovs, length);
    // 读取数据，放到第一个元素中
    int rt = m_socket->send(&iovs[0], iovs.size());
    if (rt > 0) {
        // 写入成功，将bytearray中的操作位置给设置到读完之后的位置
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

void SocketStream::close() {
    if (m_socket) {
        m_socket->close();
    }
}

bool SocketStream::isConnected() const {
    return m_socket && m_socket->isConnected();
}

Address::ptr SocketStream::getRemoteAddress() {
    if (m_socket) {
        return m_socket->getRemoteAddress();
    }
    return nullptr;
}

Address::ptr SocketStream::getLocalAddress() {
    if (m_socket) {
        return m_socket->getLocalAddress();
    }
    return nullptr;
}

std::string SocketStream::getRemoteAddressString() {
    auto addr = getRemoteAddress();
    if (addr) {
        return addr->toString();
    }
    return "";
}

std::string SocketStream::getLocalAddressString() {
    auto addr = getLocalAddress();
    if (addr) {
        return addr->toString();
    }
    return "";
}

AsyncSocketStream::Ctx::Ctx() 
    : sn(0)
    , timeout(0)
    , result(0)
    , timed(false)
    , scheduler(nullptr) {
}

AsyncSocketStream::AsyncSocketStream(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner)
    , m_waitSem(2)
    , m_sn(0)
    , m_autoConnect(false)
    , m_iomanager(nullptr)
    , m_worker(nullptr) {
}

bool AsyncSocketStream::start() {
    if (!m_iomanager) {
        m_iomanager = IOManager::GetThis();
    }
    if (!m_worker) {
        m_worker = IOManager::GetThis();
    }

    do {
        waitFiber();

        if (m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }

        if (!isConnected()) {
            if (!m_socket->reconnect()) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        if (m_connectCb) {
            if (!m_connectCb(shared_from_this())) {
                innerClose();
                m_waitSem.notify();
                m_waitSem.notify();
                break;
            }
        }

        startRead();
        startWrite();
        return true;
    } while (0);

    if (m_autoConnect) {
        if (m_timer) {
            m_timer->cancel();
            m_timer = nullptr;
        }
        // 这个地方传入shared_from_this主要是延长对象的生命周期，防止this悬空指针导致运行错误问题，这个异步操作，延长生命周期
        m_timer = m_iomanager->addTimer(2 * 1000, std::bind(&AsyncSocketStream::start, shared_from_this()));
    }

    return false;
}

void AsyncSocketStream::close() {
    m_autoConnect = false;
    SchedulerSwitcher ss(m_iomanager);
    if (m_timer) {
        m_timer->cancel();
    }
    SocketStream::close();
}

void AsyncSocketStream::Ctx::doRsp() {
    Scheduler* sc = scheduler;
    if (!Atomic::compareAndSwapBool(scheduler, sc, (Scheduler*)nullptr)) {
        return;
    }
    if (!sc || !fiber) {
        return;
    }
    if (timer) {
        timer->cancel();
        timer = nullptr;
    }

    if (timed) {
        result = TIMEOUT;
    }
    sc->schedule(&fiber);
}

void AsyncSocketStream::doRead() {
    try {
        while (isConnected()) {
            auto ctx = doRecv();
            if (ctx) {
                ctx->doRsp();
            }
        }
    } catch (...) {
        WARN(logger) << "Async socket doRead Error";
    }
    DEBUG(logger) << "doRead out " << this;
    innerClose();
    m_waitSem.notify();

    if (m_autoConnect) {
        m_iomanager->addTimer(10, std::bind(&AsyncSocketStream::start, shared_from_this()));
    }
}

void AsyncSocketStream::doWrite() {
    try {
        while (isConnected()) {
            m_sem.wait();
            std::list<SendCtx::ptr> ctxs;
            {
                std::unique_lock lock(m_queueMtx);
                m_queue.swap(ctxs);
            }
            auto self = shared_from_this();
            for (auto& i : ctxs) {
                if (!i->doSend(self)) {
                    innerClose();
                    break;
                }
            }
        }
    } catch (...) {
        WARN(logger) << "Async socket doWrite Error";
    }
    DEBUG(logger) << "doWrite out " << this;
    {
        std::unique_lock lock(m_queueMtx);
        m_queue.clear();
    }
    m_waitSem.notify();
}

void AsyncSocketStream::startRead() {
    m_iomanager->schedule(std::bind(&AsyncSocketStream::doRead, shared_from_this()));
}

void AsyncSocketStream::startWrite() {
    m_iomanager->schedule(std::bind(&AsyncSocketStream::doWrite, shared_from_this()));
}

void AsyncSocketStream::onTimeOut(Ctx::ptr ctx) {
    {
        std::unique_lock lock(m_mtx);
        m_ctxs.erase(ctx->sn);
    }
    ctx->timed = true;
    ctx->doRsp();
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getCtx(uint32_t sn) {
    std::shared_lock lock(m_mtx);
    auto it = m_ctxs.find(sn);
    return it == m_ctxs.end() ? nullptr : it->second;
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getAndDelCtx(uint32_t sn) {
    Ctx::ptr ctx;
    std::unique_lock lock(m_mtx);
    auto it = m_ctxs.find(sn);
    if (it != m_ctxs.end()) {
        ctx = it->second;
        m_ctxs.erase(sn);
    }
    return ctx;
}

bool AsyncSocketStream::addCtx(Ctx::ptr ctx) {
    std::unique_lock lock(m_mtx);
    m_ctxs.emplace(ctx->sn, ctx);
    return true;
}

bool AsyncSocketStream::enqueue(SendCtx::ptr ctx) {
    ASSERT(ctx);
    std::unique_lock lock(m_queueMtx);
    bool empty = m_queue.empty();
    m_queue.emplace_back(ctx);
    lock.unlock();
    if (empty) {
        m_sem.notify();
    }
    return empty;
}

bool AsyncSocketStream::innerClose() {
    ASSERT(m_iomanager == IOManager::GetThis());
    if (isConnected() && m_disconnectCb) {
        m_disconnectCb(shared_from_this());
    }
    SocketStream::close();
    m_sem.notify();
    std::unordered_map<uint32_t, Ctx::ptr> ctxs;
    {
        std::unique_lock lock(m_mtx);
        ctxs.swap(m_ctxs);
    }
    {
        std::unique_lock lock(m_queueMtx);
        m_queue.clear();
    }
    for (auto& [id, ctx] : ctxs) {
        ctx->result = IO_ERROR;
        ctx->doRsp();
    }
    return true;
}

bool AsyncSocketStream::waitFiber() {
    m_waitSem.wait();
    m_waitSem.wait();
    return true;
}

AsyncSocketStreamManager::AsyncSocketStreamManager()
    : m_idx(0)
    , m_size(0) {
}

void AsyncSocketStreamManager::add(AsyncSocketStream::ptr stream) {
    std::unique_lock lock(m_mtx);
    m_datas.emplace_back(stream);
    ++m_size;

    if (m_connectCb) {
        stream->setConnectCb(m_connectCb);
    }
    
    if (m_disconnectCb) {
        stream->setDisConnectCb(m_disconnectCb);
    }
}

void AsyncSocketStreamManager::clear() {
    std::unique_lock lock(m_mtx);
    for (auto& i : m_datas) {
        i->close();
    }
    m_datas.clear();
    m_size = 0;
}

void AsyncSocketStreamManager::setConnection(const std::vector<AsyncSocketStream::ptr>& streams) {
    auto cs = streams;
    std::unique_lock lock(m_mtx);
    cs.swap(m_datas);
    m_size = m_datas.size();
    if (m_connectCb || m_disconnectCb) {
        for (auto& i : m_datas) {
            if (m_connectCb) {
                i->setConnectCb(m_connectCb);
            }
            if (m_disconnectCb) {
                i->setDisConnectCb(m_disconnectCb);
            }
        }
    }
    lock.unlock();

    for (auto& i : cs) {
        i->close();
    }
}

AsyncSocketStream::ptr AsyncSocketStreamManager::get() {
    std::shared_lock lock(m_mtx);
    for (uint32_t i = 0; i < m_size; ++i) {
        auto idx = Atomic::addFetch(m_idx, 1);
        if (m_datas[idx % m_size]->isConnected()) {
            return m_datas[idx % m_size];
        }
    }
    return nullptr;
}

void AsyncSocketStreamManager::setConnectCb(connect_callback v) {
    m_connectCb = v;
    std::unique_lock lock(m_mtx);
    for (auto& i : m_datas) {
        i->setConnectCb(m_connectCb);
    }
}

void AsyncSocketStreamManager::setDisConnectCb(disconnect_callback v) {
    m_disconnectCb = v;
    std::unique_lock lock(m_mtx);
    for (auto& i : m_datas) {
        i->setDisConnectCb(m_disconnectCb);
    }
}

} // namespace chen
