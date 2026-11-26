# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Full build
make                    # from project root (auto-creates build dir, runs cmake + make)
# OR
cmake -B build && cmake --build build -j$(nproc)

# Build with AddressSanitizer
cmake -B build -DENABLE_ASAN=ON && cmake --build build -j$(nproc)

# Build a specific target
make test_unit_bytearray   # builds tests/bin/test_unit_bytearray
make test_iomanager        # builds tests/bin/test_iomanager
# (the Makefile passes the target name through to cmake --build)

# Clean
make clean
```

Build outputs: `bin/server` / `bin/orm`, shared libraries → `lib/`, modules → `bin/module/`,
测试程序 → `tests/bin/`。

## Running Tests

单元测试通过 CTest 注册，Debug 构建默认启用（`CHEN_BUILD_TESTS`）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j"$(nproc)"
make test                                              # ctest -L "unit|smoke"
ctest --test-dir build -L unit --output-on-failure     # 只跑单元测试
ctest --test-dir build -R test_unit_bytearray -V       # 单个用例
```

测试代码在 `tests/Unit/`，使用 `tests/TestHarness.hpp`（`CHEN_TEST` / `CHEN_CHECK_*` / `CHEN_TEST_MAIN`），
新增用例在 `tests/CMakeLists.txt` 里用 `chen_add_unit_test()` 注册。

历史测试程序仍是独立可执行文件（输出到 `tests/bin/`，不在 `bin/`），部分需要端口/数据库，因此不注册进
CTest，按需手工执行：`tests/bin/test_iomanager`、`tests/bin/test_http_server` 等（见 `tests/CMakeLists.txt`）。

完整约定见 [`docs/testing.md`](docs/testing.md)。非 Linux 机器不要构建或运行测试。

## Running the App

```bash
bin/server -s -c /path/to/conf        # foreground
bin/server -d -c /path/to/conf        # daemon (background)
bin/server -s reload                  # hot reload (SIGHUP)
bin/server -s stop                    # graceful shutdown (SIGTERM)
```

The main config entry point is `bin/conf/system.yml`. Config is a directory, not a single file — all `.yml` files under it are loaded.

## Architecture

This is a C++20 coroutine-based async server framework. The heavy lifting is done by two core engines:

### IOManager = Scheduler + TimerManager + epoll

- **Scheduler** (`src/Schedule/`): N:M fiber-to-thread scheduler. A pool of pthreads runs fibers from a shared task
  queue. `tickle()` wakes idle threads via pipe write.
- **TimerManager** (`src/Timer/`): sorted timer set, dispatches expired timers in epoll idle.
- **IOManager** (`src/IOManager/`): inherits both. `epoll_wait` is the idle loop. Each fd has a `FdContext` storing the
  fiber waiting on it → when epoll fires, the waiting fiber is rescheduled. This makes all socket I/O fiber-transparent
  via hook.

Responsibilities are split across three classes:

- `ProcessSupervisor` (`src/ProcessSupervisor.*`): master only — creates/binds listening sockets via
  `ServerRegistry`, forks and supervises workers, handles hot reload and shutdown.
- `WorkerRuntime` (`src/WorkerRuntime.*`): one worker process — owns one IOManager (`main`,
  `use_caller=true`, so it runs on the worker thread) that serves IO + business fibers + module
  `onTick()` timers; plus module lifecycle, config watcher, graceful drain. There is no separate
  tick thread — slow `onTick()` blocks the event loop, keep it non-blocking.
- `Application` (`src/Application.*`): process bootstrap (args, config, daemon) + façade (`init` / `run` / `getServer` /
  `getAllServers` / service discovery).

### Fiber (coroutine)

`src/Fiber/Fiber.hpp` — uses `boost::context::fcontext_t` for user-space context switching. Each fiber has its own stack
(default 128KB). The system call hooks (`src/Hook/`) make blocking syscalls (read/write/accept/connect/sleep)
fiber-aware: they register the fd with the IOManager and yield, so the thread can run other fibers.

### Server hierarchy

```
TcpServer (base: bind/start/stop/SSL)
├── HttpServer       (HTTP/1.1 + H2 negotiation, ServletDispatch)
├── WSServer         (WebSocket upgrade from HTTP)
├── RockServer       (proprietary binary protocol)
├── RpcServer        (Protobuf RPC, template registerMethod)
└── GenericProtocolServer  (game protocol, pluggable parser/handler)
```

Servers are created by `TcpServerFactory` from YAML config. Each server gets three IOManager slots: `accept_worker`, `io_worker`, `process_worker`.

### Module system (hot reload)

`src/Module.hpp` — Modules are `.so` files loaded via `dlopen`. `ModuleManager` scans `bin/module/`, loads each, calls
lifecycle methods.

