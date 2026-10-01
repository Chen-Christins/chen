# Chen — 高性能 C++ 协程服务器框架

基于 C++20 协程（Boost.Context）构建的异步网络服务器框架，提供 Fiber 调度、I/O 多路复用（epoll）、模块热重载、RPC、HTTP/2、WebSocket、SSE、Game 协议等完整的服务器基础设施。

## 架构概览

```
┌──────────────────────────────────────────────────────────┐
│                     Application                          │
│  ┌───────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ TcpServer     │  │  HttpServer  │  │  RpcServer    │  │
│  │ (game/generic)│  │  (HTTP/2/WS) │  │  (template)   │  │
│  └──────┬────────┘  └──────┬───────┘  └──────┬────────┘  │
│         │                  │                 │           │
│  ┌──────┴──────────────────┴─────────────────┴────────┐  │
│  │                 IOManager (epoll)                  │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────┐      │  │
│  │  │ accept   │  │   io     │  │    tick      │      │  │
│  │  │ worker   │  │  worker  │  │   worker     │      │  │
│  │  └──────────┘  └──────────┘  └──────────────┘      │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │              Scheduler (Fiber)                     │  │
│  │  M:N 协程调度 — Fiber::swapIn/swapOut               │  │
│  └────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────┐  │
│  │              Module Manager                        │  │
│  │     动态加载 .so → 热重载 → 优雅关闭                   │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

## 核心特性

### 协程与调度

- **M:N 协程模型**：基于 Boost.Context 的 `jump_fcontext` 实现用户态上下文切换
- **Fiber**：每个协程独立栈空间（默认 128KB），支持 `swapIn`/`swapOut`/`yield`
- **Scheduler**：协程调度器，支持多线程 worker，任务队列 + tickle 唤醒机制
- **IOManager**：epoll 驱动的 I/O 调度器，空闲时 `epoll_wait`，有任务时通过 pipe tickle 唤醒

### 模块系统

- **动态加载**：`.so` 共享库热插拔，`dlopen`/`dlclose` 生命周期管理
- **热重载**：`kill -HUP` 或 `-s reload` 触发。先 `dlopen` 新模块，`prepareDispatch()` / `commitDispatch()` 原子切换流量（无 404 窗口），旧模块 `onDeactivate()` 后 `dlclose` 真正卸载
- **配置热加载**：inotify 监听配置目录，`.yml` / `.xml` 变更后立即重载对应文件并触发变更回调（见 [`chen/watcher/README.md`](chen/watcher/README.md)）
- **生命周期回调**：
  ```
  启动:    onLoad → onServerReady → onServerUp → [onTick 周期]
  热重载:  新模块 onLoad → onServerReady → commitDispatch → onServerUp
           旧模块 onDeactivate → dlclose
  关机:    onDeactivate → onUnload
  ```
- **优雅关闭**：停止 accept → `closeAllClients()` 关闭存量连接 → drain 排空 → 停 tick → `onDeactivate` → `onUnload` → 停 Worker → 清 pid

### 网络协议支持

| 协议 | 说明 |
|------|------|
| TCP | 通用 TCP 服务器，自定义协议支持 |
| UDP | 独立 UDP 服务器，`recvfrom` 数据报循环 |
| Rock | 私有二进制协议（magic + version + flag + length） |
| HTTP/1.1 | HTTP 请求解析、路由、中间件 |
| HTTP/2 | 基于 nghttp2 的 HTTP/2 服务器 |
| WebSocket | WS 协议升级与帧处理 |
| SSE | Server-Sent Events 流式推送 |
| RPC | 模板驱动的 RPC 框架，自动序列化/反序列化，无需 IDL；支持同步与回调式异步调用 |
| Game | 自定义二进制游戏协议（magic + cmd + seq + len） |

### 定时器

- **Timer**：基于时间堆的定时器，支持一次性/循环定时器、条件定时器
- **精度**：毫秒级，自动检测系统时间回拨
- **Tick**：模块可声明 `getTickIntervalMs()`，框架按周期调用 `onTick()`

### 数据库

- **ORM**：自研轻量 ORM，支持 SQLite/MySQL，参数化查询
- **Redis**：基于 hiredis 的异步 Redis 客户端
- **FoxThread**：独立数据库线程，避免阻塞 I/O（懒启动）

数据库/缓存的接入与生命周期由业务 module 决定，配置与用法见
[`docs/db-configuration.md`](docs/db-configuration.md)。

## 构建

### 依赖

- C++20 编译器（GCC 13+ / Clang 17+）
- CMake 3.22+
- Boost.Context
- OpenSSL
- Protobuf
- nghttp2
- yaml-cpp
- jsoncpp
- SQLite3 / MySQL
- hiredis
- Ragel（HTTP 解析器生成）

### 编译

```bash
cmake -B build
cmake --build build -j$(nproc)

