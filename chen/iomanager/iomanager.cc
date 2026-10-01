#include "iomanager.h"

#include <fcntl.h>

#include "../hook/fd_manager.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

IOManager::IOManager(size_t threadCount, bool use_caller, const std::string& name)
    :Scheduler(threadCount, use_caller, name) {
    // 创建epoll文件描述符
    m_epfd = epoll_create(5);
    // 成功epoll会返回非负文件描述符，出错就会返回-1
    ASSERT(m_epfd != -1);

    // 创建管道，用于进程间通信
    int rt = pipe(m_tickleFds);
    // 成功返回0，失败返回-1
    ASSERT(rt != -1);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    // 注册读事件，设置边沿触发
    event.events = EPOLLIN | EPOLLET;
    // fd 关联管道的读端
    event.data.fd = m_tickleFds[0];

    // 对一个打开的文件描述符执行控制操作
    // F_SETFL: 获取/设置文件状态标志
    // ON_NONBLOCK: 让文件描述符变成非阻塞模式，当读不到数据或者写缓冲区满了就马上return, 而不会阻塞等待
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    ASSERT(rt != -1);

    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    ASSERT(rt != -1);

    contextResize(32);

    start();
}

IOManager::~IOManager() {
    // 停止调度器
    stop();
    // 关闭epoll文件描述符
    close(m_epfd);
    // 关闭读管道的文件描述符
    close(m_tickleFds[0]);
    // 关闭写管道的文件描述符
    close(m_tickleFds[1]);

    // 释放所有的m_fdcontext的资源
    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx = nullptr;
    // 加读锁
    std::shared_lock lock(m_mutex);
    // 从 m_fdContexts 中拿到对应的 Fdcontext
    if ((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } else {
        lock.unlock();
        std::unique_lock lock2(m_mutex);
        // 扩容
        contextResize(1.5 * fd);
        // 取注册的事件上下文信息
        fd_ctx = m_fdContexts[fd];
    }

    std::unique_lock lock2(fd_ctx->mutex);
    // 一个文件描述符不会重复添加同一个事件
    if (UNLIKELY(fd_ctx->events & event)) {
        // 检查是否为 fd 关闭后被复用的场景：
        // FdCtx 不存在或已关闭 → 旧连接已释放，fd 被新连接复用
        FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
        bool fd_reused = !ctx || ctx->isClose();
        if (fd_reused) {
            WARN(logger) << "addEvent: fd=" << fd << " FdContext has stale events="
                        << fd_ctx->events << " (fd reused after close), resetting";
            // 修正 m_penddingEventCount: 残留的事件从未被 cancel 触发，需手动递减
            if (fd_ctx->events & READ) {
                --m_penddingEventCount;
                fd_ctx->resetContext(fd_ctx->read);
            }
            if (fd_ctx->events & WRITE) {
                --m_penddingEventCount;
                fd_ctx->resetContext(fd_ctx->write);
            }
            fd_ctx->events = NONE;
        } else {
            ERROR(logger) << "addEvent assert fd = " << fd
                << ", event = " << event
                << ", fd_ctx.event = " << fd_ctx->events;
            ASSERT(!(fd_ctx->events & event));
        }
    }

    // 如果已经有注册事件则改为修改操作，没有就改为添加操作
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    // 设置边沿触发，将原本的事件和要注册的事件都添加上去
    epevent.events = EPOLLET | fd_ctx->events | event;
    // 将fd_ctx存到data的指针中
    epevent.data.ptr = fd_ctx;

    // 注册事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    /* 成功0 失败-1 */
    if (rt) {
        ERROR(logger) << "epoll_ctl(" << m_epfd << ","
            << op << ", " << fd << ", " << epevent.events << ") : "
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return -1;
    }
    // 等待执行的事件数量+1
    ++m_penddingEventCount;
    // 将 fd_ctx 的注册事件更新
    fd_ctx->events = (Event)(fd_ctx->events | event);
    // 记录注册此 fd 的 IOManager，确保其他线程 close(fd) 能找到正确的 IOManager
    auto ctx = FdMgr::GetInstance()->get(fd);
    if (ctx) {
        ctx->setIOManager(this);
    }
    // 获取对应事件的EventContext
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    ASSERT(!event_ctx.scheduler
            && !event_ctx.fiber
            && !event_ctx.cb);
    // 获取当前的调度器
    event_ctx.scheduler = Scheduler::GetThis();

    // 将回调函数添加进去
    if (cb) {
        event_ctx.cb.swap(cb);
    } else {
        // 将当前线程的工作协程放进去
        event_ctx.fiber = Fiber::GetThis();
        ASSERT_MSG(event_ctx.fiber->getState() == Fiber::EXEC || event_ctx.fiber->getState() == Fiber::HOLD
                ,"state=" << event_ctx.fiber->getState());
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    std::shared_lock lock(m_mutex);
    if (fd >= (int)m_fdContexts.size()) {
        return false;
    }
    // 拿到fd对应的context
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    std::unique_lock lock2(fd_ctx->mutex);
    // 如果没有要删除的事件
    if (UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }
    // 将事件从注册事件中删除
    Event new_events = (Event)(fd_ctx->events & ~event);
    // 如果还有事件，就将操作变成修改
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    // 边沿触发模式，注册新的事件
    epevent.events = EPOLLET | new_events;
    // ptr 关联 fd_ctx
    epevent.data.ptr = fd_ctx;

    // 注册事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        if (errno == ENOENT || errno == EBADF) {
            // fd 已不在 epoll 中（ENOENT: 内核移除，EBADF: fd 已关闭），清理残留的内存状态
            WARN(logger) << "delEvent epoll_ctl fd=" << fd << " errno=" << errno
                        << ", cleaning stale events";
            --m_penddingEventCount;
            fd_ctx->events = new_events;
            FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
            fd_ctx->resetContext(event_ctx);
            return true;
        }
        ERROR(logger) << "epoll_ctl(" << m_epfd << ","
            << op << ", " << fd << ", " << epevent.events << ") : "
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }
    // 等待执行的事件数量-1
    --m_penddingEventCount;
    // 更新事件
    fd_ctx->events = new_events;
    // 拿到对应事件的EventContext
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    // 重置EventContext, 将其置为空
    fd_ctx->resetContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    std::shared_lock lock(m_mutex);
    if (fd >= (int)m_fdContexts.size()) {
        return false;
    }
    // 拿到fd对应的context
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    std::unique_lock lock2(fd_ctx->mutex);
    // 如果没有要删除的事件
    if (!(fd_ctx->events & event)) {
        return false;
    }
    // 将事件从注册事件中删除
    Event new_events = (Event)(fd_ctx->events & ~event);
    // 如果还有事件，就将操作变成修改
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    // 边沿触发模式，注册新的事件
    epevent.events = EPOLLET | new_events;
    // ptr 关联 fd_ctx
    epevent.data.ptr = fd_ctx;

    // 注册事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        if (errno == ENOENT || errno == EBADF) {
            // fd 已不在 epoll 中（ENOENT: 内核移除，EBADF: fd 已关闭），清理残留的内存状态
            WARN(logger) << "cancelEvent epoll_ctl fd=" << fd << " errno=" << errno
                        << ", cleaning stale events";
            fd_ctx->triggerEvent(event);
            --m_penddingEventCount;
            return true;
        }
        ERROR(logger) << "epoll_ctl(" << m_epfd << ","
            << op << ", " << fd << ", " << epevent.events << ") : "
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }
    // 触发事件
    fd_ctx->triggerEvent(event);
    --m_penddingEventCount;

    return true;
}

