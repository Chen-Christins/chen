#include "FoxThread.hpp"

#include <iomanip>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "../Util/Macro.hpp"
#include "../Util/Util.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_thread_info_set =
    Config::Lookup("fox_thread", std::map<std::string, std::map<std::string, std::string>>(), "config for thread");

static std::shared_mutex s_thread_mutex;
static std::map<uint64_t, std::string> s_thread_names;

thread_local FoxThread* s_thread = nullptr;

IFoxThread::~IFoxThread()
{
}

void FoxThread::read_cb(evutil_socket_t sock, short which, void* args)
{
    FoxThread* thread = static_cast<FoxThread*>(args);
    uint8_t cmd[4096];
    if (recv(sock, cmd, sizeof(cmd), 0) > 0) {
        std::list<callback> callbacks;
        std::unique_lock lock(thread->mutex_);
        callbacks.swap(thread->callbacks_);
        lock.unlock();
        thread->working_ = true;
        for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
            if (*it) {
                // ASSERT(thread == GetThis());
                try {
                    (*it)();
                } catch (std::exception& ex) {
                    ERROR(logger) << "exception:" << ex.what();
                } catch (const char* c) {
                    ERROR(logger) << "exception:" << c;
                } catch (...) {
                    ERROR(logger) << "uncatch exception";
                }
            } else {
                event_base_loopbreak(thread->base_);
                thread->start_ = false;
                thread->unsetThis();
                break;
            }
        }
        Atomic::addFetch(thread->total_, callbacks.size());
        thread->working_ = false;
    }
}

FoxThread::FoxThread(const std::string& name, struct event_base* base)
    : read_(0)
    , write_(0)
    , base_(NULL)
    , event_(NULL)
    , thread_(NULL)
    , name_(name)
    , working_(false)
    , start_(false)
    , total_(0)
{
    int fds[2];
    if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        // ERROR(logger) << "FoxThread init error";
        throw std::logic_error("thread init error");
    }

    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    read_ = fds[0];
    write_ = fds[1];

    if (base) {
        base_ = base;
        setThis();
    } else {
        base_ = event_base_new();
    }
    event_ = event_new(base_, read_, EV_READ | EV_PERSIST, read_cb, this);
    event_add(event_, NULL);
}

void FoxThread::dump(std::ostream& os)
{
    std::shared_lock lock(mutex_);
    os << "[thread name=" << name_ << " working=" << working_ << " tasks=" << callbacks_.size() << " total=" << total_
       << "]" << std::endl;
}

std::thread::id FoxThread::getId() const
{
    if (thread_) {
        return thread_->get_id();
    }
    return std::thread::id();
}

void* FoxThread::getData(const std::string& name)
{
    // Mutex::ReadLock lock(mutex_);
    auto it = datas_.find(name);
    return it == datas_.end() ? nullptr : it->second;
}

void FoxThread::setData(const std::string& name, void* v)
{
    // Mutex::WriteLock lock(mutex_);
    datas_[name] = v;
}

FoxThread::~FoxThread()
{
    if (read_) {
        close(read_);
    }
    if (write_) {
        close(write_);
    }
    stop();
    join();
    if (thread_) {
        delete thread_;
    }
    if (event_) {
        event_free(event_);
    }
    if (base_) {
        event_base_free(base_);
    }
}

void FoxThread::start()
{
    if (thread_) {
        // ERROR(logger) << "FoxThread is running";
        throw std::logic_error("FoxThread is running");
    }

    thread_ = new std::thread(std::bind(&FoxThread::thread_cb, this));
    start_ = true;
}

void FoxThread::thread_cb()
{
    // std::cout << "FoxThread(" << name_ << "," << pthread_self() << ")" << std::endl;
    setThis();
    pthread_setname_np(pthread_self(), name_.substr(0, 15).c_str());
    if (init_cb_) {
        init_cb_(this);
        init_cb_ = nullptr;
    }
    event_base_loop(base_, 0);
}

