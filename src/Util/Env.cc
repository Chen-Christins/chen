#include "Env.hpp"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>

#include <pthread.h>
#include <unistd.h>

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "FSUtil.hpp"
#include "Macro.hpp"

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

    exe_ = path;

    auto pos = exe_.find_last_of("/");
    cwd_ = exe_.substr(0, pos) + "/";

    program_ = argv[0];
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
    std::unique_lock lock(mutex_);
    args_[key] = val;
}

bool Env::has(const std::string& key)
{
    std::shared_lock lock(mutex_);
    auto it = args_.find(key);
    return it != args_.end();
}

void Env::del(const std::string& key)
{
    std::unique_lock lock(mutex_);
    args_.erase(key);
}

std::string Env::get(const std::string& key, const std::string& default_val)
{
    std::shared_lock lock(mutex_);
    auto it = args_.find(key);
    return it != args_.end() ? it->second : default_val;
}

void Env::addHelp(const std::string& key, const std::string& desc)
{
    removeHelp(key);
    std::unique_lock lock(mutex_);
    helps_.push_back({key, desc});
}

void Env::removeHelp(const std::string& key)
{
    std::unique_lock lock(mutex_);
    for (auto it = helps_.begin(); it != helps_.end();) {
        if (it->first == key) {
            it = helps_.erase(it);
        } else {
            ++it;
        }
    }
}

void Env::printHelp()
{
    std::shared_lock lock(mutex_);
    std::cout << "Usage: " << program_ << " [options]" << std::endl;
    for (auto& [key, desc] : helps_) {
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
    return cwd_ + path;
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
    if (title_buf_) {
        return;
    }

    title_default_ = FSUtil::Basename(exe_);
    if (title_default_.empty()) {
        title_default_ = FSUtil::Basename(argv[0]);
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

    title_buf_ = argv[0];
    title_max_ = static_cast<std::size_t>(end - title_buf_);
    title_ = title_default_;
}

std::string Env::buildProcTitleLocked() const
{
    const std::string& base = title_base_.empty() ? title_default_ : title_base_;
    if (title_role_.empty()) {
        return base;
    }
    if (base.empty()) {
        return title_role_;
    }
    return base + ": " + title_role_;
}

void Env::writeProcTitleLocked(const std::string& title)
{
    title_ = title;
    if (!title_buf_ || title_max_ == 0) {
        return;
    }

    // 至少保留一个字节放 '\0'，否则 ps 会读到相邻内存
    std::size_t len = title.size();
    if (len >= title_max_) {
        len = title_max_ - 1;
    }
    memcpy(title_buf_, title.data(), len);
    memset(title_buf_ + len, 0, title_max_ - len);
    title_.resize(len);
}

void Env::setProcTitleBase(const std::string& name)
{
    std::unique_lock lock(mutex_);
    title_base_ = name;
    writeProcTitleLocked(buildProcTitleLocked());
}

void Env::setProcTitleRole(const std::string& role)
{
    std::unique_lock lock(mutex_);
    title_role_ = role;
    writeProcTitleLocked(buildProcTitleLocked());
}

void Env::setProcTitle(const std::string& title)
{
    std::unique_lock lock(mutex_);
    writeProcTitleLocked(title);
}

std::string Env::getProcTitle()
{
    std::shared_lock lock(mutex_);
    return title_;
}

void Env::setProcComm(const std::string& name)
{
    // 内核会把线程名截断到 15 字节（不含 '\0'）
    std::string value = name.substr(0, 15);
    pthread_setname_np(pthread_self(), value.c_str());
}

} // namespace chen
