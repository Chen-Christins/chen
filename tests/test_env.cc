#include "chen/util/env.h"
#include <iostream>
#include <fstream>
#include <unistd.h>

struct A {
    A() {
        std::ifstream ifs("/proc/" + std::to_string(getpid()) + "/cmdline", std::ios::binary);
        std::string content;
        content.resize(4096);

        ifs.read(&content[0], content.size());
        content.resize(ifs.gcount());

        for (size_t i = 0; i < content.size(); ++i) {
            std::cout << i << " - " << content[i] << " - " << (int)content[i] << std::endl;
        }
        std::cout << content << std::endl;
    }
};

A a;

static std::string ReadFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return "";
    }
    std::string content;
    content.resize(4096);
    ifs.read(&content[0], content.size());
    content.resize(ifs.gcount());
    return content;
}

/// 读取 /proc/self/cmdline 的第一个字段
static std::string ReadCmdlineTitle()
{
    std::string content = ReadFile("/proc/self/cmdline");
    return content.substr(0, content.find('\0'));
}

/// 读取 /proc/self/comm 并去掉换行
static std::string ReadComm()
{
    std::string content = ReadFile("/proc/self/comm");
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
        content.pop_back();
    }
    return content;
}

static int g_failed = 0;

static void Check(bool ok, const std::string& what, const std::string& expect = "", const std::string& actual = "")
{
    if (ok) {
        std::cout << "[PASS] " << what << std::endl;
        return;
    }
    ++g_failed;
    std::cout << "[FAIL] " << what;
    if (!expect.empty() || !actual.empty()) {
        std::cout << " expect=[" << expect << "] actual=[" << actual << "]";
    }
    std::cout << std::endl;
}

int main(int argc, char** argv) {

    chen::EnvMgr::GetInstance()->addHelp("s", "start with the terminal");
    chen::EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    chen::EnvMgr::GetInstance()->addHelp("p", "print help");
    if(!chen::EnvMgr::GetInstance()->init(argc, argv)) {
        chen::EnvMgr::GetInstance()->printHelp();
        return 0;
    }

    std::cout << "exe=" << chen::EnvMgr::GetInstance()->getExe() << std::endl;
    std::cout << "cwd=" << chen::EnvMgr::GetInstance()->getCwd() << std::endl;

    std::cout << "PATH=" << chen::EnvMgr::GetInstance()->getEnv("HADOOP_HOME", "xxxx") << std::endl;

    std::cout << "set Env " << chen::EnvMgr::GetInstance()->setEnv("TEST", "1") << std::endl;
    std::cout << "TEST=" << chen::EnvMgr::GetInstance()->getEnv("TEST", "xxxx") << std::endl;

    // ── 进程标题 ──
    auto env = chen::EnvMgr::GetInstance();
    const std::string default_title = env->getProcTitle();
    Check(!default_title.empty(), "init 后默认标题非空", "non-empty", default_title);

    env->setProcTitleBase("chen-test");
    env->setProcTitleRole("worker process");
    Check(env->getProcTitle() == "chen-test: worker process", "base + role", "chen-test: worker process", env->getProcTitle());
    Check(ReadCmdlineTitle() == env->getProcTitle(), "cmdline 与 getProcTitle 一致", env->getProcTitle(), ReadCmdlineTitle());

    // 超长标题：验证 environ 区域被复用，而不是截断到原始 argv 长度
    const std::string long_title(256, 'T');
    env->setProcTitle(long_title);
    Check(env->getProcTitle() == long_title, "超长标题未被截断到 argv 长度", long_title, env->getProcTitle());
    Check(ReadCmdlineTitle() == long_title, "超长标题写入 cmdline", long_title, ReadCmdlineTitle());

    // comm 由内核截断到 15 字节
    env->setProcComm("chen-test-worker-1234567890");
    Check(ReadComm() == "chen-test-worke", "setProcComm 截断到 15 字节", "chen-test-worke", ReadComm());

    // 标题覆写后环境变量仍然可读（environ 已整体搬到堆上）
    Check(!env->getEnv("PATH").empty(), "标题写入后环境变量仍可读");

    // 恢复默认基础名和角色，避免影响进程后续输出
    env->setProcTitleBase("");
    env->setProcTitleRole("");
    Check(env->getProcTitle() == default_title, "恢复默认标题", default_title, env->getProcTitle());

    if (g_failed) {
        std::cout << g_failed << " check(s) failed" << std::endl;
        return 1;
    }

    if (chen::EnvMgr::GetInstance()->has("p")) {
        chen::EnvMgr::GetInstance()->printHelp();
    }

    return 0;
}