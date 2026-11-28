#include "Schedule.hpp"

#include "../Hook/Hook.hpp"
#include "../Log/Log.hpp"
#include "../Util/Macro.hpp"
#include "../Util/StallDetector.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

// 当前线程的调度器
static thread_local Scheduler* t_scheduler = nullptr;
// 当前线程的主协程
static thread_local Fiber* t_schedule_fiber = nullptr;

Scheduler::FiberAndThread::FiberAndThread(Fiber::ptr f, int thr)
    : fiber(f)
    , thread_id(thr)
{
}

Scheduler::FiberAndThread::FiberAndThread(Fiber::ptr* f, int thr)
    : thread_id(thr)
{
    fiber.swap(*f);
}

Scheduler::FiberAndThread::FiberAndThread(std::function<void()> f, int thr)
    : cb(f)
    , thread_id(thr)
{
}

Scheduler::FiberAndThread::FiberAndThread(std::function<void()>* f, int thr)
    : thread_id(thr)
{
    cb.swap(*f);
}

Scheduler::FiberAndThread::FiberAndThread()
    : thread_id(-1)
{
}

void Scheduler::FiberAndThread::reset()
{
    fiber = nullptr;
    cb = nullptr;
    thread_id = -1;
}

Scheduler::Scheduler(size_t thread_count, bool use_caller, const std::string& name)
    : name_(name)
{
    ASSERT(thread_count > 0);

    // 如果执行调度器的线程也算进来
    if (use_caller) {
        Fiber::GetThis();
        --thread_count; // 没必要多创建一个线程，因为会将当前的执行调度器的线程也算进来
        // 在创建协程调度器的时候这个线程里面应该是没有协程调度器的，再创建一个当然有问题
        ASSERT(GetThis() == nullptr);
        // 设置当前协程调度器
        t_scheduler = this;

        // 非静态成员函数需要传递this指针作为第一个参数，用std::bind()绑定
        root_fiber_.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        // 既然要把当前线程算入，当然要把当前这个线程的名称设置一下
        Thread::SetName(name_);

        // 设置当前线程的主协程
        // 当前线程的主协程，执行run函数的协程，只有默认构造出来的fiber才是主协程
        t_schedule_fiber = root_fiber_.get();
        // 将获取当前线程id
        root_thread_id_ = GetThreadId();
        thread_ids_.push_back(root_thread_id_);
    } else {
        root_thread_id_ = -1;
    }
    thread_count_ = thread_count;
}

