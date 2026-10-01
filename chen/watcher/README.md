# FileWatcher — 配置文件热加载

基于 Linux inotify 的配置热加载组件。配置目录下的 `.yml` / `.xml` 文件被修改后，
框架通过事件通知**立即**重新加载该文件，无需重启、无需定时轮询。

## 组成

| 文件 | 作用 |
|------|------|
| `chen/watcher/file_watcher.h/.cc` | inotify 封装，接入 IOManager 的 epoll 事件循环 |
| `chen/config/config.cc` `Config::LoadFromFile()` | 重新加载单个配置文件，触发变更回调 |
| `chen/application.cc` `startConfigWatcher()` | 启动时监听配置目录，变更时调用 `LoadFromFile` |

工作流程：

```
修改 .yml → inotify 事件 → handleEvents 协程（main IOManager）
          → Config::LoadFromFile(该文件) → ConfigVar::setValue
          → 触发所有已注册的 onChange 回调
```

> 注意：热加载只重新加载**发生变更的那个文件**，不是全量重扫目录。
> SIGHUP / `bin/main -s reload` 现在只重载 `.so` 业务模块，配置由 inotify 自动处理。

---

## 业务模块怎么用

### 1. 声明配置项

```cpp
#include "chen/config/config.h"

// 模块加载时注册（键名规则：[a-z0-9_.]+）
static chen::ConfigVar<int>::ptr g_max_conn =
    chen::Config::Lookup("myapp.max_conn", 1000, "最大连接数");
```

### 2. 写配置文件

放在启动参数 `-c` 指定的配置目录下（如 `bin/conf/myapp.yml`）：

```yaml
myapp:
  max_conn: 2000
```

### 3. 注册变更回调（推荐）

```cpp
#include "chen/config/config.h"
#include "chen/module/module.h"
#include "chen/log/log.h"

static chen::Logger::ptr logger = LOG_NAME("system");

static chen::ConfigVar<int>::ptr g_max_conn =
    chen::Config::Lookup("myapp.max_conn", 1000, "最大连接数");

class MyModule : public chen::Module {
public:
    MyModule() : Module("my_module", "1.0.0", "my_module.so") {}

    bool onServerReady() override {
        // 注册回调，保存 id 用于后续移除
        m_listenerId = g_max_conn->addListener(
            [](const int& old_val, const int& new_val) {
                INFO(logger) << "myapp.max_conn: " << old_val << " -> " << new_val;
                // 应用新配置：重建连接池 / 更新限流阈值等
            });
        return true;
    }

    // 热重载：旧模块被替换时，必须移除回调（见下文「安全」）
    bool onDeactivate() override {
        removeListener();
        return true;
    }

    // 优雅关闭：同样移除
    bool onUnload() override {
        removeListener();
        return true;
    }

private:
    void removeListener() {
        if (m_listenerId != 0) {
            g_max_conn->delListener(m_listenerId);
            m_listenerId = 0;
        }
    }

    uint64_t m_listenerId = 0;
};

extern "C" {
chen::Module* CreateModule() { return new MyModule(); }
void DestroyModule(chen::Module* m) { delete m; }
}
```

### 4. 或者按需读取

不需要实时响应变更时，直接在用到的地方读取最新值即可：

```cpp
int n = g_max_conn->getValue();   // 每次都拿到最新值
```

---

## 生命周期与安全（重点）

`addListener` 注册的回调是 `std::function`，其**函数指针指向模块 `.so` 内的代码**。
热重载会 `dlclose` 旧 `.so`，若回调未移除，下次配置变更时调用悬空指针 → **SIGSEGV**。

> 这与 EventBus listener 的规则完全相同：**谁注册，谁移除。**

| 时机 | 操作 |
|------|------|
| `onServerReady()` | `addListener` 注册回调，保存返回的 id |
| `onDeactivate()`（热重载） | `delListener(id)` 移除回调 |
| `onUnload()`（优雅关闭） | `delListener(id)` 移除回调 |

### 回调注意事项

- **旧值语义**：回调触发时 `ConfigVar::m_val` 尚未更新，回调内 `getValue()` 返回的是**旧值**；
  新值通过回调参数 `new_val` 传入。
- **禁止在回调里增删监听**：`setValue` 调用回调时持有共享锁，回调内再调
  `addListener` / `delListener` 会死锁。
- **执行线程**：回调运行在 main IOManager 的 `handleEvents` 协程上（单线程）。
  回调应保持轻量，避免阻塞操作；耗时任务请 `IOManager::GetThis()->schedule(...)` 投递到协程池。
- **仅值变化时触发**：`setValue` 只有在 `new_value != old_value` 时才触发回调。

---

## 触发规则

| 文件操作 | 是否触发重载 |
|----------|--------------|
| 原地保存（write + close） | ✅ `IN_CLOSE_WRITE` |
| 原子替换（vim：写临时文件后 `rename`） | ✅ `IN_MOVED_TO` |
| 新建 `.yml` / `.xml` 并写入 | ✅ |
| 子目录下的配置文件 | ✅（递归监听） |
| 删除配置文件 | ❌ |
| 非 `.yml` / `.xml` 文件 | ❌ |

---

## FileWatcher API（框架层）

```cpp
#include "chen/watcher/file_watcher.h"

auto watcher = std::make_shared<chen::FileWatcher>();
watcher->watchDir("/path/to/conf", [](const std::string& file, uint32_t mask) {
    // file: 发生变更的配置文件绝对路径
    // mask: inotify 事件掩码
    chen::Config::LoadFromFile(file);
});

// 停止监听（关闭 inotify fd、唤醒事件协程）
watcher->stop();
```

| 方法 | 说明 |
|------|------|
| `watchDir(dir, cb)` | 递归监听目录下的配置文件变更 |
| `stop()` | 停止监听 |
| `getFd()` | 获取 inotify 文件描述符 |

> 仅 Linux 支持（`#ifdef __linux__`）。macOS/Windows 下该组件不编译。

---

## 常见问题

**Q：改了配置没生效？**
A：确认文件在配置目录（`-c` 指定）下、扩展名为 `.yml`/`.xml`、键名与 `Config::Lookup` 一致
（`myapp.max_conn` 对应 YAML 的 `myapp: { max_conn: ... }`）。删除文件不会触发重载。

**Q：为什么回调里 `getValue()` 还是旧值？**
A：设计如此。`setValue` 先触发回调再更新内部值，新值通过回调参数获取。

**Q：能动态新增/删除配置项吗？**
A：`Config::Lookup` 对同名键返回同一实例，可在模块加载时注册新键；热重载时新文件里
未知的键会被忽略（不会报错）。
