#include "FdManager.hpp"

#include <mutex>

#include <fcntl.h>
#include <sys/stat.h>

#include "Hook.hpp"

namespace chen {

FdCtx::FdCtx(int fd)
    : is_init_(false)
    , is_socket_(false)
    , sys_nonblock_(false)
    , user_nonblock_(false)
    , is_closed_(false)
    , fd_(fd)
    , recv_timout_(-1)
    , send_timeout_(-1)
{
    init();
}

FdCtx::~FdCtx()
{
}

bool FdCtx::init()
{
    if (is_init_) {
        return true;
    }
    // 赋值为无穷大
    recv_timout_ = -1;
    send_timeout_ = -1;

    struct stat fd_stat;
    if (-1 == fstat(fd_, &fd_stat)) {
        is_init_ = false;
        is_socket_ = false;
    } else {
        is_init_ = true;
        is_socket_ = S_ISSOCK(fd_stat.st_mode);
    }
    if (is_socket_) {
        int flags = fcntl_f(fd_, F_GETFL, 0);
        if (!(flags & O_NONBLOCK)) {
            fcntl_f(fd_, F_SETFL, flags | O_NONBLOCK);
        }
        sys_nonblock_ = true;
    } else {
        sys_nonblock_ = false;
    }
    user_nonblock_ = false;
    is_closed_ = false;
    return is_init_;
}

void FdCtx::setTimeout(int type, uint64_t v)
{
    if (type == SO_RCVTIMEO) {
        recv_timout_ = v;
    } else {
        send_timeout_ = v;
    }
}

uint64_t FdCtx::getTimeout(int type)
{
    if (type == SO_RCVTIMEO) {
        return recv_timout_;
    } else {
        return send_timeout_;
    }
}

FdManager::FdManager()
{
    datas_.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create)
{
    if (fd == -1) {
        return nullptr;
    }
    std::shared_lock lock(mutex_);
    if ((int)datas_.size() <= fd) {
        if (auto_create == false) {
            return nullptr;
        }
    } else {
        if (datas_[fd] || !auto_create) {
            return datas_[fd];
        }
    }
    lock.unlock();
    // fd不存在，并且要求创建
    std::unique_lock lock2(mutex_);
    FdCtx::ptr ctx(new FdCtx(fd));
    if (fd >= (int)datas_.size()) {
        datas_.resize(fd * 1.5);
    }
    datas_[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd)
{
    std::unique_lock lock(mutex_);
    if ((int)datas_.size() <= fd) {
        return;
    }
    // 标记旧 FdCtx 为已关闭：持有该 FdCtx 的 do_io 在唤醒后可据此感知
    // fd 已被关闭/复用，避免在复用后的 fd 上再次 recv / addEvent。
    if (datas_[fd]) {
        datas_[fd]->setClose(true);
    }
    datas_[fd].reset();
}

} // namespace chen
