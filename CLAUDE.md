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
make test_config        # builds bin/test_config
make test_iomanager     # builds bin/test_iomanager
# (the Makefile passes the target name through to cmake --build)

# Clean
make clean
```

Build outputs: executables → `bin/`, shared libraries → `lib/`, modules → `bin/module/`.

## Running Tests

Each test is a standalone executable under `bin/`:

```bash
bin/test_config
bin/test_iomanager
bin/test_http_server
bin/test_fiber
# ... etc (see tests/CMakeLists.txt for full list)
```

No test framework — tests are ad-hoc programs that exercise the corresponding subsystem.

## Running the App

```bash
bin/main -s -c /path/to/conf         # foreground
bin/main -d -c /path/to/conf         # daemon (background)
bin/main -s reload                   # hot reload (SIGHUP)
bin/main -s stop                     # graceful shutdown (SIGTERM)
```

The main config entry point is `bin/conf/system.yml`. Config is a directory, not a single file — all `.yml` files under it are loaded.

## Architecture

This is a C++20 coroutine-based async server framework. The heavy lifting is done by two core engines:

### IOManager = Scheduler + TimerManager + epoll

- **Scheduler** (`chen/schedule/`): N:M fiber-to-thread scheduler. A pool of pthreads runs fibers from a shared task queue. `tickle()` wakes idle threads via pipe write.
- **TimerManager** (`chen/timer/`): sorted timer set, dispatches expired timers in epoll idle.
- **IOManager** (`chen/iomanager/`): inherits both. `epoll_wait` is the idle loop. Each fd has a `FdContext` storing the fiber waiting on it → when epoll fires, the waiting fiber is rescheduled. This makes all socket I/O fiber-transparent via hook.

The `Application` creates two IOManagers: `m_mainIOManager` (IO + business logic) and `m_tickIOManager` (periodic module `onTick()` calls).

### Fiber (coroutine)

`chen/fiber/fiber.h` — uses `boost::context::fcontext_t` for user-space context switching. Each fiber has its own stack (default 128KB). The system call hooks (`chen/hook/`) make blocking syscalls (read/write/accept/connect/sleep) fiber-aware: they register the fd with the IOManager and yield, so the thread can run other fibers.

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

`chen/module.h` — Modules are `.so` files loaded via `dlopen`. `ModuleManager` scans `bin/module/`, loads each, calls lifecycle methods.

Hot reload flow: `SIGHUP` → `Application::doHotReload()` → `ModuleManager::reloadModule()`:
1. Copies `.so` to tmp file (defeats glibc `DF_1_NODELETE` with ASan)
2. `dlopen` new .so, `CreateModule()`, `onLoad()`
3. Calls `old->onDrain()` — module stops accepting new work, closes connections
4. Waits for connections to drain, then `old->onGracefulUnload()`
5. Releases old shared_ptr → `DestroyModule()` → `dlclose`

Module lifecycle methods (all optional, have defaults):
`onLoad` → `onServerReady` → `onServerUp` → [`onTick` periodic] → `onDrain` → `onGracefulUnload` → `onUnload`

### Config system

`chen/config/config.h` — Typed YAML-backed config with change listeners. `Config::Lookup<T>(name, default, desc)` creates/retrieves a `ConfigVar<T>`. Listeners fire on value change (used for hot reload of log config, server config, etc.). Name must match `[a-z0-9_.]+`.

### Log system

`chen/log/log.h` — `Logger` → `LogAppender` chain. `FileLogAppender` supports time-based rotation (`30min`/`hour`/`12hour`/`day`/`week`/`month`) and `retention_days` cleanup. Log macros: `DEBUG(logger)`, `INFO(logger)`, `WARN(logger)`, `ERROR(logger)`, `FATAL(logger)`. C++20 `std::format`-style: `INFO(LOG_ROOT()) << std::format("{} connections", n)`.

Full configuration guide: `docs/log-configuration.md`.

## Code Style

Follow `cpp-code-style` skill (`.claude/skills/cpp-code-style/SKILL.md`). Key points:

- **Class/struct**: PascalCase (`TcpServer`, `LogEvent`)
- **Member variables**: `m_` + snake_case (`m_name`, `m_isKeepAlive`)
- **Static members**: `g_` prefix (`g_datas`)
- **Thread-local**: `t_` prefix (`t_thread`)
- **Member functions**: camelCase (`getValue()`, `setName()`)
- **Free/static functions**: PascalCase (`GetInstance()`, `Lookup()`)
- **Local variables/params**: snake_case
- **Enums/macros**: ALL_CAPS
- **Indent**: 4 spaces, no tabs. Opening brace on same line for class/if/for/switch, new line for functions.
- **Smart pointer typedef**: each class defines `typedef std::shared_ptr<T> ptr;`
- **Lock typedef**: each class defines `typedef SpinLock MutexType;` with RAII `MutexType::Lock lock(m_mutex);`
- **File headers**: Doxygen `@file @brief @author @date`
- **Include guard**: existing code uses `#ifndef`, new files prefer `#pragma once`
- **Namespace**: all code in `namespace chen`, content not indented inside namespace
- **Comma style**: leading commas for wrapped parameter lists and initializer lists
- Avoid `NULL`/`0` for null pointers — use `nullptr`

## Key Files to Know

| File | Role |
|------|------|
| `chen/application.h` | App orchestrator, signal handling, graceful shutdown / hot reload |
| `chen/iomanager/iomanager.h` | epoll + fiber scheduler + timer manager |
| `chen/schedule/schedule.h` | N:M fiber thread pool |
| `chen/fiber/fiber.h` | boost.context coroutine |
| `chen/hook/hook.h` | Syscall interception for fiber-friendly IO |
| `chen/module.h` | Module base class, lifecycle |
| `chen/library.h` | dlopen/dlclose wrapper |
| `chen/config/config.h` | Typed YAML config with change listeners |
| `chen/log/log.h` | Logger, appenders, formatters |
| `chen/tcp/tcp_server.h` | TcpServer base + TcpServerFactory |
| `chen/http/servlet.h` | ServletDispatch, HTTP routing |
| `chen/rpc/rpc_server.h` | Protobuf RPC server |
| `chen/thread/thread.h` | pthread wrapper |
| `servers/main.cc` | Entry point |
| `bin/conf/system.yml` | Main runtime config |
| `CMakeLists.txt` | Root build file |
| `cmake/utils.cmake` | Build helper functions |
