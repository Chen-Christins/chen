/**
 * @file iomanager.h
 * @brief IO协程调度
 * @author Christins
 * @date 2024-11-14
 */
#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>

#include <sys/epoll.h>

#include "../schedule/schedule.h"
#include "../timer/timer.h"
#include "../util/macro.h"

namespace chen {

class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;

    enum Event {
        /// 无事件
        NONE  = 0x0,
        /// EPOLLIN
        READ  = 0x1,
        /// EPOLLOUT
        WRITE = 0x4
    };
private:
    struct FdContext {
        /**
         * @brief 事件上下文
         */
        struct EventContext {
            Scheduler* scheduler = nullptr;
            Fiber::ptr fiber;
            std::function<void()> cb;
        };
        /**
         * @brief 获取对应事件上下文
         */
        EventContext& getContext(Event event) {
            switch (event) {
            case IOManager::READ:
                return read;
            case IOManager::WRITE:
                return write;
            default:
                ASSERT_MSG(false, "getContext");
            }
            throw std::invalid_argument("getContext invalid event");
        }
        /**
         * @brief 重置事件上下文
         */
        void resetContext(EventContext& ctx) {
            ctx.scheduler = nullptr;
            ctx.fiber.reset();
            ctx.cb = nullptr;
        }
        /**
         * @brief 触发事件
         */
        void triggerEvent(Event event) {
            ASSERT(events & event);
            // 触发事件就从注册事件中删除
            events = (Event)(events & ~event);
            EventContext& ctx = getContext(event);
            if (ctx.cb) {
                /* 使用地址传入，会将cb的引用计数-1 */
                ctx.scheduler->schedule(&ctx.cb);
            } else {
                /* 使用地址传入，会将fiber的引用计数-1 */
                ctx.scheduler->schedule(&ctx.fiber);
            }
            ctx.scheduler = nullptr;
            return ;
        }
        /// 事件文件描述符
        int fd = 0;
        /// 读事件
        EventContext read;
        /// 写事件
        EventContext write;
        /// 已经注册的事件
        Event events = NONE;
        /// 互斥锁
        std::mutex mutex;
    };
public:
    /**
     * @brief 构造函数
     * @param threadCount 创建的线程数量
     * @param use_caller 是否将当前的调度线程纳入工作线程中
     * @param name 线程名称/调度器名称
     */
    IOManager(size_t threadCount = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数
     */
    ~IOManager();

    /**
     * @brief 为当前的
     * @param fd 文件描述符
     * @param event 要添加的事件 
     * @param cb 回调函数
     * @return int 0 成功，-1 失败
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    /**
     * @brief 在fd的上下文中将event事件删除
     * @param fd 文件描述符
     * @param event 要删除的事件
     * @return bool 是否删除成功
     */
    bool delEvent(int fd, Event event);

    /**
     * @brief 取消fd上下文的event事件
     * @attention 取消事件会触发那个要取消的事件，但删除不会触发
     * @param fd 文件描述符
     * @param event 要取消的事件
     * @return bool 是否成功取消
     */
    bool cancelEvent(int fd, Event event);

    /**
     * @brief 取消所有事件
     * @attention 取消所有事件，这里分为读事件和写事件，这里的逻辑和取消单个事件差不多
     * @param fd 文件描述符
     * @return bool 是否取消成功
     */
    bool cancelAll(int fd);

    /**
     * @brief 获取当前IO协程调度器
     * @details 这里主要是使用的std::dynamic_cast来将基类指针转换为派生类
     * @return IOManager* 当前IO调度器
     */
    static IOManager* GetThis();
protected:
    /**
     * @brief 通知有任务来了
     */
    void tickle() override;
    
    /**
     * @brief 停止条件
     */
    bool stopping() override;
    
    /**
     * @brief 调度器无任务执行的时候
     */
    void idle() override;
    
    /**
     * @brief 真正的停止条件
     */
    bool stopping(uint64_t& timeout);
    
    /**
     * @brief 扩容
     */
    void contextResize(size_t v);
    
    /**
     * @brief tickle一下
     */
    virtual void onTimerInsertedAtFront() override;
private:
    /// Epollfd
    int m_epfd = 0;
    /// 管道的读写端 [0]读 [1]写
    int m_tickleFds[2];
    /// 等待执行的事件数量
    std::atomic<size_t> m_penddingEventCount = {0};
    /// 读写锁
    std::shared_mutex m_mutex;
    /// socket上下文容器
    std::vector<FdContext*> m_fdContexts;
};

}