Hot reload uses a **master/worker multi-process model**: the master owns the listening
sockets and pidfile, and forks a fresh worker to load the new modules. Old workers stop
accepting, drain in-flight connections, then exit. No in-process `dlclose` — each process
has exactly one module version.

Hot reload flow: `SIGHUP` → master forks new worker:
1. New worker `dlopen`s the new `.so`, `CreateModule()`, `onLoad()`
2. New worker `onServerReady()` → `onServerUp()`, starts accepting
3. Master signals old worker to stop accepting
4. Old worker drains in-flight connections (bounded by `server.drain_timeout_ms`)
5. Old worker `onDeactivate()` → `onUnload()` → process exits

Module lifecycle methods (all optional, have defaults):
`onLoad` → `onServerReady` → `onServerUp` → [`onTick` periodic] → `onDeactivate` → `onUnload`

### Config system

`src/Config/Config.hpp` — Typed YAML-backed config with change listeners. `Config::Lookup<T>(name, default, desc)`
creates/retrieves a `ConfigVar<T>`. Listeners fire on value change (used for hot reload of log config, server config,
etc.). Name must match `[a-z0-9_.]+`.

### Log system

`src/Log/Log.hpp` — `Logger` → `LogAppender` chain. `FileLogAppender` supports time-based rotation (`30min`/`hour`/
`12hour`/`day`/`week`/`month`) and `retention_days` cleanup. Log macros: `DEBUG(logger)`, `INFO(logger)`,
`WARN(logger)`, `ERROR(logger)`, `FATAL(logger)`. C++20 `std::format`-style:
`INFO(LOG_ROOT()) << std::format("{} connections", n)`.

Full configuration guide: `docs/log-configuration.md`.

## Code Style

Follow `cpp-code-style` skill (`.claude/skills/cpp-code-style/SKILL.md`). Key points:

- **Class/struct**: PascalCase (`TcpServer`, `LogEvent`)
- **Member variables**: snake_case + 后缀下划线 (`name_`, `is_keep_alive_`)
- **Static members**: `g_` prefix (`g_datas`)
- **Thread-local**: `t_` prefix (`t_thread`)
- **Member functions**: camelCase (`getValue()`, `setName()`)
- **Free/static functions**: PascalCase (`GetInstance()`, `Lookup()`)
- **Local variables/params**: snake_case
- **Enums/macros**: ALL_CAPS
- **Indent**: 4 spaces, no tabs. Opening brace on same line for class/if/for/switch, new line for functions.
- **Smart pointer typedef**: each class defines `typedef std::shared_ptr<T> ptr;`
- **Lock typedef**: each class defines `typedef SpinLock MutexType;` with RAII `MutexType::Lock lock(mutex_);`
- **File headers**: Doxygen `@file @brief @author @date`
- **Include guard**: existing code uses `#ifndef`, new files prefer `#pragma once`
- **Namespace**: all code in `namespace chen`, content not indented inside namespace
- **Comma style**: leading commas for wrapped parameter lists and initializer lists
- Avoid `NULL`/`0` for null pointers — use `nullptr`

## Key Files to Know

| File                          | Role                                                           |
|-------------------------------|----------------------------------------------------------------|
| `src/Application.hpp`         | Process bootstrap + façade (args/config/daemon, server lookup) |
| `src/ProcessSupervisor.hpp`   | Master: fork/supervise workers, hot reload, shutdown           |
| `src/WorkerRuntime.hpp`       | Worker: IOManagers, module lifecycle, graceful drain           |
| `src/ServerRegistry.hpp`      | TcpServer creation/bind/lookup                                 |
| `src/IOManager/IOManager.hpp` | epoll + fiber scheduler + timer manager                        |
| `src/Schedule/Schedule.hpp`   | N:M fiber thread pool                                          |
| `src/Fiber/Fiber.hpp`         | boost.context coroutine                                        |
| `src/Hook/Hook.hpp`           | Syscall interception for fiber-friendly IO                     |
| `src/Module.hpp`              | Module base class, lifecycle                                   |
| `src/Library.hpp`             | dlopen/dlclose wrapper                                         |
| `src/Config/Config.hpp`       | Typed YAML config with change listeners                        |
| `src/Log/Log.hpp`             | Logger, appenders, formatters                                  |
| `src/Tcp/TcpServer.hpp`       | TcpServer base + TcpServerFactory                              |
| `src/Http/Servlet.hpp`        | ServletDispatch, HTTP routing                                  |
| `src/Rpc/RpcServer.hpp`       | Protobuf RPC server                                            |
| `src/Thread/Thread.hpp`       | pthread wrapper                                                |
| `servers/main.cc`             | Entry point                                                    |
| `bin/conf/system.yml`         | Main runtime config                                            |
| `CMakeLists.txt`              | Root build file                                                |
| `cmake/utils.cmake`           | Build helper functions                                         |
