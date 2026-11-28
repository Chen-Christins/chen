#include "SocketStream.hpp"

#include "../Log/Log.hpp"
#include "../Util/Util.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

Stream::~Stream()
{
}

/// 读取固定长度到内存
int Stream::readFixSize(void* buffer, size_t length)
{
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

int Stream::readFixSize(ByteArray::ptr ba, size_t length)
{
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

int Stream::writeFixSize(const void* buffer, size_t length)
{
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

int Stream::writeFixSize(ByteArray::ptr ba, size_t length)
{
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
    : socket_(sock)
    , owner_(owner)
{
}

SocketStream::~SocketStream()
{
    if (owner_ && socket_) {
        socket_->close();
    }
}

int SocketStream::read(void* buffer, size_t length)
{
    if (!isConnected()) {
        return -1;
    }
    return socket_->recv(buffer, length);
}

int SocketStream::read(ByteArray::ptr ba, size_t length)
{
    if (!isConnected()) {
        return -1;
    }

    std::vector<iovec> iovs;
    // 获取可写内存，将数据写入bytearray
    ba->getWriteBuffers(iovs, length);
    // 读取数据，放到第一个元素中
    int rt = socket_->recv(&iovs[0], iovs.size());
    if (rt > 0) {
        // 写入成功，将bytearray中的操作位置给设置到读完之后的位置
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

int SocketStream::write(const void* buffer, size_t length)
{
    if (!isConnected()) {
        return -1;
    }
    return socket_->send(buffer, length);
}

int SocketStream::write(ByteArray::ptr ba, size_t length)
{
    if (!isConnected()) {
        return -1;
    }

    std::vector<iovec> iovs;
    // 获取可写内存，将数据写入bytearray
    ba->getReadBuffers(iovs, length);
    // 读取数据，放到第一个元素中
    int rt = socket_->send(&iovs[0], iovs.size());
    if (rt > 0) {
        // 写入成功，将bytearray中的操作位置给设置到读完之后的位置
        ba->setPosition(ba->getPosition() + rt);
    }
    return rt;
}

void SocketStream::close()
{
    if (socket_) {
        socket_->close();
    }
}

bool SocketStream::isConnected() const
{
    return socket_ && socket_->isConnected();
}

Address::ptr SocketStream::getRemoteAddress()
{
    if (socket_) {
        return socket_->getRemoteAddress();
    }
    return nullptr;
}

Address::ptr SocketStream::getLocalAddress()
{
    if (socket_) {
        return socket_->getLocalAddress();
    }
    return nullptr;
}

std::string SocketStream::getRemoteAddressString()
{
    auto addr = getRemoteAddress();
    if (addr) {
        return addr->toString();
    }
    return "";
}

std::string SocketStream::getLocalAddressString()
{
    auto addr = getLocalAddress();
    if (addr) {
        return addr->toString();
    }
    return "";
}

AsyncSocketStream::SendCtx::~SendCtx()
{
}

AsyncSocketStream::Ctx::Ctx()
    : sn(0)
    , timeout(0)
    , result(0)
    , timed(false)
    , scheduler(nullptr)
{
}

AsyncSocketStream::AsyncSocketStream(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner)
    , wait_sem_(2)
    , sn_(0)
    , auto_connect_(false)
    , iomanager_(nullptr)
    , worker_(nullptr)
{
}

bool AsyncSocketStream::start()
{
    if (!iomanager_) {
        iomanager_ = IOManager::GetThis();
    }
    if (!worker_) {
        worker_ = IOManager::GetThis();
    }

    do {
        waitFiber();

        if (timer_) {
            timer_->cancel();
            timer_ = nullptr;
        }

        if (!isConnected()) {
            if (!socket_->reconnect()) {
                innerClose();
                wait_sem_.notify();
                wait_sem_.notify();
                break;
            }
        }

        if (connect_cb_) {
            if (!connect_cb_(shared_from_this())) {
                innerClose();
                wait_sem_.notify();
                wait_sem_.notify();
                break;
            }
        }

        startRead();
        startWrite();
        return true;
    } while (0);

    if (auto_connect_) {
        if (timer_) {
            timer_->cancel();
            timer_ = nullptr;
        }
        // 这个地方传入shared_from_this主要是延长对象的生命周期，防止this悬空指针导致运行错误问题，这个异步操作，延长生命周期
        timer_ = iomanager_->addTimer(2 * 1000, std::bind(&AsyncSocketStream::start, shared_from_this()));
    }

    return false;
}

void AsyncSocketStream::close()
{
    auto_connect_ = false;
    SchedulerSwitcher ss(iomanager_);
    if (timer_) {
        timer_->cancel();
    }
    SocketStream::close();
}

void AsyncSocketStream::Ctx::doRsp()
{
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

void AsyncSocketStream::doRead()
{
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
    wait_sem_.notify();

    if (auto_connect_) {
        iomanager_->addTimer(10, std::bind(&AsyncSocketStream::start, shared_from_this()));
    }
}

void AsyncSocketStream::doWrite()
{
    try {
        while (isConnected()) {
            sem_.wait();
            std::list<SendCtx::ptr> ctxs;
            {
                std::unique_lock lock(queue_mtx_);
                queue_.swap(ctxs);
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
        std::unique_lock lock(queue_mtx_);
        queue_.clear();
    }
    wait_sem_.notify();
}

void AsyncSocketStream::startRead()
{
    iomanager_->schedule(std::bind(&AsyncSocketStream::doRead, shared_from_this()));
}

void AsyncSocketStream::startWrite()
{
    iomanager_->schedule(std::bind(&AsyncSocketStream::doWrite, shared_from_this()));
}

void AsyncSocketStream::onTimeOut(Ctx::ptr ctx)
{
    {
        std::unique_lock lock(mtx_);
        ctxs_.erase(ctx->sn);
    }
    ctx->timed = true;
    ctx->doRsp();
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getCtx(uint32_t sn)
{
    std::shared_lock lock(mtx_);
    auto it = ctxs_.find(sn);
    return it == ctxs_.end() ? nullptr : it->second;
}

AsyncSocketStream::Ctx::ptr AsyncSocketStream::getAndDelCtx(uint32_t sn)
{
    Ctx::ptr ctx;
    std::unique_lock lock(mtx_);
    auto it = ctxs_.find(sn);
    if (it != ctxs_.end()) {
        ctx = it->second;
        ctxs_.erase(sn);
    }
    return ctx;
}

bool AsyncSocketStream::addCtx(Ctx::ptr ctx)
{
    std::unique_lock lock(mtx_);
    ctxs_.emplace(ctx->sn, ctx);
    return true;
}

bool AsyncSocketStream::enqueue(SendCtx::ptr ctx)
{
    ASSERT(ctx);
    std::unique_lock lock(queue_mtx_);
    bool empty = queue_.empty();
    queue_.emplace_back(ctx);
    lock.unlock();
    if (empty) {
        sem_.notify();
    }
    return empty;
}

bool AsyncSocketStream::innerClose()
{
    ASSERT(iomanager_ == IOManager::GetThis());
    if (isConnected() && disconnect_cb_) {
        disconnect_cb_(shared_from_this());
    }
    SocketStream::close();
    sem_.notify();
    std::unordered_map<uint32_t, Ctx::ptr> ctxs;
    {
        std::unique_lock lock(mtx_);
        ctxs.swap(ctxs_);
    }
    {
        std::unique_lock lock(queue_mtx_);
        queue_.clear();
    }
    for (auto& [id, ctx] : ctxs) {
        ctx->result = IO_ERROR;
        ctx->doRsp();
    }
    return true;
}

bool AsyncSocketStream::waitFiber()
{
    wait_sem_.wait();
    wait_sem_.wait();
    return true;
}

AsyncSocketStream::Ctx::~Ctx()
{
}

AsyncSocketStreamManager::~AsyncSocketStreamManager()
{
}

AsyncSocketStreamManager::AsyncSocketStreamManager()
    : idx_(0)
    , size_(0)
{
}

void AsyncSocketStreamManager::add(AsyncSocketStream::ptr stream)
{
    std::unique_lock lock(mtx_);
    datas_.emplace_back(stream);
    ++size_;

    if (connect_cb_) {
        stream->setConnectCb(connect_cb_);
    }

    if (disconnect_cb_) {
        stream->setDisConnectCb(disconnect_cb_);
    }
}

void AsyncSocketStreamManager::clear()
{
    std::unique_lock lock(mtx_);
    for (auto& i : datas_) {
        i->close();
    }
    datas_.clear();
    size_ = 0;
}

void AsyncSocketStreamManager::setConnection(const std::vector<AsyncSocketStream::ptr>& streams)
{
    auto cs = streams;
    std::unique_lock lock(mtx_);
    cs.swap(datas_);
    size_ = datas_.size();
    if (connect_cb_ || disconnect_cb_) {
        for (auto& i : datas_) {
            if (connect_cb_) {
                i->setConnectCb(connect_cb_);
            }
            if (disconnect_cb_) {
                i->setDisConnectCb(disconnect_cb_);
            }
        }
    }
    lock.unlock();

    for (auto& i : cs) {
        i->close();
    }
}

AsyncSocketStream::ptr AsyncSocketStreamManager::get()
{
    std::shared_lock lock(mtx_);
    for (uint32_t i = 0; i < size_; ++i) {
        auto idx = Atomic::addFetch(idx_, 1);
        if (datas_[idx % size_]->isConnected()) {
            return datas_[idx % size_];
        }
    }
    return nullptr;
}

void AsyncSocketStreamManager::setConnectCb(connect_callback v)
{
    connect_cb_ = v;
    std::unique_lock lock(mtx_);
    for (auto& i : datas_) {
        i->setConnectCb(connect_cb_);
    }
}

void AsyncSocketStreamManager::setDisConnectCb(disconnect_callback v)
{
    disconnect_cb_ = v;
    std::unique_lock lock(mtx_);
    for (auto& i : datas_) {
        i->setDisConnectCb(disconnect_cb_);
    }
}

} // namespace chen
