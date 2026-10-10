#include "hook.h"

#include <cstdarg>
#include <dlfcn.h>

#include "../config/config.h"
#include "../fiber/fiber.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"
#include "fd_manager.h"

static chen::Logger::ptr logger = LOG_NAME("system");

namespace chen {

static ConfigVar<int>::ptr g_tcp_connect_timeout = 
    Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep)        \
    XX(usleep)       \
    XX(nanosleep)    \
    XX(socket)       \
    XX(connect)      \
    XX(accept)       \
    XX(read)         \
    XX(readv)        \
    XX(recv)         \
    XX(recvfrom)     \
    XX(recvmsg)      \
    XX(write)        \
    XX(writev)       \
    XX(send)         \
    XX(sendto)       \
    XX(sendmsg)      \
    XX(close)        \
    XX(fcntl)        \
    XX(ioctl)        \
    XX(getsockopt)   \
    XX(setsockopt)

// main函数之前执行
void hook_init() {
    static bool is_inited = false;
    if (is_inited) {
        return ;
    }
// sleep_f = dlsym(RTLD_NEXT, sleep) 这里就是把 XX(name) 定义为 name_f，
// 而这个dlsym，就是在动态库中寻找出第一个叫做name的函数地址，所以，name_f 就代表源函数
// 而下面这个 HOOK_FUN(XX) 就是将 name_f 加入 HOOK_FUN() 中，变成HOOK_FUN(name_f)
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX

}

static uint64_t s_connect_timeout = -1;
struct _HookIniter {
    _HookIniter() {
        hook_init();

        s_connect_timeout = g_tcp_connect_timeout->getValue();
        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value) {
            INFO(logger) << "tcp connect timeout changed from:"
                << old_value << " new value:" << new_value;
            s_connect_timeout = new_value;
        });
    }
};

static _HookIniter s_hook_initer;

bool is_hook_enable() {
    return t_hook_enable;
}

void set_hook_enable(bool flag) {
    t_hook_enable = flag;
}

}

// 条件定时器的条件
struct timer_info {
    int cancelled = 0;
};

/**
 * @brief 这个是用来处理io操作的通用函数模板
 * @details 如果不是socket系列的fd，hook就不生效
 *          如果不开启socket也不生效，fd相关信息找不到也不生效
 * @tparam OriginFun 源函数 例如 sleep_f...
 * @tparam Args 多个参数
 * @param fd 文件描述符
 * @param fun 源函数
 * @param hook_fun_name hook的函数名称
 * @param event 添加的io事件 [read/write]
 * @param timeout_so 超时时间类型，是读超时还是写超时
 * @param args 多个可变参数
 * @return ssize_t 目标类型
 */
template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name
        , uint32_t event, int timeout_so, Args&&... args) {
    if (!chen::t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);
    }
    chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
    // 如果fd的相关信息不存在
    if (!ctx) {
        return fun(fd, std::forward<Args>(args)...);
    }

    if (ctx->isClose()) {
        errno = EBADF; // 这个错误码 9 对应的错误为 EBADF，意思是“错误的文件描述符”
        return -1;
    }
    // 如果不是socket系列的 || 是用户主动设置的非阻塞
    if (!ctx->isSocket() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }
    // 获取当前类型的超时时间
    uint64_t to = ctx->getTimeout(timeout_so);
    // 设置超时条件
    std::shared_ptr<timer_info> tinfo(new timer_info);
retry:
    if (ctx->isClose()) {
        errno = EBADF;
        return -1;
    }
    // 这里如果有效，直接返回
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    // 中断状态, 就继续重试
    while (n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }
    // 阻塞状态，真的没有数据来
    if (n == -1 && errno == EAGAIN) {
        // 在 addEvent 之前再次检查 fd 是否已被其他线程 close
        // 避免在已关闭（或被复用）的 fd 上执行 epoll_ctl
        if (ctx->isClose()) {
            errno = EBADF;
            return -1;
        }

        // 获取当前的io调度器
        chen::IOManager* iom = chen::IOManager::GetThis();
        // 定时器
        chen::Timer::ptr timer;
        // tinfo的弱指针，可以判断tinfo是否已经销毁
        std::weak_ptr<timer_info> winfo(tinfo);
        // 这个地方是超时器，超时时间订好了
        if (to != (uint64_t)-1) {
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                auto t = winfo.lock();
                // 没有触发定时器，相当于已经失效了
                if (!t || t->cancelled) {
                    return ;
                }
                // 设置取消
                t->cancelled = ETIMEDOUT;
                // 到时间就强制取消
                iom->cancelEvent(fd, (chen::IOManager::Event)(event));
            }, winfo);
        }

        int rt = iom->addEvent(fd, (chen::IOManager::Event)(event));
        // 添加事件失败
        if (UNLIKELY(rt)) {
            ERROR(logger) << hook_fun_name << " addEvent(" 
                << fd << ", " << event << ")";
            // 记录 FdCtx 详细状态帮助诊断竞态／已关闭的问题
            chen::FdCtx::ptr info = chen::FdMgr::GetInstance()->get(fd);
            if (info) {
                ERROR(logger) << "FdCtx fd=" << fd
                    << " isInit=" << info->isInit()
                    << " isSocket=" << info->isSocket()
                    << " isClose=" << info->isClose()
                    << " userNonblock=" << info->getUserNonblock()
                    << " sysNonblock=" << info->getSysNonblock()
                    << " rto=" << info->getTimeout(SO_RCVTIMEO)
                    << " sto=" << info->getTimeout(SO_SNDTIMEO);
            } else {
                ERROR(logger) << "FdCtx missing for fd=" << fd;
            }
            ERROR(logger) << "addEvent errno=" << errno << " (" << strerror(errno) << ")";
            // 事件添加失败，就把添加的超时器取消
            if (timer) {
                timer->cancel();
            }
            return -1;
        } else {
            /*  如果事件添加成功，这里只有两种可能
             *  1）超时了，timer cancelEvent triggerEvent会唤醒回来
             *  2）addEvent数据回来了
             */
            chen::Fiber::YieldToHold();
            // 回来了还有定时器就取消，
            if (timer) {
                timer->cancel();
            }

            // 从定时任务唤醒，超时失败
            if (tinfo->cancelled) {
                errno = tinfo->cancelled;
                return -1;
            }
            // 数据来了，就去操作
            goto retry;
        }
    }
    return n;
}

