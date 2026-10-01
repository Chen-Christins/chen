#include "schedule.h"

#include "../hook/hook.h"
#include "../log/log.h"
#include "../util/macro.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

// 当前线程的调度器
static thread_local Scheduler* t_scheduler = nullptr;
// 当前线程的主协程
static thread_local Fiber* t_schedule_fiber = nullptr;

Scheduler::Scheduler(size_t threadCount, bool use_caller, const std::string& name)
    :m_name(name) {
    ASSERT(threadCount > 0);

	// 如果执行调度器的线程也算进来
    if (use_caller) {
        Fiber::GetThis();
        --threadCount;  // 没必要多创建一个线程，因为会将当前的执行调度器的线程也算进来
        // 在创建协程调度器的时候这个线程里面应该是没有协程调度器的，再创建一个当然有问题
        ASSERT(GetThis() == nullptr);
        // 设置当前协程调度器
        t_scheduler = this;

        // 非静态成员函数需要传递this指针作为第一个参数，用std::bind()绑定
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        // 既然要把当前线程算入，当然要把当前这个线程的名称设置一下
        Thread::SetName(m_name);

        // 设置当前线程的主协程
        // 当前线程的主协程，执行run函数的协程，只有默认构造出来的fiber才是主协程
        t_schedule_fiber = m_rootFiber.get();
        // 将获取当前线程id
        m_rootThreadId = GetThreadId();
        m_threadIds.push_back(m_rootThreadId);
    } else {
        m_rootThreadId = -1;
    }
    m_threadCount = threadCount;
}

