/**
 * @file file_watcher.h
 * @brief inotify 文件变更监听器
 * @author Christins
 * @date 2026-09-21
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace chen {

class IOManager;

/**
 * @brief 基于 inotify 的文件变更监听器
 * @details 递归监听目录下 .yml/.xml 文件的变更事件，
 *          通过 IOManager 的 epoll 集成实现协程友好的事件驱动。
 *          只监听目录（目录 watch 会携带文件名上报子文件事件），
 *          避免文件被 rename 替换后 watch 失效的问题。
 */
class FileWatcher : public std::enable_shared_from_this<FileWatcher> {
public:
    typedef std::shared_ptr<FileWatcher> ptr;
    typedef std::function<void(const std::string& file, uint32_t events)> Callback;

    FileWatcher();
    ~FileWatcher();

    /**
     * @brief 递归监听目录下的配置文件
     * @param dir 要监听的目录绝对路径
     * @param cb 文件变更回调
     * @return true 成功，false inotify 初始化失败
     */
    bool watchDir(const std::string& dir, Callback cb);

    /**
     * @brief 停止监听
     */
    void stop();

    /**
     * @brief 获取 inotify 文件描述符
     */
    int getFd() const { return m_fd; }

private:
    /**
     * @brief 协程主循环：注册 epoll 事件 → 等待 → 读取 inotify 事件 → 处理
     */
    void handleEvents();

    /**
     * @brief 递归为目录及其子目录添加 inotify watch
     */
    void addWatchRecursive(const std::string& dir);

    /**
     * @brief 判断文件名是否为配置文件
     */
    static bool isConfigFile(const std::string& filename);

private:
    /// inotify 文件描述符
    int m_fd = -1;
    /// 所属的 IO 调度器（用于 stop 时取消事件）
    IOManager* m_iom = nullptr;
    /// 变更回调
    Callback m_callback;
    /// 监听的根目录路径
    std::string m_watchDir;
    /// watch descriptor → 目录路径映射
    std::map<int, std::string> m_wd2path;
    /// 是否已停止
    std::atomic<bool> m_stopped{false};
};

} // namespace chen
