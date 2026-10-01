#include "thread.h"

#include "../log/log.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/* 当前这根线程 */
static thread_local Thread* t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

Thread* Thread::GetThis() {
    return t_thread;
}

Thread::Thread(std::function<void()> task, const std::string& name)
    :m_task(task)
    ,m_name(name) {
    if (name.empty()) {
        m_name = "UNKNOW";
    }
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {
        ERROR(logger) << "pthread_create thread fail, rt= " << rt
            << "name=" << name;
        throw std::logic_error("pthread_create error!");
    }
    m_semaphore.wait();
}

Thread::~Thread() {
    if (m_thread) {
        pthread_detach(m_thread);
    }
}

void Thread::join() {
    if (m_thread) {
        int rt = pthread_join(m_thread, nullptr);
        if (rt) {
            ERROR(logger) << "pthread_join thread fail, rt= " << rt
                << "name=" << m_name;
            throw std::logic_error("pthread_join error!");
        }
        m_thread = 0;
    }
}

const std::string& Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string& name) {
    if (name.empty()) {
        return ;
    }
    if (t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

void* Thread::run(void *arg) {
    Thread* thread = (Thread*)arg;
    t_thread = thread;
    t_thread_name = thread->m_name;
    thread->m_id = GetThreadId();
    /* 设置线程的名称 */
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> task;
    task.swap(thread->m_task);

    thread->m_semaphore.notify();
    task();
    
    return 0;
}

}