bool IOManager::cancelAll(int fd) { 
    std::shared_lock lock(m_mutex);
    if (fd >= (int)m_fdContexts.size()) {
        return false;
    }
    // 拿到fd对应的context
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    std::unique_lock lock2(fd_ctx->mutex);
    // 如果没有要删除的事件
    if (!(fd_ctx->events)) {
        return false;
    }
    // 如果还有事件，就将操作变成修改
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    // 直接删除，没有事件
    epevent.events = 0;
    // ptr 关联 fd_ctx
    epevent.data.ptr = fd_ctx;

    // 注册事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        // ENOENT/EBADF: fd 已不在 epoll 中（内核移除 / fd 已关闭），
        // FdContext 中的事件是残留的，需要清理内存状态，否则后续复用会反复报错
        if (errno == ENOENT || errno == EBADF) {
            WARN(logger) << "cancelAll epoll_ctl EPOLL_CTL_DEL fd=" << fd
                        << " errno=" << errno
                        << " (fd not in epoll, cleaning stale FdContext events="
                        << fd_ctx->events << ")";
        } else {
            ERROR(logger) << "epoll_ctl(" << m_epfd << ","
                << op << ", " << fd << ", " << epevent.events << ") : "
                << rt << " (" << errno << ") (" << strerror(errno) << ")";
            return false;
        }
    }
    // 触发事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_penddingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_penddingEventCount;
    }
    // 确保清空了所有事件
    ASSERT(fd_ctx->events == 0)
    return true;
}