extern "C" {

// 这里就是声明所有的 name_fun 类型的 name_f 的初始值为 nullptr, 
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

// 秒
unsigned int sleep(unsigned int seconds) {
    if (!chen::t_hook_enable) {
        return sleep_f(seconds);
    }
    // 获取当前的协程
    chen::Fiber::ptr fiber = chen::Fiber::GetThis();
    // 获取当前的调度器
    chen::IOManager* iom = chen::IOManager::GetThis();

    iom->addTimer(seconds * 1000, [iom, fiber]() {
        iom->schedule(fiber, -1);
    });
    
    chen::Fiber::YieldToHold();
    return 0;
}
// 微秒
int usleep(useconds_t usec) {
    if (!chen::t_hook_enable) {
        return usleep_f(usec);
    }
    
    chen::Fiber::ptr fiber = chen::Fiber::GetThis();
    chen::IOManager* iom = chen::IOManager::GetThis();
    iom->addTimer(usec / 1000, [iom, fiber]() {
        iom->schedule(fiber, -1);
    });
    chen::Fiber::YieldToHold();
    return 0;
}
// 纳秒
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!chen::t_hook_enable) {
        return nanosleep_f(req, rem);
    }

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
    chen::Fiber::ptr fiber = chen::Fiber::GetThis();
    chen::IOManager* iom = chen::IOManager::GetThis();
    iom->addTimer(timeout_ms, [iom, fiber]() {
        iom->schedule(fiber, -1);
    });
    chen::Fiber::YieldToHold();
    return 0;
}
// socket
int socket(int domain, int type, int protocol) {
    if (!chen::t_hook_enable) {
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    // 创建socket失败
    if (fd == -1) {
        return fd;
    }
    
    chen::FdMgr::GetInstance()->get(fd, true);
    return fd;
}
// connect
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    if (!chen::t_hook_enable) {
        return connect_f(fd, addr, addrlen);
    }
    // 先拿到关于这个文件描述符的相关信息
    chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
    // 如果不存在或者关闭了，那表示出错了
    if (!ctx || ctx->isClose()) {
        errno = EBADF;
        return -1;
    }
    // 这个connect就是做socket的相关文件描述符
    // 不是当然就直接做原来的操作
    if (!ctx->isSocket() || ctx->getUserNonblock()) {
        return connect_f(fd, addr, addrlen);
    }
    
    // 尝试连接
    int n = connect_f(fd, addr, addrlen);
    
    if (n == 0) {
        return 0;
    } else if (n != -1 || errno != EINPROGRESS) {
        return n;
    }

    chen::IOManager* iom = chen::IOManager::GetThis();
    chen::Timer::ptr timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    // 设置了超时时间
    if (timeout_ms != (uint64_t)-1) {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
            auto t = winfo.lock();
            if (!t || t->cancelled) {
                return ;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, chen::IOManager::WRITE);
        }, winfo);
    }

    int rt = iom->addEvent(fd, chen::IOManager::WRITE);
    if (rt == 0) {
        /*  如果事件添加成功，这里只有两种可能
         *  1）超时了，timer cancelEvent triggerEvent会唤醒回来
         *  2）addEvent数据回来了
         */
        chen::Fiber::YieldToHold();
        // 回来了还有定时器就取消，
        if (timer) {
            timer->cancel();
        }

        // 从定时任务唤醒，超时失败
        if (tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    } else {
        if (timer) {
            timer->cancel();
        }
        ERROR(logger) << "connect addEvent(" << fd << ", WRITE) error";
        chen::FdCtx::ptr info = chen::FdMgr::GetInstance()->get(fd);
        if (info) {
            ERROR(logger) << "FdCtx fd=" << fd
                << " isInit=" << info->isInit()
                << " isSocket=" << info->isSocket()
                << " isClose=" << info->isClose()
                << " userNonblock=" << info->getUserNonblock()
                << " sysNonblock=" << info->getSysNonblock()
                << " rto=" << info->getTimeout(SO_RCVTIMEO)
                << " sto=" << info->getTimeout(SO_SNDTIMEO);
        } else {
            ERROR(logger) << "FdCtx missing for fd=" << fd;
        }
        ERROR(logger) << "addEvent errno=" << errno << " (" << strerror(errno) << ")";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    // 获取套接字的错误状态
    if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
        return -1;
    }
    if (!error) {
        return 0;
    } else {
        errno = error;
        return -1;
    }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_with_timeout(sockfd, addr, addrlen, chen::s_connect_timeout);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = do_io(s, accept_f, "accept", chen::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    // 将新创建的文件描述符加入管理器中
    if (fd >= 0) {
        chen::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", chen::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", chen::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", chen::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", chen::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", chen::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", chen::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", chen::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", chen::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", chen::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", chen::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

int close(int fd) {
    if (!chen::t_hook_enable) {
        // 即使 hook 未启用，也需清理 FdMgr 和 IOManager 事件，
        // 否则 fd 被复用时残留的 FdCtx/FdContext 会导致 addEvent 断言失败
        chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
        if (ctx) {
            auto iom = ctx->getIOManager();       // 优先用 FdCtx 记录的
            if (!iom) {
                iom = chen::IOManager::GetThis();  // 回退到当前线程的
            }
            chen::FdMgr::GetInstance()->del(fd);
            if (iom) {
                iom->cancelAll(fd);
            }
        }
        return close_f(fd);
    }

    chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
    if (ctx) {
        auto iom = ctx->getIOManager();       // 优先用 FdCtx 记录的
        if (!iom) {
            iom = chen::IOManager::GetThis();  // 回退到当前线程的
        }
        chen::FdMgr::GetInstance()->del(fd);
        if (iom) {
            iom->cancelAll(fd);
        }
    }
    return close_f(fd);
}

// 对用户反馈是否用户设置了非阻塞模式
int fcntl(int fd, int cmd, ... /* arg */ ) {
    va_list va;
    va_start(va, cmd);
    switch (cmd) {
    case F_SETFL:
        {
            int arg = va_arg(va, int);
            va_end(va);
            chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
            // 如果在文件描述符管理器中找不到，或者已经关闭了，或者不是socket系列的，就用原函数
            if (!ctx || ctx->isClose() || !ctx->isSocket()) {
                return fcntl_f(fd, cmd, arg);
            }
            // 判断要添加的信息包含非阻塞
            ctx->setUserNonblock(arg & O_NONBLOCK);
            // 如果是用户主动设置的，就将其加上，否则就拿掉
            if (ctx->getUserNonblock()) {
                arg |= O_NONBLOCK;
            } else {
                arg &= ~O_NONBLOCK;
            }
            return fcntl_f(fd, cmd, arg);
        }
        break;
    case F_GETFL:
        {
            va_end(va);
            int arg = fcntl_f(fd, cmd);
            chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
            if (!ctx || ctx->isClose() || !ctx->isSocket()) {
                return arg;
            }
            if (ctx->getUserNonblock()) {
                return arg | O_NONBLOCK;
            } else {
                return arg & ~O_NONBLOCK;
            }
        }
        break;
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
#ifdef F_SETPIPE_SZ
    case F_SETPIPE_SZ:
#endif
        {
            va_end(va);
            return fcntl_f(fd, cmd);
        }
        break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK:
        {
            struct flock* arg = va_arg(va, struct flock*);
            va_end(va);
            return fcntl_f(fd, cmd, arg);
        }
        break;
    case F_GETOWN_EX:
        {
            struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
            va_end(va);
            return fcntl(fd, cmd, arg);
        }
        break;
    default:
        va_end(va);
        return fcntl_f(fd, cmd);
    }
}

int ioctl(int fd, unsigned long int request, ...) {
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*);
    va_end(va);

    // FIONBIO用于设置文件描述符的非阻塞模式
    if (FIONBIO == request) {
        bool user_nonblock = !!*(int*)arg;
        chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(fd);
        if (!ctx || ctx->isClose() || !ctx->isSocket()) {
            return ioctl_f(fd, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    if (!chen::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    // 设置socket通用选项
    if (level == SOL_SOCKET) {
        // 如果设置超时选项
        if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            chen::FdCtx::ptr ctx = chen::FdMgr::GetInstance()->get(sockfd);
            if (ctx) {
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

} // extern "C"