bool FoxThread::dispatch(callback cb)
{
    std::unique_lock lock(mutex_);
    callbacks_.push_back(cb);
    // if(callbacks_.size() > 1) {
    //     std::cout << std::this_thread::get_id() << ":" << callbacks_.size() << " " << name_ << std::endl;
    // }
    lock.unlock();
    uint8_t cmd = 1;
    // write(write_, &cmd, sizeof(cmd));
    if (send(write_, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

bool FoxThread::dispatch(uint32_t id, callback cb)
{
    return dispatch(cb);
}

bool FoxThread::batchDispatch(const std::vector<callback>& cbs)
{
    std::unique_lock lock(mutex_);
    for (auto& i : cbs) {
        callbacks_.push_back(i);
    }
    lock.unlock();
    uint8_t cmd = 1;
    if (send(write_, &cmd, sizeof(cmd), 0) <= 0) {
        return false;
    }
    return true;
}

void FoxThread::broadcast(callback cb)
{
    dispatch(cb);
}

void FoxThread::stop()
{
    std::unique_lock lock(mutex_);
    callbacks_.push_back(nullptr);
    if (thread_) {
        uint8_t cmd = 0;
        // write(write_, &cmd, sizeof(cmd));
        send(write_, &cmd, sizeof(cmd), 0);
    }
    // if(data_) {
    //     delete data_;
    //     data_ = NULL;
    // }
}

void FoxThread::join()
{
    if (thread_) {
        thread_->join();
        delete thread_;
        thread_ = NULL;
    }
}

FoxThreadPool::FoxThreadPool(uint32_t size, const std::string& name, bool advance)
    : size_(size)
    , cur_(0)
    , name_(name)
    , advance_(advance)
    , start_(false)
    , total_(0)
{
    threads_.resize(size_);
    for (size_t i = 0; i < size; ++i) {
        FoxThread* t(new FoxThread(name + "_" + std::to_string(i)));
        threads_[i] = t;
    }
}

FoxThreadPool::~FoxThreadPool()
{
    for (size_t i = 0; i < size_; ++i) {
        delete threads_[i];
    }
}

void FoxThreadPool::start()
{
    for (size_t i = 0; i < size_; ++i) {
        threads_[i]->setInitCb(init_cb_);
        threads_[i]->start();
        free_fox_threads_.push_back(threads_[i]);
    }
    if (init_cb_) {
        init_cb_ = nullptr;
    }
    start_ = true;
    check();
}

void FoxThreadPool::stop()
{
    for (size_t i = 0; i < size_; ++i) {
        threads_[i]->stop();
    }
    start_ = false;
}

void FoxThreadPool::join()
{
    for (size_t i = 0; i < size_; ++i) {
        threads_[i]->join();
    }
}

void FoxThreadPool::releaseFoxThread(FoxThread* t)
{
    do {
        std::unique_lock lock(mutex_);
        free_fox_threads_.push_back(t);
    } while (0);
    check();
}

bool FoxThreadPool::dispatch(callback cb)
{
    do {
        Atomic::addFetch(total_, (uint64_t)1);
        std::unique_lock lock(mutex_);
        if (!advance_) {
            return threads_[cur_++ % size_]->dispatch(cb);
        }
        callbacks_.push_back(cb);
    } while (0);
    check();
    return true;
}

bool FoxThreadPool::batchDispatch(const std::vector<callback>& cbs)
{
    Atomic::addFetch(total_, cbs.size());
    std::unique_lock lock(mutex_);
    if (!advance_) {
        for (auto cb : cbs) {
            threads_[cur_++ % size_]->dispatch(cb);
        }
        return true;
    }
    for (auto cb : cbs) {
        callbacks_.push_back(cb);
    }
    lock.unlock();
    check();
    return true;
}

void FoxThreadPool::check()
{
    do {
        if (!start_) {
            break;
        }
        std::unique_lock lock(mutex_);
        if (free_fox_threads_.empty() || callbacks_.empty()) {
            break;
        }

        std::shared_ptr<FoxThread> thr(
            free_fox_threads_.front(), std::bind(&FoxThreadPool::releaseFoxThread, this, std::placeholders::_1));

        free_fox_threads_.pop_front();

        callback cb = callbacks_.front();
        callbacks_.pop_front();
        lock.unlock();

        if (thr->isStart()) {
            thr->dispatch(std::bind(&FoxThreadPool::wrapcb, this, thr, cb));
        } else {
            std::unique_lock lock(mutex_);
            callbacks_.push_front(cb);
        }
    } while (true);
}

void FoxThreadPool::wrapcb(std::shared_ptr<FoxThread> thr, callback cb)
{
    cb();
}

bool FoxThreadPool::dispatch(uint32_t id, callback cb)
{
    Atomic::addFetch(total_, (uint64_t)1);
    return threads_[id % size_]->dispatch(cb);
}

FoxThread* FoxThreadPool::getRandFoxThread()
{
    return threads_[cur_++ % size_];
}

void FoxThreadPool::broadcast(callback cb)
{
    for (size_t i = 0; i < threads_.size(); ++i) {
        threads_[i]->dispatch(cb);
    }
}

void FoxThreadPool::dump(std::ostream& os)
{
    std::shared_lock lock(mutex_);
    os << "[FoxThreadPool name = " << name_ << " thread_count = " << threads_.size() << " tasks = " << callbacks_.size()
       << " total = " << total_ << " advance = " << advance_ << "]" << std::endl;
    for (size_t i = 0; i < threads_.size(); ++i) {
        os << "    ";
        threads_[i]->dump(os);
    }
}

FoxThread* FoxThread::GetThis()
{
    return s_thread;
}

const std::string& FoxThread::GetFoxThreadName()
{
    FoxThread* t = GetThis();
    if (t) {
        return t->name_;
    }

    uint64_t tid = GetThreadId();
    do {
        std::shared_lock lock(s_thread_mutex);
        auto it = s_thread_names.find(tid);
        if (it != s_thread_names.end()) {
            return it->second;
        }
    } while (0);

    do {
        std::unique_lock lock(s_thread_mutex);
        s_thread_names[tid] = "UNNAME_" + std::to_string(tid);
        return s_thread_names[tid];
    } while (0);
}

void FoxThread::GetAllFoxThreadName(std::map<uint64_t, std::string>& names)
{
    std::shared_lock lock(s_thread_mutex);
    for (auto it = s_thread_names.begin(); it != s_thread_names.end(); ++it) {
        names.insert(*it);
    }
}

void FoxThread::setThis()
{
    name_ = name_ + "_" + std::to_string(GetThreadId());
    s_thread = this;

    std::unique_lock lock(s_thread_mutex);
    s_thread_names[GetThreadId()] = name_;
}

void FoxThread::unsetThis()
{
    s_thread = nullptr;
    std::unique_lock lock(s_thread_mutex);
    s_thread_names.erase(GetThreadId());
}

IFoxThread::ptr FoxThreadManager::get(const std::string& name)
{
    std::shared_lock lock(mutex_);
    auto it = threads_.find(name);
    return it == threads_.end() ? nullptr : it->second;
}

void FoxThreadManager::add(const std::string& name, IFoxThread::ptr thr)
{
    std::unique_lock lock(mutex_);
    threads_[name] = thr;
}

void FoxThreadManager::dispatch(const std::string& name, callback cb)
{
    IFoxThread::ptr ti = get(name);
    ASSERT(ti);
    ti->dispatch(cb);
}

void FoxThreadManager::dispatch(const std::string& name, uint32_t id, callback cb)
{
    IFoxThread::ptr ti = get(name);
    ASSERT(ti);
    ti->dispatch(id, cb);
}

void FoxThreadManager::batchDispatch(const std::string& name, const std::vector<callback>& cbs)
{
    IFoxThread::ptr ti = get(name);
    ASSERT(ti);
    ti->batchDispatch(cbs);
}

void FoxThreadManager::broadcast(const std::string& name, callback cb)
{
    IFoxThread::ptr ti = get(name);
    ASSERT(ti);
    ti->broadcast(cb);
}

void FoxThreadManager::dumpFoxThreadStatus(std::ostream& os)
{
    os << "FoxThreadManager: " << std::endl;
    for (auto it = threads_.begin(); it != threads_.end(); ++it) {
        it->second->dump(os);
    }

    os << "All FoxThreads:" << std::endl;
    std::map<uint64_t, std::string> names;
    FoxThread::GetAllFoxThreadName(names);
    for (auto it = names.begin(); it != names.end(); ++it) {
        os << std::setw(30) << it->first << ": " << it->second << std::endl;
    }
}

void FoxThreadManager::init()
{
    std::unique_lock lock(mutex_);
    if (inited_) {
        INFO(logger) << "FoxThreadManager already initialized, skip";
        return;
    }

    auto m = g_thread_info_set->getValue();
    for (auto i : m) {
        auto num = GetParamValue(i.second, "num", 0);
        auto name = i.first;
        auto advance = GetParamValue(i.second, "advance", 0);
        if (num <= 0) {
            ERROR(logger) << "thread pool:" << name << " num:" << num << " advance:" << advance << " invalid";
            continue;
        }
        if (num == 1) {
            threads_[i.first] = FoxThread::ptr(new FoxThread(i.first));
            INFO(logger) << "init thread : " << i.first;
        } else {
            threads_[i.first] = FoxThreadPool::ptr(new FoxThreadPool(num, name, advance));
            INFO(logger) << "init thread pool:" << name << " num:" << num << " advance:" << advance;
        }
    }
    inited_ = true;
}

void FoxThreadManager::start()
{
    std::unique_lock lock(mutex_);
    if (started_) {
        INFO(logger) << "FoxThreadManager already started, skip";
        return;
    }
    started_ = true;

    for (auto i : threads_) {
        INFO(logger) << "thread: " << i.first << " start begin";
        i.second->start();
        INFO(logger) << "thread: " << i.first << " start end";
    }
}

void FoxThreadManager::stop()
{
    std::unique_lock lock(mutex_);
    for (auto i : threads_) {
        INFO(logger) << "thread: " << i.first << " stop begin";
        i.second->stop();
        INFO(logger) << "thread: " << i.first << " stop end";
    }
    for (auto i : threads_) {
        INFO(logger) << "thread: " << i.first << " join begin";
        i.second->join();
        INFO(logger) << "thread: " << i.first << " join end";
    }
    // 清空线程池，触发 ~FoxThread() 释放 pipe/event/event_base
    threads_.clear();
    // 重置状态，允许再次 init/start（如完整关闭后需要重建）
    started_ = false;
    inited_ = false;
}

} // namespace chen