IOManager* IOManager::GetThis() {
    // dynamic_cast 将基类指针转换到继承类
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

void IOManager::tickle() {
    // 没有空闲的线程，就不能tickle
    if (!hasIdleThreads()) {
        return ;
    }
    // 有任务来了，就往pipe里发送一个字节的数据，唤醒epoll_wait
    int rt = write(m_tickleFds[1], "T", 1);
    ASSERT(rt == 1);
}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    return timeout == ~0ull
            && m_penddingEventCount == 0
            && Scheduler::stopping();
}

void IOManager::idle() {
    const uint64_t MAX_EVNETS = 256;
    epoll_event* events = new epoll_event[MAX_EVNETS]();
    // 使用智能指针托管events，离开作用域{idle函数}自动删除
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr) {
        delete [] ptr;
    });

    while (true) {
        // 下一个任务要执行的时间
        uint64_t next_timeout = 0;
        // 获得下一个执行任务的时间，判断是否到达停止条件
        if (UNLIKELY(stopping(next_timeout))) {
            TRACE(logger) << "name= " << getName() << ", idle stopping exit";
            break;
        }

        int rt = 0;
        do {
            static const int MAX_TIMEOUT = 3000;
            if (next_timeout != ~0ull) {
                // 睡眠时间为next_timeout，但不能超过MAX_TIMEOUT
                next_timeout = (int)next_timeout > MAX_TIMEOUT ?
                    MAX_TIMEOUT : (int)next_timeout;
            } else {
                // 没有定时器就睡眠 MAX_TIMEOUT
                next_timeout = MAX_TIMEOUT;
            }
            /*  
             * 阻塞在这里，但有3中情况能够唤醒epoll_wait
             * 1. 超时时间到了
             * 2. 关注的 soket 有数据来了
             * 3. 通过 tickle 往 pipe 里发数据，表明有任务来了
             */
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            // 如果由于操作系统中断，就继续执行epoll_wait，成功就跳出去执行任务
            if (rt < 0 && errno == EINTR) {
            } else {
                break;
            }
        } while (true);

        // 获取已经超时的任务
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);

        // 全部放到任务队列
        if (!cbs.empty()) {
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }
        // 枚举所有触发的事件
        for (int i = 0; i < rt; ++i) {
            // 从 events 中拿一个 event
            epoll_event& event = events[i];
            // 如果获得的这个信息来自管道
            if (event.data.fd == m_tickleFds[0]) {
                uint8_t dumpy[256];
                // 将管道发来的这一个字节的数据读取
                while (read(m_tickleFds[0], &dumpy, sizeof(dumpy)) > 0);
                continue ;
            }

            // 从epoll.data.ptr中取出fdContext
            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            std::unique_lock lock(fd_ctx->mutex);
            // 如果注册的事件发生了错误，或者意外挂断
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                // 加入读写事件
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            // 真正的事件
            int real_events = NONE;
            // 读事件好了
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }   
            // 写事件好了
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }
            // 没有事件
            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }
            // 获取剩下的事件
            int left_events = (fd_ctx->events & ~real_events);
            // 如果执行完了real_events里的事件，还有事件则修改，否则删除
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            // 更新新的事件
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                // EBADF/ENOENT: fd 已在其他线程被 close（例如优雅关闭路径），
                // 内核已自动移除 epoll 事件，需清理用户态计数器，否则 idle 循环无法退出
                if (errno == EBADF || errno == ENOENT) {
                    WARN(logger) << "epoll_ctl(" << m_epfd << ","
                        << op << ", " << fd_ctx->fd << ", " << event.events << ") errno="
                        << errno << " (" << strerror(errno) << "), cleaning stale events";
                    // 触发残留事件并递减计数器，使 stopping() 条件最终满足
                    if (fd_ctx->events & READ) {
                        fd_ctx->triggerEvent(READ);
                        --m_penddingEventCount;
                    }
                    if (fd_ctx->events & WRITE) {
                        fd_ctx->triggerEvent(WRITE);
                        --m_penddingEventCount;
                    }
                } else {
                    ERROR(logger) << "epoll_ctl(" << m_epfd << ","
                        << op << ", " << fd_ctx->fd << ", " << event.events << ") : "
                        << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                }
                continue;
            }
            // 触发真正的事件
            if (fd_ctx->events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_penddingEventCount;
            }
            if (fd_ctx->events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_penddingEventCount;
            }
        }

        // 执行完epoll_wait返回的事件
        // 获取当前的协程
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        // 将当前idle协程指向空指针，状态为INIT
        cur.reset();
        
        // 执行完返回scheduler的MainFiber 继续下一轮
        raw_ptr->swapOut();
    }
}

void IOManager::contextResize(size_t v) {
    m_fdContexts.resize(v);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        // 没有就加一个
        if (!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}