# 开启 AddressSanitizer
cmake -B build -DENABLE_ASAN=ON
cmake --build build -j$(nproc)
```

### 目录结构

```
chen/
├── chen/                   # 核心库
│   ├── fiber/              # 协程
│   ├── schedule/           # 调度器
│   ├── iomanager/          # I/O 管理器
│   ├── timer/              # 定时器
│   ├── tcp/                # TCP 服务器
│   ├── udp/                # UDP 服务器
│   ├── http/               # HTTP/HTTP2/WS/SSE
│   ├── rpc/                # RPC 框架（同步 + 异步）
│   ├── game/               # 游戏协议服务器
│   ├── rock/               # Rock 二进制协议
│   ├── socket/             # Socket 封装
│   ├── module/             # 模块系统 + 动态库加载
│   ├── rate_limiter/       # 连接限流器
│   ├── ds/                 # Dispatcher / EventBus / 缓存
│   ├── streams/            # 流抽象 + 负载均衡
│   ├── application.*       # 应用主控
│   ├── log/                # 日志系统
│   ├── config/             # 配置系统 (YAML/XML)
│   ├── db/                 # 数据库 (Redis/MySQL/SQLite)
│   ├── orm/                # ORM 与 QueryBuilder
│   ├── util/               # 工具 (字符串/时间/加密/环境)
│   ├── bytearray/          # 字节流
│   ├── hook/               # 系统调用 Hook
│   └── thread/             # 线程封装
├── tests/                  # 测试与示例模块
│   ├── game/               # 游戏模块示例
│   └── module_test/        # 生命周期测试模块
├── servers/                # 主程序入口
├── template/               # 模块模板
└── cmake/                  # CMake 工具函数
```

## 使用方法

### 启动服务

```bash
# 前台运行
bin/main -s -c /path/to/conf

# 后台守护进程
bin/main -d -c /path/to/conf
```

### 信号命令

```bash
bin/main -s reload    # 热重载 .so 模块（SIGHUP）；配置变更由 inotify 自动加载
bin/main -s stop      # 优雅关闭（SIGTERM）
bin/main -s quit      # 优雅关闭（SIGTERM）
```

### 手动发信号

```bash
kill -HUP  $(cat blog.pid)   # 热重载 .so 模块
kill -TERM $(cat blog.pid)   # 优雅关闭
```

## 配置

配置是一个目录，其中所有 `.yml` / `.xml` 文件都会被加载（XML 元素树会被转换为 YAML 节点后复用同一套加载流程）。

```yaml
# system.yml 示例
server:
  drain_timeout_ms: 10000
  housekeeping_interval_ms: 500

module:
  path: module

fiber:
  stack_size: 131072
```

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `server.drain_timeout_ms` | 10000 | 停止 accept 后等待连接排空的超时 |
| `server.housekeeping_interval_ms` | 500 | housekeeping 轮询间隔（仅信号检测） |
| `fiber.stack_size` | 131072 | 协程栈大小（字节） |
| `module.path` | module | 模块 .so 搜索路径 |

配置文件变更由 inotify 事件驱动自动重载（改完即生效，无轮询延迟），业务模块如何声明配置项
并响应变更见 [`chen/watcher/README.md`](chen/watcher/README.md)。

Redis / MySQL / SQLite3 的配置字段与业务侧用法见
[`docs/db-configuration.md`](docs/db-configuration.md)。

## 编写模块

### 最小模块示例

```cpp
#include <chen/module/module.h>
#include <chen/log/log.h>

class MyModule : public chen::Module {
public:
    MyModule() : Module("my_module", "1.0.0", "my_module") {}

    bool onLoad() override {
        INFO(logger) << "MyModule loaded";
        return true;
    }

    bool onUnload() override {
        INFO(logger) << "MyModule unloaded";
        return true;
    }

    bool onServerReady() override {
        // 服务器准备就绪，可在此注册 handler
        return true;
    }

    bool onServerUp() override {
        // 服务器启动完成
        return true;
    }

    bool onDeactivate() override {
        // 热重载旧模块被替换 / 关机前：停定时器、flush 脏数据
        return true;
    }
};

extern "C" {
chen::Module* CreateModule() {
    return new MyModule();
}
void DestroyModule(chen::Module* m) {
    delete m;
}
}
```

编译为 `.so`，放入 `bin/module/` 目录，启动时自动加载。

## 致谢

sylar

## License

GNU General Public License v3.0. See [LICENSE](LICENSE).
