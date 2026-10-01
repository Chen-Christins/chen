/**
 * @file file_watcher.cc
 * @brief inotify 文件变更监听器实现
 * @author Christins
 * @date 2026-09-21
 */
#include "file_watcher.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include <sys/inotify.h>

#include "../fiber/fiber.h"
#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

/// 需要监听的事件：新建、删除、移入、移出、关闭写入（不含 IN_MODIFY，避免读到半截文件）
static constexpr uint32_t WATCH_MASK = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_CLOSE_WRITE;

/// 触发配置重载的事件：写完关闭、原子替换（rename）
static constexpr uint32_t RELOAD_MASK = IN_CLOSE_WRITE | IN_MOVED_TO;

FileWatcher::FileWatcher() {
}

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::isConfigFile(const std::string& filename) {
    if (filename.size() < 4) {
        return false;
    }
    std::string ext = filename.substr(filename.size() - 4);
    return ext == ".yml" || ext == ".xml";
}

bool FileWatcher::watchDir(const std::string& dir, Callback cb) {
    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        ERROR(logger) << "FileWatcher::watchDir inotify_init1 failed: " << strerror(errno);
        return false;
    }

    m_iom = IOManager::GetThis();
    if (!m_iom) {
        ERROR(logger) << "FileWatcher::watchDir no IOManager in current thread";
        close(m_fd);
        m_fd = -1;
        return false;
    }

    m_callback = std::move(cb);
    m_watchDir = dir;

    INFO(logger) << "FileWatcher: watching config dir=" << dir;

    addWatchRecursive(dir);

    // 通过 IOManager 调度事件处理协程，捕获 shared_ptr 避免协程生命周期内对象被销毁
    auto self = shared_from_this();
    m_iom->schedule([self]() {
        self->handleEvents();
    });

    return true;
}

void FileWatcher::stop() {
    if (m_stopped.exchange(true)) {
        return;
    }
    // 先取消 epoll 事件，唤醒可能驻留的协程并归还 pending event 计数
    if (m_iom && m_fd >= 0) {
        m_iom->cancelEvent(m_fd, IOManager::READ);
    }
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
    m_wd2path.clear();
}

void FileWatcher::addWatchRecursive(const std::string& dir) {
    // 目录 watch 会携带 event->name 上报其直接子文件的事件
    int wd = inotify_add_watch(m_fd, dir.c_str(), WATCH_MASK);
    if (wd < 0) {
        WARN(logger) << "FileWatcher: inotify_add_watch dir=" << dir
            << " failed: " << strerror(errno);
        return;
    }
    m_wd2path[wd] = dir;

    DIR* dp = opendir(dir.c_str());
    if (dp == nullptr) {
        return;
    }
    struct dirent* ent = nullptr;
    while ((ent = readdir(dp)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        if (ent->d_type == DT_DIR) {
            addWatchRecursive(dir + "/" + ent->d_name);
        }
    }
    closedir(dp);
}

void FileWatcher::handleEvents() {
    constexpr size_t BUF_SIZE = 4096;
    char buf[BUF_SIZE];

    while (!m_stopped.load(std::memory_order_acquire)) {
        if (m_fd < 0) {
            break;
        }
        // 注册 inotify fd 到 epoll，等待可读事件
        if (m_iom->addEvent(m_fd, IOManager::READ) != 0) {
            ERROR(logger) << "FileWatcher: addEvent failed fd=" << m_fd;
            break;
        }
        Fiber::YieldToHold();

        if (m_stopped.load(std::memory_order_acquire)) {
            break;
        }

        // 读取所有待处理的 inotify 事件（边缘触发需一次读完）
        while (true) {
            ssize_t len = read(m_fd, buf, BUF_SIZE);
            if (len <= 0) {
                break;
            }

            for (char* ptr = buf; ptr < buf + len; ) {
                auto* event = reinterpret_cast<inotify_event*>(ptr);
                size_t event_size = sizeof(inotify_event) + event->len;

                if (event->len > 0) {
                    auto it = m_wd2path.find(event->wd);
                    if (it != m_wd2path.end()) {
                        std::string filepath = it->second + "/" + event->name;

                        if (event->mask & IN_ISDIR) {
                            // 新建/移入子目录 → 递归补充 watch
                            if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                                addWatchRecursive(filepath);
                            }
                        } else if (isConfigFile(event->name) && (event->mask & RELOAD_MASK)) {
                            INFO(logger) << "FileWatcher: config changed file=" << filepath;
                            if (m_callback) {
                                m_callback(filepath, event->mask);
                            }
                        }
                    }
                }

                ptr += event_size;
            }
        }
    }

    INFO(logger) << "FileWatcher: event loop stopped";
}

} // namespace chen
