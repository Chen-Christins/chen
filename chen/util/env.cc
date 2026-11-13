#include "env.h"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>

#include <pthread.h>
#include <unistd.h>

#include "../config/config.h"
#include "../log/log.h"
#include "fs_util.h"
#include "macro.h"

extern char** environ;

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

bool Env::init(int argc, char** argv)
{
    char link[1024] = {0};
    char path[1024] = {0};
    sprintf(link, "/proc/%d/exe", getpid());

    ssize_t num = readlink(link, path, sizeof(path));
    ASSERT(num != -1);

    m_exe = path;

    auto pos = m_exe.find_last_of("/");
    m_cwd = m_exe.substr(0, pos) + "/";

    m_program = argv[0];
    initProcTitle(argc, argv);

    const char* now_key = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            const char* key = argv[i];
            while (*key == '-') {
                ++key;
            }
            if (*key != '\0') {
                if (now_key) {
                    add(now_key, "");
                }
                now_key = key;
            } else {
                ERROR(logger) << "invalid arg idx=" << i << " val=" << argv[i];
                return false;
            }
        } else {
            if (now_key) {
                add(now_key, argv[i]);
                now_key = nullptr;
            } else {
                ERROR(logger) << "invalid arg idx=" << i << " val=" << argv[i];
                return false;
            }
        }
    }
    if (now_key) {
        add(now_key, "");
    }
    return true;
}

void Env::add(const std::string& key, const std::string& val)
{
    std::unique_lock lock(m_mutex);
    m_args[key] = val;
}

bool Env::has(const std::string& key)
{
    std::shared_lock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Env::del(const std::string& key)
{
    std::unique_lock lock(m_mutex);
    m_args.erase(key);
}

std::string Env::get(const std::string& key, const std::string& default_val)
{
    std::shared_lock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_val;
}

void Env::addHelp(const std::string& key, const std::string& desc)
{
    removeHelp(key);
    std::unique_lock lock(m_mutex);
    m_helps.push_back({key, desc});
}

void Env::removeHelp(const std::string& key)
{
    std::unique_lock lock(m_mutex);
    for (auto it = m_helps.begin(); it != m_helps.end();) {
        if (it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

void Env::printHelp()
{
    std::shared_lock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for (auto& [key, desc] : m_helps) {
        std::cout << std::setw(5) << "-" << key << " : " << desc << std::endl;
    }
}

bool Env::setEnv(const std::string& key, const std::string& val)
{
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Env::getEnv(const std::string& key, const std::string& default_val)
{
    const char* v = getenv(key.c_str());
    return v ? v : default_val;
}

std::string Env::getAbsolutePath(const std::string& path) const
{
    if (path.empty()) {
        return "/";
    }
    if (path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

std::string Env::getConfigPath()
{
    return getAbsolutePath(get("c", "conf"));
}

std::string Env::getAbsoluteWorkPath(const std::string& path) const
{
    if (path.empty()) {
        return "/";
    }
    if (path[0] == '/') {
        return path;
    }
    static ConfigVar<std::string>::ptr g_server_work_path = Config::Lookup<std::string>("server.work_path");
    return g_server_work_path->getValue();
}

void Env::initProcTitle(int argc, char** argv)
{
    if (argc <= 0 || !argv || !argv[0] || !argv[argc - 1]) {
        return;
    }
    if (m_titleBuf) {
        return;
    }

    m_titleDefault = FSUtil::Basename(m_exe);
    if (m_titleDefault.empty()) {
        m_titleDefault = FSUtil::Basename(argv[0]);
    }

    char* end = argv[argc - 1] + strlen(argv[argc - 1]) + 1;

    // Linux 初始栈上 argv 与 environ 的字符串紧邻存放，可以借用 environ 的空间放更长的标题。
    // 先把所有环境变量字符串整体复制到堆上，再让 environ 指向副本，原区域即可安全覆写；
    // 若两者不连续（例如已被 setenv 重排），则退化为只使用 argv 区域。
    bool contiguous = true;
    char* env_end = end;
    for (char** e = environ; e && *e; ++e) {
        if (env_end != *e) {
            contiguous = false;
            break;
        }
        env_end += strlen(*e) + 1;
    }

    if (contiguous && env_end > end) {
        std::size_t total = static_cast<std::size_t>(env_end - end);
        // 必须用 malloc：glibc 的 setenv 可能对 environ 数组做 realloc，不能与 new[] 混用
        char* copy = static_cast<char*>(std::malloc(total));
        if (copy) {
            char* p = copy;
            for (char** e = environ; e && *e; ++e) {
                std::size_t len = strlen(*e) + 1;
                memcpy(p, *e, len);
                *e = p;
                p += len;
            }
            end = env_end;
        }
    }

    m_titleBuf = argv[0];
    m_titleMax = static_cast<std::size_t>(end - m_titleBuf);
    m_title = m_titleDefault;
}

std::string Env::buildProcTitleLocked() const
{
    const std::string& base = m_titleBase.empty() ? m_titleDefault : m_titleBase;
    if (m_titleRole.empty()) {
        return base;
    }
    if (base.empty()) {
        return m_titleRole;
    }
    return base + ": " + m_titleRole;
}

void Env::writeProcTitleLocked(const std::string& title)
{
    m_title = title;
    if (!m_titleBuf || m_titleMax == 0) {
        return;
    }

    // 至少保留一个字节放 '\0'，否则 ps 会读到相邻内存
    std::size_t len = title.size();
    if (len >= m_titleMax) {
        len = m_titleMax - 1;
    }
    memcpy(m_titleBuf, title.data(), len);
    memset(m_titleBuf + len, 0, m_titleMax - len);
    m_title.resize(len);
}

void Env::setProcTitleBase(const std::string& name)
{
    std::unique_lock lock(m_mutex);
    m_titleBase = name;
    writeProcTitleLocked(buildProcTitleLocked());
}

void Env::setProcTitleRole(const std::string& role)
{
    std::unique_lock lock(m_mutex);
    m_titleRole = role;
    writeProcTitleLocked(buildProcTitleLocked());
}

void Env::setProcTitle(const std::string& title)
{
    std::unique_lock lock(m_mutex);
    writeProcTitleLocked(title);
}

std::string Env::getProcTitle()
{
    std::shared_lock lock(m_mutex);
    return m_title;
}

void Env::setProcComm(const std::string& name)
{
    // 内核会把线程名截断到 15 字节（不含 '\0'）
    std::string value = name.substr(0, 15);
    pthread_setname_np(pthread_self(), value.c_str());
}

} // namespace chen