Scheduler::~Scheduler() {
    ASSERT(m_stopping);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

// 获取当前的调度器
Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

// 获取当前的主协程
Fiber* Scheduler::GetMainFiber() {
    return t_schedule_fiber;
}

void Scheduler::start() {
    // DEBUG(logger) << "start()";
    // 多线程操作同一块变量加锁
    std::lock_guard lock(m_mutex);
	// 已经启动了
    if (!m_stopping) {
        return ;
    }
    // 没有停止，已经开启了
    m_stopping = false;
    // 第一次启动，线程池为空
    ASSERT(m_threads.empty());
	// 开始构建线程池
    m_threads.resize(m_threadCount);
    for (size_t i = 0; i < m_threadCount; ++i) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this)
            ,m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

// 并不是立马退出，而是需要等待这个调度器里的所有函数的执行完成后再退出
void Scheduler::stop() {
    DEBUG(logger) << "stop()";
    // 进入stop将自动停止设为true
    m_autoStop = true;
    // 使用use_caller，并且只有一个线程，而且主协程的状态为结束或者初始化
    if (m_rootFiber && m_threadCount == 0 && (m_rootFiber->getState() == Fiber::TERM
            || m_rootFiber->getState() == Fiber::INIT)) {
        INFO(logger) << this << " scheduler stopped";
        m_stopping = true;

        // 如果达到停止条件直接停止
        if (stopping()) {
            return ;
        }
    }
    // 说明这是use_caller的线程，那么这个stop一定是在创建的线程中执行的
    if (m_rootThreadId != -1) {
        ASSERT(GetThis() == this);
    } else {
        ASSERT(GetThis() != this);
    }
    // 否则就多线程的话，每个线程去tickle一下
    m_stopping = true;
    for (size_t i = 0; i < m_threadCount; ++i) {
        tickle();
    }
    // 使用了use_caller就多tickle一下
    if (m_rootFiber) {
        tickle();
    }
    // 使用use_caller，如果没有达到停止条件，调度器主协程交出执行权，执行run
    if (m_rootFiber) {
        if (!stopping()) {
            m_rootFiber->call();
        }
    }

    // 建一个新空的线程池，去把当前的线程池的资源换掉，控制权和所有权依然是各自独立的
    std::vector<Thread::ptr> thrs;
    {
        std::lock_guard lock(m_mutex);
        thrs.swap(m_threads);
    }
    // 等待线程执行完成
    for (auto& i : thrs) {
        i->join();
    }
}

void Scheduler::setThis() {
    t_scheduler = this;
}

bool Scheduler::stopping() {
    std::lock_guard lock(m_mutex);
    // 当自动停止 && 正在停止 && 任务队列为空 && 活跃线程数为0时
    return m_autoStop && m_stopping
        && m_fibers.empty() && m_activeThreadCount == 0;
}

// 新创建的线程直接在run上面启动
void Scheduler::run() {
    DEBUG(logger) << m_name << " run";
    set_hook_enable(true);
    // 设置当前线程调度器
    setThis();
    // 如果不是user_caller线程，设置主协程为线程主协程
    if (GetThreadId() != m_rootThreadId) {
        t_schedule_fiber = Fiber::GetThis().get();
    }
    // 当任务执行完之后，执行idle
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    FiberAndThread ft;
    while (true) {
        ft.reset();
        bool tickle_me = false;
        bool is_active = false;
        // 去协程的消息队列里取出一个协程或者任务，要加锁
        {
            std::lock_guard lock(m_mutex);
            auto it = m_fibers.begin();
            while (it != m_fibers.end()) {
                // 如果当前任务指定的线程不是当前线程，则跳过，并且tickle一下
                if (it->threadId != -1 && it->threadId != GetThreadId()) {
                    ++it;
                    tickle_me = true;
                    continue ;
                }
                // 确保任务的fiber或者cb存在
                ASSERT(it->fiber || it->cb);
                // 如果fiber正在执行，就跳过
                if (it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue ;
                }
                // 否则就取出任务
                ft = *it;
                m_fibers.erase(it++);
                // 正在执行任务的线程+1
                ++m_activeThreadCount;
                // 正在执行任务
                is_active = true;
                break;
            }
            tickle_me |= (it != m_fibers.end());
        }
        // 如果取到了任务，同时有线程不是当前线程的，就tickle一下
        if (tickle_me) {
            tickle();
        }
        // 如果任务是fiber，并且处于可以执行
        if (ft.fiber && (ft.fiber->getState() != Fiber::TERM || ft.fiber->getState() != Fiber::EXCEPT)) {
            // 执行任务
            ft.fiber->swapIn();
            // 任务执行完成，活跃线程就-1
            --m_activeThreadCount;

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
            cb_fiber->swapIn();
			// 任务执行完成，活跃线程就-1
            --m_activeThreadCount;
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
                --m_activeThreadCount;
                continue ;
            }
            // 结束了，真正的结束
            if (idle_fiber->getState() == Fiber::TERM) {
                TRACE(logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            // 执行idle
            idle_fiber->swapIn();
            // 正在执行idle线程的数量-1
            --m_idleThreadCount;
            // 将idle_fiber状态设置为HOLD
            if (idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->setState(Fiber::HOLD);
            }
        }
    }
    // while 循环退出后，idle_fiber 即将析构，但 t_fiber 仍指向它。
    // 将 t_fiber 切回本线程的主协程，避免 ~Fiber() 日志中 GetFiberId() 访问已释放内存。
    if (t_schedule_fiber) {
        Fiber::SetThis(t_schedule_fiber);
    }
    // 工作线程退出前清理 thread_local 协程引用，释放主协程
    if (GetThreadId() != m_rootThreadId) {
        Fiber::CleanupThread();
    }
}

void Scheduler::tickle() {
    INFO(logger) << "tickle";
}

void Scheduler::idle() {
    INFO(logger) << "idle";
    while (!stopping()) {
        Fiber::YieldToHold();
    }
}

void Scheduler::switchTo(int thread) {
	ASSERT(Scheduler::GetThis());
	if (Scheduler::GetThis() == this) {
		if (thread == -1 || thread == GetThreadId()) {
			return;
		}
	}
	schedule(Fiber::GetThis(), thread);
	Fiber::YieldToHold();
}

std::ostream& Scheduler::dump(std::ostream& os) {
    os << "[Scheduler name=" << m_name
       << " size=" << m_threadCount
       << " active_count=" << m_activeThreadCount
       << " idle_count=" << m_idleThreadCount
       << " stopping=" << m_stopping
       << " ]" << std::endl << "    ";
    for (size_t i = 0; i < m_threadIds.size(); ++i) {
        if (i) {
            os << ", ";
        }
        os << m_threadIds[i];
    }
    return os;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target) {
	m_caller = Scheduler::GetThis();
	if (target) {
		target->switchTo();
	}
}

SchedulerSwitcher::~SchedulerSwitcher() {
	if (m_caller) {
		m_caller->switchTo();
	}
}

}
