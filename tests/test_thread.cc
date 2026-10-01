#include "chen/thread/thread.h"
#include "chen/log/log.h"
#include <mutex>
#include "chen/config/config.h"
#include <shared_mutex>

static chen::Logger::ptr logger = LOG_ROOT();

static int count = 0;

std::mutex mtx;

void fun1() {
    INFO(logger) << "name: " << chen::Thread::GetName()
        << " this.name: " << chen::Thread::GetThis()->getName()
        << " id: " << chen::GetThreadId()
        << " this.id: " << chen::Thread::GetThis()->getId();

    for (int i = 0; i < 1000000; ++i) {
        std::unique_lock locker(mtx);
        ++count;
    }
}

void fun2() {
    while (true) {
        INFO(logger) << "=====================================";
    }
}

void fun3() {
    while (true) {
        INFO(logger) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

int main(int argc, char** argv) {
    INFO(logger) << "thread test begin";
    // YAML::Node root = YAML::LoadFile("/home/chen/workspace/chen/bin/conf/log_thread.yml");
    // chen::Config::LoadFromYaml(root);

    std::vector<chen::Thread::ptr> thrs;
    for (int i = 0; i < 5; ++i) {
        chen::Thread::ptr thr(new chen::Thread(&fun1, "name_" + std::to_string(i)));
        // chen::Thread::ptr thr2(new chen::Thread(&fun3, "fun3"));
        
        thrs.push_back(thr);
        // thrs.push_back(thr2);
    }

    for (size_t i = 0; i < thrs.size(); ++i) {
        thrs[i]->join();
    }

    chen::Thread::SetName(std::to_string(1));
    INFO(logger) << "thread test end";
    INFO(logger) << "count=" << count;
    return 0;
}