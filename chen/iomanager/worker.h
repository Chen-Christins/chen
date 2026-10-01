/**
 * @file worker.h
 * @brief worker模块
 * @author Christins
 * @date 2025-03-30
 * @copyright GPL-3.0
 */
#pragma once

#include "../util/noncopyable.h"
#include "iomanager.h"

namespace chen {

/**
 * @class WorkerGroup
 * @brief 工作组：用于批量调度任务并等待所有任务完成
 * 
 * 提供了批量任务调度和同步等待机制，常用于需要并发执行一批任务并等待全部完成的场景。
 * 使用信号量来控制并发数量，避免一次性提交过多任务导致系统资源耗尽。
 */
class WorkerGroup : Noncopyable, public std::enable_shared_from_this<WorkerGroup> {
public:
    typedef std::shared_ptr<WorkerGroup> ptr;
    
    /**
     * @brief 创建工作组的静态工厂方法
     * @param batch_size 批处理大小，控制同时执行的任务数量上限
     * @param s 调度器指针，默认使用当前调度器
     * @return WorkerGroup::ptr 工作组智能指针
     */
    static WorkerGroup::ptr Create(uint32_t batch_size, Scheduler* s = Scheduler::GetThis()) {
        return std::make_shared<WorkerGroup>(batch_size, s);
    }

    /**
     * @brief 构造函数
     * @param batch_size 批处理大小，控制同时执行的任务数量上限
     * @param s 调度器指针，默认使用当前调度器
     */
    WorkerGroup(uint32_t batch_size, Scheduler* s = Scheduler::GetThis());
    
    /**
     * @brief 析构函数
     */
    ~WorkerGroup();

    /**
     * @brief 调度一个任务到工作组
     * @param cb 要执行的回调函数
     * @param thread 指定在哪个线程执行，-1 表示任意线程
     */
    void schedule(std::function<void()> cb, int thread = -1);
    
    /**
     * @brief 等待所有已调度的任务完成
     * @details 会阻塞当前协程直到所有任务执行完毕
     */
    void waitAll();
private:
    /**
     * @brief 执行具体的工作任务
     * @param cb 要执行的回调函数
     */
    void doWork(std::function<void()> cb);
private:
    /// 批处理大小，控制并发任务数量
    uint32_t m_batchSize;
    /// 是否完成标志
    bool m_finish;
    /// 关联的调度器
    Scheduler* m_scheduler;
    /// 信号量，用于控制并发和同步
    FiberSemaphore m_sem;
};

/**
 * @class WorkerManager
 * @brief 工作管理器：管理多个命名的调度器（Worker）
 * 
 * 负责创建、管理和调度多个命名的工作调度器，支持通过名称获取调度器并向其提交任务。
 * 通常用于应用程序中需要区分不同类型工作线程池的场景（如 IO 线程池、计算线程池等）。
 */
class WorkerManager {
public:
    /**
     * @brief 构造函数
     */
    WorkerManager();
    
    /**
     * @brief 添加一个调度器到管理器
     * @param s 调度器智能指针
     */
    void add(Scheduler::ptr s);
    
    /**
     * @brief 根据名称获取调度器
     * @param name 调度器名称
     * @return Scheduler::ptr 调度器智能指针，未找到返回空指针
     */
    Scheduler::ptr get(const std::string& name);
    
    /**
     * @brief 根据名称获取 IOManager 类型的调度器
     * @param name 调度器名称
     * @return IOManager::ptr IOManager 智能指针，未找到或类型不匹配返回空指针
     */
    IOManager::ptr getAsIOManager(const std::string& name);

    /**
     * @brief 向指定名称的调度器提交单个任务
     * @tparam FiberOrCb 协程或回调函数类型
     * @param name 调度器名称
     * @param fc 协程或回调函数
     * @param thread 指定在哪个线程执行，-1 表示任意线程
     */
    template <class FiberOrCb>
    void schedule(const std::string& name, FiberOrCb fc, int thread = -1);

    /**
     * @brief 向指定名称的调度器批量提交任务
     * @tparam Iter 迭代器类型
     * @param name 调度器名称
     * @param begin 任务列表起始迭代器
     * @param end 任务列表结束迭代器
     */
    template <class Iter>
    void schedule(const std::string& name, Iter begin, Iter end);

    /**
     * @brief 初始化工作管理器（从配置文件读取）
     * @return bool 初始化是否成功
     */
    bool init();
    
    /**
     * @brief 初始化工作管理器（从指定配置）
     * @param v 配置映射表：调度器名称 -> 配置项（线程数、类型等）
     * @return bool 初始化是否成功
     */
    bool init(const std::map<std::string, std::map<std::string, std::string>>& v);
    
    /**
     * @brief 停止所有调度器
     */
    void stop();
    
    /**
     * @brief 判断管理器是否已停止
     * @return bool true 表示已停止，false 表示正在运行
     */
    bool isStoped() const { return m_stop; }
    
    /**
     * @brief 将管理器状态输出到流
     * @param os 输出流
     * @return std::ostream& 输出流引用
     */
    std::ostream& dump(std::ostream& os);

    /**
     * @brief 获取管理的调度器总数
     * @return uint32_t 调度器数量
     */
    uint32_t getCount();
private:
    /// 存储所有调度器：调度器名称 -> 调度器列表
    std::map<std::string, std::vector<Scheduler::ptr>> m_datas;
    /// 停止标志
    bool m_stop;
};

template <class FiberOrCb>
void WorkerManager::schedule(const std::string& name, FiberOrCb fc, int thread) {
    auto x = get(name);
    if (x) {
        x->schedule(fc, thread);
    } else {
        ERROR(LOG_NAME("system")) << "schedule name=" << name
            << " not exists";
    }
}

template <class Iter>
void WorkerManager::schedule(const std::string& name, Iter begin, Iter end) {
    auto x = get(name);
    if (x) {
        x->schedule(begin, end);
    } else {
        ERROR(LOG_NAME("system")) << "schedule name=" << name
            << " not exists";
    }
}

typedef Singleton<WorkerManager> WorkerMgr;

}