Scheduler::~Scheduler()
{
    ASSERT(stopping_);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

// 获取当前的调度器
Scheduler* Scheduler::GetThis()
{
    return t_scheduler;
}

// 获取当前的主协程
Fiber* Scheduler::GetMainFiber()
{
    return t_schedule_fiber;
}

void Scheduler::start()
{
    // DEBUG(logger) << "start()";
    // 多线程操作同一块变量加锁
    std::lock_guard lock(mutex_);
    // 已经启动了
    if (!stopping_) {
        return;
    }
    // 没有停止，已经开启了
    stopping_ = false;
    // 第一次启动，线程池为空
    ASSERT(threads_.empty());
    // 开始构建线程池
    threads_.resize(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i) {
        threads_[i].reset(new Thread(std::bind(&Scheduler::run, this), name_ + "_" + std::to_string(i)));
        thread_ids_.push_back(threads_[i]->getId());
    }
}

// 并不是立马退出，而是需要等待这个调度器里的所有函数的执行完成后再退出
void Scheduler::stop()
{
    DEBUG(logger) << "stop()";
    // 进入stop将自动停止设为true
    auto_stop_ = true;
    // 使用use_caller，并且只有一个线程，而且主协程的状态为结束或者初始化
    if (root_fiber_ && thread_count_ == 0
        && (root_fiber_->getState() == Fiber::TERM || root_fiber_->getState() == Fiber::INIT)) {
        stopping_ = true;

        // 如果达到停止条件直接停止
        if (stopping()) {
            INFO(logger) << this << " scheduler stopped";
            return;
        }
    }
    // 说明这是use_caller的线程，那么这个stop一定是在创建的线程中执行的
    if (root_thread_id_ != -1) {
        ASSERT(GetThis() == this);
    } else {
        ASSERT(GetThis() != this);
    }
    // 否则就多线程的话，每个线程去tickle一下
    stopping_ = true;
    for (size_t i = 0; i < thread_count_; ++i) {
        tickle();
    }
    // 使用了use_caller就多tickle一下
    if (root_fiber_) {
        tickle();
    }
    // 使用use_caller，如果没有达到停止条件，调度器主协程交出执行权，执行run
    if (root_fiber_) {
        if (!stopping()) {
            root_fiber_->call();
        }
    }

    // 建一个新空的线程池，去把当前的线程池的资源换掉，控制权和所有权依然是各自独立的
    std::vector<Thread::ptr> thrs;
    {
        std::lock_guard lock(mutex_);
        thrs.swap(threads_);
    }
    // 等待线程执行完成
    for (auto& i : thrs) {
        i->join();
    }
}

void Scheduler::setThis()
{
    t_scheduler = this;
}

bool Scheduler::stopping()
{
    std::lock_guard lock(mutex_);
    // 当自动停止 && 正在停止 && 任务队列为空 && 活跃线程数为0时
    return auto_stop_ && stopping_ && fibers_.empty() && active_thread_count_ == 0;
}

bool Scheduler::isStopping()
{
    std::lock_guard lock(mutex_);
    return stopping_;
}

// 新创建的线程直接在run上面启动
void Scheduler::run()
{
    DEBUG(logger) << name_ << " run";
    set_hook_enable(true);
    // 设置当前线程调度器
    setThis();
    // 注册到 stall 检测器：事件循环里打心跳，卡死时由 housekeeping 巡检报出来
    StallDetector* stall_detector = StallDetectorMgr::GetInstance();
    stall_detector->registerThread(name_);
    // 如果不是user_caller线程，设置主协程为线程主协程
    if (GetThreadId() != root_thread_id_) {
        t_schedule_fiber = Fiber::GetThis().get();
    }
    // 当任务执行完之后，执行idle
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    FiberAndThread ft;
    while (true) {
        stall_detector->heartbeat();
        ft.reset();
        bool tickle_me = false;
        bool is_active = false;
        // 去协程的消息队列里取出一个协程或者任务，要加锁
        {
            std::lock_guard lock(mutex_);
            auto it = fibers_.begin();
            while (it != fibers_.end()) {
                // 如果当前任务指定的线程不是当前线程，则跳过，并且tickle一下
                if (it->thread_id != -1 && it->thread_id != GetThreadId()) {
                    ++it;
                    tickle_me = true;
                    continue;
                }
                // 确保任务的fiber或者cb存在
                ASSERT(it->fiber || it->cb);
                // 如果fiber正在执行，就跳过
                if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue;
                }
                // 否则就取出任务
                ft = *it;
                fibers_.erase(it++);
                // 正在执行任务的线程+1
                ++active_thread_count_;
                // 正在执行任务
                is_active = true;
                break;
            }
            tickle_me |= (it != fibers_.end());
        }
        // 如果取到了任务，同时有线程不是当前线程的，就tickle一下
        if (tickle_me) {
            tickle();
        }
        // 如果任务是fiber，并且处于可以执行
        if (ft.fiber && (ft.fiber->getState() != Fiber::TERM || ft.fiber->getState() != Fiber::EXCEPT)) {
            // 执行任务
            stall_detector->setFiber(ft.fiber->getId());
            ft.fiber->swapIn();
            // 任务执行完成，活跃线程就-1
            --active_thread_count_;

            // 如果线程状态被设置为Ready，那么将其重新加到任务队列中
            if (ft.fiber->getState() == Fiber::READY) {
                schedule(ft.fiber);
            } else if (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT) {
                ft.fiber->setState(Fiber::HOLD);
            }
            ft.reset();
            // 如果任务是cb
        } else if (ft.cb) {
            // 如果这个fiber存在, 将其放入回调任务函数
            if (cb_fiber) {
                cb_fiber->reset(ft.cb);
                // 不存在就创建一个新的fiber
            } else {
                cb_fiber.reset(new Fiber(ft.cb));
            }
            // 重置数据ft
            ft.reset();
            // 执行回调函数
            stall_detector->setFiber(cb_fiber->getId());
            cb_fiber->swapIn();
            // 任务执行完成，活跃线程就-1
            --active_thread_count_;
            if (cb_fiber->getState() == Fiber::READY) {
                // 重新放入任务队列
                schedule(cb_fiber);
                cb_fiber.reset();
            } else if (cb_fiber->getState() == Fiber::EXCEPT || cb_fiber->getState() == Fiber::TERM) {
                cb_fiber->reset(nullptr);
            } else {
                // 此任务后面还会通过ft.fiber被拉起来
                cb_fiber->setState(Fiber::HOLD);
                // 释放智能指针，调用下一个就重新再new一个
                cb_fiber.reset();
            }
            // 如果没有任务执行
        } else {
            // 如果没有任务且任务任务正在执行
            if (is_active) {
                --active_thread_count_;
                continue;
            }
            // 结束了，真正的结束
            if (idle_fiber->getState() == Fiber::TERM) {
                TRACE(logger) << "idle fiber term";
                break;
            }
            ++idle_thread_count_;
            // 执行idle
            idle_fiber->swapIn();
            // 正在执行idle线程的数量-1
            --idle_thread_count_;
            // 将idle_fiber状态设置为HOLD
            if (idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->setState(Fiber::HOLD);
            }
        }
    }
    stall_detector->unregisterThread();
    // while 循环退出后，idle_fiber 即将析构，但 t_fiber 仍指向它。
    // 将 t_fiber 切回本线程的主协程，避免 ~Fiber() 日志中 GetFiberId() 访问已释放内存。
    if (t_schedule_fiber) {
        Fiber::SetThis(t_schedule_fiber);
    }
    // 工作线程退出前清理 thread_local 协程引用，释放主协程
    if (GetThreadId() != root_thread_id_) {
        Fiber::CleanupThread();
    }
}

