/**
 * @file test_file_watcher.cc
 * @brief FileWatcher inotify 热加载测试
 * @author Christins
 * @date 2026-09-21
 */
#ifdef __linux__

#include "chen/config/config.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/util/fs_util.h"
#include "chen/watcher/file_watcher.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

static chen::Logger::ptr logger = LOG_ROOT();

static chen::ConfigVar<int>::ptr g_port =
    chen::Config::Lookup("hotreload.test.port", 1000, "hot reload test port");

static std::atomic<int> g_count{0};
static int g_failures = 0;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (cond) {                                             \
            INFO(logger) << "[PASS] " << msg;                   \
        } else {                                                \
            ++g_failures;                                       \
            ERROR(logger) << "[FAIL] " << msg;                  \
        }                                                       \
    } while (0)

static void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::trunc);
    ofs << content;
    ofs.close();
}

static void WaitMs(chen::IOManager* iom, uint64_t ms) {
    auto fiber = chen::Fiber::GetThis();
    iom->addTimer(ms, [iom, fiber]() { iom->schedule(fiber, -1); });
    chen::Fiber::YieldToHold();
}

int main(int argc, char** argv) {
    char tmpl[] = "/tmp/chen_watcher_test_XXXXXX";
    char* tmp = mkdtemp(tmpl);
    if (tmp == nullptr) {
        ERROR(logger) << "mkdtemp failed";
        return 1;
    }
    std::string dir = tmp;
    std::string conf = dir + "/test.yml";
    std::string subdir = dir + "/sub";
    std::string subconf = subdir + "/extra.yml";
    std::string txtfile = dir + "/note.txt";
    std::string tmpfile = dir + "/test.yml.tmp";

    WriteFile(conf, "hotreload.test.port: 1000\n");

    {
        chen::IOManager iom(1, true, "watcher_test");
        chen::FileWatcher::ptr watcher(new chen::FileWatcher());
        watcher->watchDir(dir, [](const std::string& file, uint32_t mask) {
            ++g_count;
            INFO(logger) << "callback file=" << file << " mask=0x" << std::hex << mask;
            chen::Config::LoadFromFile(file);
        });

        iom.schedule([&iom, watcher, &conf, &subdir, &subconf, &txtfile, &tmpfile]() {
            // 1. 原地修改（write + close → IN_CLOSE_WRITE）
            WriteFile(conf, "hotreload.test.port: 2000\n");
            WaitMs(&iom, 1000);
            CHECK(g_port->getValue() == 2000, "in-place modify triggers reload");

            // 2. 原子替换（write tmp + rename → IN_MOVED_TO），模拟 vim 保存
            WriteFile(tmpfile, "hotreload.test.port: 3000\n");
            rename(tmpfile.c_str(), conf.c_str());
            WaitMs(&iom, 1000);
            CHECK(g_port->getValue() == 3000, "atomic rename triggers reload");

            // 3. 子目录递归监听
            chen::FSUtil::Mkdir(subdir);
            WaitMs(&iom, 1000);  // 等子目录 watch 建立
            WriteFile(subconf, "hotreload.test.port: 4000\n");
            WaitMs(&iom, 1000);
            CHECK(g_port->getValue() == 4000, "recursive subdir triggers reload");

            // 4. 非配置文件应被忽略
            int before = g_count.load();
            WriteFile(txtfile, "hello\n");
            WaitMs(&iom, 1000);
            CHECK(g_count.load() == before, "non-config file ignored");

            // 5. 删除配置文件不应崩溃，且值不变
            chen::FSUtil::Unlink(conf, true);
            WaitMs(&iom, 1000);
            CHECK(g_port->getValue() == 4000, "delete does not corrupt config");

            // 停止 watcher，让事件循环退出
            watcher->stop();
        });
    }

    std::filesystem::remove_all(dir);

    if (g_failures == 0) {
        INFO(logger) << "ALL TESTS PASSED";
        return 0;
    }
    ERROR(logger) << g_failures << " TEST(S) FAILED";
    return 1;
}

#else

#include <iostream>
int main() {
    std::cout << "FileWatcher test skipped: not Linux" << std::endl;
    return 0;
}

#endif
