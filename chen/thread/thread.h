/**
 * @file thread.h
 * @brief 线程模块
 * @author Christins
 * @date 2024-11-03
 */
#pragma once

#include <functional>
#include <memory>

#include "../util/mutex.h"
#include "../util/noncopyable.h"

namespace chen {

/**
 * @brief 线程模块
 */
class Thread : Noncopyable {
public:
    typedef std::shared_ptr<Thread> ptr;
    /**
     * @brief 构造函数，创建一个携带task任务函数的线程
     * @param task 线程执行函数
     * @param name 线程名称
     */
    Thread(std::function<void()> task, const std::string& name);

    /**
     * @brief 析构函数
     */
    ~Thread();

    /**
     * @brief 获取线程id
     * @return 返回线程的pid
     */
    pid_t getId() const { return m_id; }

    /**
     * @brief 返回线程名称
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 等待子线程执行完成
     */
    void join();

    /**
     * @brief 获取当前这个线程的指针
     * @return Thread* 当前线程的指针
     */
    static Thread* GetThis();

    /**
     * @brief 静态方法，获取线程名称
     */
    static const std::string& GetName();

    /**
     * @brief 设置当前线程的名称
     * @param name 传入的新名称
     */
    static void SetName(const std::string& name);
private:
    /**
     * @brief 线程执行的任务
     * @param arg void(void*) 类型的参数
     * @return void* 返回void类型的
     */
    static void* run(void *arg);
private:
    /// 线程执行函数
    std::function<void()> m_task;
    /// 线程名称
    std::string m_name;
    /// 线程结构
    pthread_t m_thread = 0;
    /// 线程号
    pid_t m_id = -1;
    /// 信号量
    Semaphore m_semaphore;
};

}