void Scheduler::tickle()
{
    INFO(logger) << "tickle";
}

void Scheduler::idle()
{
    INFO(logger) << "idle";
    while (!stopping()) {
        Fiber::YieldToHold();
    }
}

void Scheduler::switchTo(int thread)
{
    ASSERT(Scheduler::GetThis());
    if (Scheduler::GetThis() == this) {
        if (thread == -1 || thread == GetThreadId()) {
            return;
        }
    }
    schedule(Fiber::GetThis(), thread);
    Fiber::YieldToHold();
}

size_t Scheduler::getTaskCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fibers_.size();
}

std::ostream& Scheduler::dump(std::ostream& os)
{
    os << "[Scheduler name=" << name_ << " size=" << thread_count_ << " active_count=" << active_thread_count_
       << " idle_count=" << idle_thread_count_ << " stopping=" << stopping_ << " ]" << std::endl
       << "    ";
    for (size_t i = 0; i < thread_ids_.size(); ++i) {
        if (i) {
            os << ", ";
        }
        os << thread_ids_[i];
    }
    return os;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target)
{
    caller_ = Scheduler::GetThis();
    // 不在调度器线程里时无法切换，直接跳过
    if (target && caller_ && target != caller_) {
        target->switchTo();
    }
}

SchedulerSwitcher::~SchedulerSwitcher()
{
    if (caller_ && Scheduler::GetThis() != caller_) {
        caller_->switchTo();
    }
}

} // namespace chen
