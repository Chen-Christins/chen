#include "Fiber.hpp"

#include <atomic>

#include <boost/context/fiber_fcontext.hpp>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "../Schedule/Schedule.hpp"
#include "../Util/Macro.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");
// 协程号
static std::atomic<uint64_t> g_fiber_id{0};
// 协程个数
static std::atomic<uint64_t> g_fiber_count{0};

static thread_local Fiber* t_fiber = nullptr;
static thread_local Fiber::ptr t_threadFiber = nullptr;

// 通过配置文件来设置大小
static ConfigVar<uint32_t>::ptr g_fiber_stack_size = Config::Lookup<uint32_t>("fiber.stack_size", 64 * 1024, "fiber stack size");

// 为ucontext_t申请栈空间
class MallocStackAllocator {
public:
    static void* Alloc(size_t size) { return malloc(size); }
    static void Dealloc(void* vp, size_t size) { return free(vp); }
};

using StackAllocator = MallocStackAllocator;

Fiber::Fiber()
{
    state_ = EXEC;
    is_main_ = true;
    SetThis(this);
    ++g_fiber_count;
    DEBUG(logger) << "Fiber::Fiber main";
}

Fiber::Fiber(std::function<void()> task, size_t stacksize, bool use_caller)
    : id_(++g_fiber_id)
    , task_(std::move(task))
{
    ++g_fiber_count;
    stacksize_ = stacksize ? stacksize : g_fiber_stack_size->getValue();

    // 申请一段栈空间
    stack_ = StackAllocator::Alloc(stacksize_);
    if (!stack_) {
        ASSERT_MSG(false, "stack alloc");
    }

    char* stack_top = (char*)stack_ + stacksize_;
    if (!use_caller) {
        ctx_ = boost::context::detail::make_fcontext(stack_top, stacksize_, &Fiber::MainFunc);
    } else {
        ctx_ = boost::context::detail::make_fcontext(stack_top, stacksize_, &Fiber::CallMainFunc);
    }
    caller_ = nullptr;

    DEBUG(logger) << "Fiber::Fiber id=" << id_;
}

Fiber::~Fiber()
{
    --g_fiber_count;
    if (stack_) {
        ASSERT(state_ == TERM || state_ == EXCEPT || state_ == INIT);
        StackAllocator::Dealloc(stack_, stacksize_);
        // 如果 t_fiber 恰好指向当前正被析构的 fiber，先清掉
        // 避免后续 fiber 析构时通过 GetFiberId() 访问已释放内存
        if (t_fiber == this) {
            SetThis(nullptr);
        }
    } else {
        ASSERT(!task_);
        ASSERT(state_ == EXEC);
        // 主协程（无栈）析构时无条件清 t_fiber
        // 此时 t_fiber 可能指向已释放的其他 fiber（如 root fiber）
        SetThis(nullptr);
    }
    DEBUG(logger) << "Fiber::~Fiber id=" << id_ << " total=" << g_fiber_count;
}
// 重置协程函数，并且重新设置状态
void Fiber::reset(std::function<void()> task)
{
    ASSERT(stack_);
    ASSERT(state_ == TERM || state_ == EXCEPT || state_ == INIT);
    task_ = std::move(task);
    char* stack_top = (char*)stack_ + stacksize_;
    ctx_ = boost::context::detail::make_fcontext(stack_top, stacksize_, &Fiber::MainFunc);
    caller_ = nullptr;
    state_ = INIT;
}

// 切换到当前协程执行
void Fiber::swapIn()
{
    // DEBUG(logger) << "Fiber::swapIn id=" << id_ << " total=" << g_fiber_count;
    SetThis(this);
    ASSERT(state_ != EXEC);
    state_ = EXEC;
    boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(ctx_, this);
    ctx_ = t.fctx;
}

void Fiber::swapOut()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    SetThis(Scheduler::GetMainFiber());
    boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(cur->caller_, this);
    cur->caller_ = t.fctx;
}

void Fiber::call()
{
    SetThis(this);
    state_ = EXEC;
    boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(ctx_, this);
    ctx_ = t.fctx;
}

void Fiber::back()
{
    SetThis(t_threadFiber.get());
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    boost::context::detail::transfer_t t = boost::context::detail::jump_fcontext(cur->caller_, this);
    cur->caller_ = t.fctx;
}

