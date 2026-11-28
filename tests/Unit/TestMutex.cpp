/**
 * @file TestMutex.cpp
 * @brief 协程感知的 Mutex / RWMutex 单元测试
 * @author Christins
 * @date 2026-11-28
 * @copyright GPL-3.0
 * @note 用例内自建 IOManager（不监听端口），stop() 会跑完已排队的任务。
 */
#include "../TestHarness.hpp"

#include "src/IOManager/IOManager.hpp"
#include "src/Util/Mutex.hpp"
#include "src/Util/TimeUtil.hpp"

#include <atomic>
#include <stdexcept>
#include <unistd.h>

using chen::IOManager;
using chen::Mutex;
using chen::RWMutex;
using chen::TimeUtil;

namespace {

void UpdatePeak(std::atomic<int>& peak, int value)
{
    int prev = peak.load();
    while (value > prev && !peak.compare_exchange_weak(prev, value)) {
        // 空循环：value 已经是最新读到的 prev
    }
}

/// 自旋等待条件成立（主线程无调度器，usleep 是真正的睡眠）
bool WaitUntil(const std::atomic<bool>& flag, uint64_t timeout_ms = 2000)
{
    const uint64_t start = TimeUtil::GetCurrentMs();
    while (!flag.load()) {
        if (TimeUtil::GetCurrentMs() - start > timeout_ms) {
            return false;
        }
        usleep(1000);
    }
    return true;
}

} // namespace

CHEN_TEST(mutex, excludes_fibers)
{
    IOManager iom(2, false, "mutex_unit");
    Mutex mutex;
    std::atomic<int> inside{0};
    std::atomic<int> peak{0};
    std::atomic<int> finished{0};

    for (int i = 0; i < 4; ++i) {
        iom.schedule([&mutex, &inside, &peak, &finished]() {
            Mutex::Lock lock(mutex);
            const int current = ++inside;
            UpdatePeak(peak, current);
            usleep(2 * 1000);
            --inside;
            ++finished;
        });
    }

    iom.stop();

    CHEN_CHECK_EQ(finished.load(), 4);
    CHEN_CHECK_EQ(peak.load(), 1);
    CHEN_CHECK(!mutex.isLocked());
}

CHEN_TEST(mutex, try_lock_without_scheduler)
{
    Mutex mutex;
    CHEN_CHECK(!mutex.isLocked());
    CHEN_CHECK(mutex.tryLock());
    CHEN_CHECK(mutex.isLocked());
    CHEN_CHECK(!mutex.tryLock());
    mutex.unlock();
    CHEN_CHECK(!mutex.isLocked());
    CHEN_CHECK(mutex.tryLock());
    mutex.unlock();
}

CHEN_TEST(mutex, recursive_lock_throws)
{
    IOManager iom(1, false, "mutex_unit");
    Mutex mutex;
    std::atomic<bool> caught{false};
    std::atomic<bool> unexpected{false};

    iom.schedule([&mutex, &caught, &unexpected]() {
        Mutex::Lock lock(mutex);
        try {
            mutex.lock();
            unexpected = true;
        } catch (const std::logic_error&) {
            caught = true;
        }
    });

    iom.stop();

    CHEN_CHECK(caught.load());
    CHEN_CHECK(!unexpected.load());
    CHEN_CHECK(!mutex.isLocked());
}

CHEN_TEST(mutex, thread_waits_for_fiber)
{
    IOManager iom(1, false, "mutex_unit");
    Mutex mutex;
    std::atomic<bool> fiber_locked{false};

    iom.schedule([&mutex, &fiber_locked]() {
        Mutex::Lock lock(mutex);
        fiber_locked = true;
        usleep(20 * 1000);
    });

    CHEN_CHECK(WaitUntil(fiber_locked));
    CHEN_CHECK(!mutex.tryLock());
    // 主线程等待协程释放：线程走条件变量，不能把调度线程卡死（协程在另一个线程上跑）
    mutex.lock();
    CHEN_CHECK(mutex.isLocked());
    mutex.unlock();

    iom.stop();
    CHEN_CHECK(!mutex.isLocked());
}

CHEN_TEST(rwmutex, readers_share)
{
    IOManager iom(3, false, "mutex_unit");
    RWMutex mutex;
    std::atomic<int> inside{0};
    std::atomic<int> arrived{0};
    std::atomic<int> peak{0};
    std::atomic<bool> stuck{false};
    std::atomic<int> finished{0};
    const int readers = 3;

    for (int i = 0; i < readers; ++i) {
        iom.schedule([&mutex, &inside, &arrived, &peak, &stuck, &finished, readers]() {
            RWMutex::ReadLock lock(mutex);
            UpdatePeak(peak, ++inside);
            ++arrived;
            const uint64_t start = TimeUtil::GetCurrentMs();
            while (arrived.load() < readers && TimeUtil::GetCurrentMs() - start < 2000) {
                usleep(1000);
            }
            if (arrived.load() < readers) {
                stuck = true;
            }
            usleep(1000);
            --inside;
            ++finished;
        });
    }

    iom.stop();

    CHEN_CHECK(!stuck.load());
    CHEN_CHECK_EQ(peak.load(), readers);
    CHEN_CHECK_EQ(finished.load(), readers);
    CHEN_CHECK_EQ(mutex.getReaderCount(), 0);
}

CHEN_TEST(rwmutex, writer_is_exclusive)
{
    IOManager iom(2, false, "mutex_unit");
    RWMutex mutex;
    std::atomic<bool> reader_inside{false};
    std::atomic<bool> writer_ran{false};
    std::atomic<bool> writer_done{false};
    std::atomic<bool> writer_saw_reader{false};

    iom.schedule([&mutex, &reader_inside]() {
        RWMutex::ReadLock lock(mutex);
        reader_inside = true;
        usleep(30 * 1000);
        reader_inside = false;
    });

    CHEN_CHECK(WaitUntil(reader_inside));

    iom.schedule([&mutex, &reader_inside, &writer_ran, &writer_done, &writer_saw_reader]() {
        {
            RWMutex::WriteLock lock(mutex);
            if (reader_inside.load()) {
                writer_saw_reader = true;
            }
            writer_ran = true;
        }
        // 写锁释放之后再置位，外面看到 writer_done 时写锁一定已经归还
        writer_done = true;
    });

    CHEN_CHECK(WaitUntil(writer_ran));
    CHEN_CHECK(WaitUntil(writer_done));
    CHEN_CHECK(!writer_saw_reader.load());
    CHEN_CHECK(!mutex.isWriting());

    iom.stop();
}

CHEN_TEST(rwmutex, write_lock_without_scheduler)
{
    RWMutex mutex;
    CHEN_CHECK_EQ(mutex.getReaderCount(), 0);
    CHEN_CHECK(!mutex.isWriting());
    {
        RWMutex::WriteLock lock(mutex);
        CHEN_CHECK(mutex.isWriting());
    }
    CHEN_CHECK(!mutex.isWriting());
    {
        RWMutex::ReadLock lock(mutex);
        CHEN_CHECK_EQ(mutex.getReaderCount(), 1);
    }
    CHEN_CHECK_EQ(mutex.getReaderCount(), 0);
}

CHEN_TEST_MAIN()
