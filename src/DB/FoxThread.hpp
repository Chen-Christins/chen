/**
 * @file FoxThread.hpp
 * @brief Fox 线程池
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <list>
#include <map>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include "../Util/Singleton.hpp"

namespace chen {

class FoxThread;
class IFoxThread {
public:
    typedef std::shared_ptr<IFoxThread> ptr;
    typedef std::function<void()> callback;

    virtual ~IFoxThread();
    virtual bool dispatch(callback cb) = 0;
    virtual bool dispatch(uint32_t id, callback cb) = 0;
    virtual bool batchDispatch(const std::vector<callback>& cbs) = 0;
    virtual void broadcast(callback cb) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void join() = 0;
    virtual void dump(std::ostream& os) = 0;
    virtual uint64_t getTotal() = 0;
};

class FoxThread : public IFoxThread {
public:
    typedef std::shared_ptr<FoxThread> ptr;
    typedef IFoxThread::callback callback;
    typedef std::function<void(FoxThread*)> init_cb;
    FoxThread(const std::string& name = "", struct event_base* base = NULL);
    ~FoxThread();

    static FoxThread* GetThis();
    static const std::string& GetFoxThreadName();
    static void GetAllFoxThreadName(std::map<uint64_t, std::string>& names);

    void setThis();
    void unsetThis();

    void start();

    virtual bool dispatch(callback cb);
    virtual bool dispatch(uint32_t id, callback cb);
    virtual bool batchDispatch(const std::vector<callback>& cbs);
    virtual void broadcast(callback cb);

    void join();
    void stop();
    bool isStart() const { return start_; }

    struct event_base* getBase() { return base_; }
    std::thread::id getId() const;

    void* getData(const std::string& name);
    template <class T>
    T* getData(const std::string& name)
    {
        return (T*)getData(name);
    }
    void setData(const std::string& name, void* v);

    void setInitCb(init_cb v) { init_cb_ = v; }

    void dump(std::ostream& os);
    virtual uint64_t getTotal() { return total_; }

private:
    void thread_cb();
    static void read_cb(evutil_socket_t sock, short which, void* args);

private:
    evutil_socket_t read_;
    evutil_socket_t write_;
    struct event_base* base_;
    struct event* event_;
    std::thread* thread_;
    std::shared_mutex mutex_;
    std::list<callback> callbacks_;

    std::string name_;
    init_cb init_cb_;

    std::map<std::string, void*> datas_;

    bool working_;
    bool start_;
    uint64_t total_;
};

class FoxThreadPool : public IFoxThread {
public:
    typedef std::shared_ptr<FoxThreadPool> ptr;
    typedef IFoxThread::callback callback;

    FoxThreadPool(uint32_t size, const std::string& name = "", bool advance = false);
    ~FoxThreadPool();

    void start();
    void stop();
    void join();

    // 随机线程执行
    bool dispatch(callback cb);
    bool batchDispatch(const std::vector<callback>& cb);
    // 指定线程执行
    bool dispatch(uint32_t id, callback cb);

    FoxThread* getRandFoxThread();
    void setInitCb(FoxThread::init_cb v) { init_cb_ = v; }

    void dump(std::ostream& os);

    void broadcast(callback cb);
    virtual uint64_t getTotal() { return total_; }

private:
    void releaseFoxThread(FoxThread* t);
    void check();

    void wrapcb(std::shared_ptr<FoxThread>, callback cb);

private:
    uint32_t size_;
    uint32_t cur_;
    std::string name_;
    bool advance_;
    bool start_;
    std::shared_mutex mutex_;
    std::list<callback> callbacks_;
    std::vector<FoxThread*> threads_;
    std::list<FoxThread*> free_fox_threads_;
    FoxThread::init_cb init_cb_;
    uint64_t total_;
};

class FoxThreadManager {
public:
    typedef IFoxThread::callback callback;
    void dispatch(const std::string& name, callback cb);
    void dispatch(const std::string& name, uint32_t id, callback cb);
    void batchDispatch(const std::string& name, const std::vector<callback>& cbs);
    void broadcast(const std::string& name, callback cb);

    void dumpFoxThreadStatus(std::ostream& os);

    void init();
    void start();
    void stop();

    IFoxThread::ptr get(const std::string& name);
    void add(const std::string& name, IFoxThread::ptr thr);

private:
    /// 线程池集合
    std::map<std::string, IFoxThread::ptr> threads_;
    /// 保护 threads_ 与初始化状态的互斥锁
    std::shared_mutex mutex_;
    /// 是否已完成 init（进程内仅一次，避免热重载销毁在用线程池）
    bool inited_ = false;
    /// 是否已完成 start
    bool started_ = false;
};

typedef Singleton<FoxThreadManager> FoxThreadMgr;

} // namespace chen