// static
void Fiber::SetThis(Fiber* f)
{
    t_fiber = f;
}

Fiber::ptr Fiber::GetThis()
{
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    // 为工作线程创建一个有效上下文的主协程
    // 这个主协程需要有有效的m_ctx用于jump_fcontext恢复
    Fiber::ptr main_fiber(new Fiber());
    t_fiber = main_fiber.get();
    t_threadFiber = main_fiber;
    return t_fiber->shared_from_this();
}

void Fiber::YieldToReady()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    // 修复竞态条件：协程可能已经被调度器改变了状态
    // 这种情况下，我们检查状态是否合理，然后设置为READY
    if (cur->state_ == EXEC || cur->state_ == HOLD) {
        cur->state_ = READY;
    } else if (cur->state_ != READY) {
        // 如果协程状态异常，报错
        ASSERT_MSG(false, "Unexpected fiber state: " << cur->state_);
    }
    cur->swapOut();
}

void Fiber::YieldToHold()
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    // 修复竞态条件：协程可能已经被调度器设置为HOLD状态
    // 这种情况下，我们只需要确保协程状态是有效的，然后让出CPU即可
    if (cur->state_ == EXEC) {
        cur->state_ = HOLD;
    } else if (cur->state_ != HOLD) {
        // 如果协程状态既不是EXEC也不是HOLD，说明状态异常
        ASSERT_MSG(false, "Unexpected fiber state: " << cur->state_);
    }
    cur->swapOut();
}

uint64_t Fiber::TotalFibers()
{
    return g_fiber_count;
}

// 不在主协程上调度
void Fiber::MainFunc(boost::context::detail::transfer_t t)
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    cur->caller_ = t.fctx;
    try {
        cur->task_();
        cur->task_ = nullptr;
        cur->state_ = TERM;
    } catch (std::exception& e) {
        cur->state_ = EXCEPT;
        ERROR(logger) << "Fiber Except: " << e.what() << " fiber_id" << cur->getId() << std::endl << BacktraceToString();
    } catch (...) {
        cur->state_ = EXCEPT;
        ERROR(logger) << "Fiber Except"
                      << " fiber_id" << cur->getId() << std::endl
                      << BacktraceToString();
    }

    auto raw_ptr = cur.get();
    auto caller = cur->caller_;
    cur.reset();
    boost::context::detail::jump_fcontext(caller, nullptr);

    ASSERT_MSG(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

// 在主协程上调度
void Fiber::CallMainFunc(boost::context::detail::transfer_t t)
{
    Fiber::ptr cur = GetThis();
    ASSERT(cur);
    cur->caller_ = t.fctx;
    try {
        cur->task_();
        cur->task_ = nullptr;
        cur->state_ = TERM;
    } catch (std::exception& e) {
        cur->state_ = EXCEPT;
        ERROR(logger) << "Fiber Except: " << e.what() << " fiber_id" << cur->getId() << std::endl << BacktraceToString();
    } catch (...) {
        cur->state_ = EXCEPT;
        ERROR(logger) << "Fiber Except"
                      << " fiber_id" << cur->getId() << std::endl
                      << BacktraceToString();
    }

    auto raw_ptr = cur.get();
    auto caller = cur->caller_;
    cur.reset();
    boost::context::detail::jump_fcontext(caller, nullptr);

    ASSERT_MSG(false, "never reach fiber_id=" + std::to_string(raw_ptr->getId()));
}

uint64_t Fiber::GetFiberId()
{
    if (t_fiber) {
        return t_fiber->getId();
    }
    return 0;
}

bool Fiber::CanYield()
{
    // 不在调度器线程里，没有可切换的目标
    if (!Scheduler::GetThis()) {
        return false;
    }
    Fiber* cur = t_fiber;
    if (!cur) {
        return false;
    }
    // 线程主协程：caller_ 为空，Yield 出去就回不来了
    if (cur->is_main_) {
        return false;
    }
    // 调度器 root 协程（use_caller 时执行 run 循环的协程）同样不能 Yield
    if (cur == Scheduler::GetMainFiber()) {
        return false;
    }
    return true;
}

void Fiber::CleanupThread()
{
    t_fiber = nullptr;
    t_threadFiber.reset();
}

} // namespace chen
