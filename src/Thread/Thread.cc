#include "Thread.hpp"

#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/* 当前这根线程 */
static thread_local Thread* t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

Thread* Thread::GetThis()
{
    return t_thread;
}

Thread::Thread(std::function<void()> task, const std::string& name)
    : task_(task)
    , name_(name)
{
    if (name.empty()) {
        name_ = "UNKNOW";
    }
    int rt = pthread_create(&thread_, nullptr, &Thread::run, this);
    if (rt) {
        ERROR(logger) << "pthread_create thread fail, rt= " << rt << "name=" << name;
        throw std::logic_error("pthread_create error!");
    }
    semaphore_.wait();
}

Thread::~Thread()
{
    if (thread_) {
        pthread_detach(thread_);
    }
}

void Thread::join()
{
    if (thread_) {
        int rt = pthread_join(thread_, nullptr);
        if (rt) {
            ERROR(logger) << "pthread_join thread fail, rt= " << rt << "name=" << name_;
            throw std::logic_error("pthread_join error!");
        }
        thread_ = 0;
    }
}

const std::string& Thread::GetName()
{
    return t_thread_name;
}

void Thread::SetName(const std::string& name)
{
    if (name.empty()) {
        return;
    }
    if (t_thread) {
        t_thread->name_ = name;
    }
    t_thread_name = name;
}

void* Thread::run(void* arg)
{
    Thread* thread = (Thread*)arg;
    t_thread = thread;
    t_thread_name = thread->name_;
    thread->id_ = GetThreadId();
    /* 设置线程的名称 */
    pthread_setname_np(pthread_self(), thread->name_.substr(0, 15).c_str());

    std::function<void()> task;
    task.swap(thread->task_);

    thread->semaphore_.notify();
    task();

    return 0;
}

} // namespace chen
