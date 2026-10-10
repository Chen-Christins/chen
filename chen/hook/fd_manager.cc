#include "fd_manager.h"

#include <mutex>

#include <sys/stat.h>

#include "hook.h"

namespace chen {

FdCtx::FdCtx(int fd)
    : m_isInit(false)
    , m_isSocket(false)
    , m_sysNonblock(false)
    , m_userNonblock(false)
    , m_isClosed(false)
    , m_fd(fd)   
    , m_recvTimout(-1)
    , m_sendTimeout(-1) {
    init();
}

FdCtx::~FdCtx() {
}

bool FdCtx::init() {
    if (m_isInit) {
        return true;
    }
    // 赋值为无穷大
    m_recvTimout = -1;
    m_sendTimeout = -1;

    struct stat fd_stat;
    if (-1 == fstat(m_fd, &fd_stat)) {
        m_isInit = false;
        m_isSocket = false;
    } else {
        m_isInit = true;
        m_isSocket = S_ISSOCK(fd_stat.st_mode);
    }
    if (m_isSocket) {
        int flags = fcntl_f(m_fd, F_GETFL, 0);
        if (!(flags & O_NONBLOCK)) {
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
        }
        m_sysNonblock = true;
    } else {
        m_sysNonblock = false;
    }
    m_userNonblock = false;
    m_isClosed = false;
    return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) {
    if (type == SO_RCVTIMEO) {
        m_recvTimout = v;
    } else {
        m_sendTimeout = v;
    }
}

uint64_t FdCtx::getTimeout(int type) {
    if (type == SO_RCVTIMEO) {
        return m_recvTimout;
    } else {
        return m_sendTimeout;
    }
}

FdManager::FdManager() {
    m_datas.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create) {
    if (fd == -1) {
        return nullptr;
    }
    std::shared_lock lock(m_mutex);
    if ((int)m_datas.size() <= fd) {
        if (auto_create == false) {
            return nullptr;
        }
    } else {
        if (m_datas[fd] || !auto_create) {
            return m_datas[fd];
        }
    }
    lock.unlock();
    // fd不存在，并且要求创建
    std::unique_lock lock2(m_mutex);
    FdCtx::ptr ctx(new FdCtx(fd));
    if (fd >= (int)m_datas.size()) {
        m_datas.resize(fd * 1.5);
    }
    m_datas[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd) {
    std::unique_lock lock(m_mutex);
    if ((int)m_datas.size() <= fd) {
        return ;
    }
    // 标记旧 FdCtx 为已关闭：持有该 FdCtx 的 do_io 在唤醒后可据此感知
    // fd 已被关闭/复用，避免在复用后的 fd 上再次 recv / addEvent。
    if (m_datas[fd]) {
        m_datas[fd]->setClose(true);
    }
    m_datas[fd].reset();
}

}
