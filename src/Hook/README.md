# Hook — 系统调用 Hook

Hook 阻塞式系统调用，使其在协程环境下表现为异步非阻塞。

## 原理

```
用户代码: read(fd, buf, len)   ← 标准 POSIX 调用
              ↓
Hook 层:   检查 fd 是否非阻塞
              ↓ 是
           IOManager::addEvent(fd, READ) → Fiber::YieldToHold()
              ↓ epoll 通知可读
           Fiber 被唤醒，继续执行真正的 read()
```

## 被 Hook 的系统调用

### Socket

- `socket`, `connect`, `accept`
- `read`, `readv`, `recv`, `recvfrom`, `recvmsg`
- `write`, `writev`, `send`, `sendto`, `sendmsg`
- `close`
- `setsockopt`, `getsockopt`

### IO

- `sleep`, `usleep`, `nanosleep`
- `fcntl`, `ioctl`

## 核心类

- **FdManager** — 管理所有 fd 的非阻塞状态
- **FdCtx** — 单个 fd 的上下文（是否非阻塞、超时等）

## 启用

```cpp
set_hook_enable(true);   // 当前线程启用 Hook
set_hook_enable(false);  // 禁用
```

Scheduler 的工作线程默认开启 Hook（`IOManager::schedule()` 提交的任务在 hook 环境下执行）。
主线程（非 Scheduler 线程）默认关闭，应用层代码不需要手动调用 `set_hook_enable()`。

## 生效条件（契约）

一个被 hook 的调用只有在**同时满足**下面几条时才真的让出协程（`src/Hook/Hook.cc` 的 `do_io`）：

1. 当前线程的 `t_hook_enable` 为 `true`；
2. fd 在 `FdMgr` 里能查到 `FdCtx`；
3. fd 是 **socket**，且没有被用户主动设成非阻塞（`getUserNonblock()`）；
4. 真正执行系统调用后返回 `EAGAIN`（确实没数据）。

任何一条不满足，hook 就直接调用 libc 原函数。由此带来几条必须记住的边界：

- **普通文件 IO 不走协程**：磁盘文件的 `read` / `write` 是同步阻塞的，慢盘会把整个 IOManager 线程卡住。
  日志、配置这类小文件没问题，大文件 / 数据库请放业务自己的线程池。
- **未 hook 的调用就是阻塞**：`getaddrinfo`（DNS）、`fsync`、`stat`、各种同步 DB 客户端都不在表里，
  放在调度线程上调用会阻塞事件循环。异步 DNS 列在 [`docs/roadmap.md`](../../docs/roadmap.md) 的 v1.8.0。
- **非调度线程上 hook 不生效**：master 进程、业务自己 `std::thread` 起的线程里调 `usleep` / `read`
  是实实在在的阻塞。
- hook 是进程级符号替换（对 `RTLD_NEXT` 取原函数），对静态链接进二进制、或代码里直接 `syscall()` 的地方无效。

## 线程模型

`t_hook_enable` 是 **thread_local**，只有跑 `Scheduler::run()` 的线程会打开它
（`src/Schedule/Schedule.cc` 的 `set_hook_enable(true)`）：

- IOManager 的工作线程会打开；`use_caller=true` 的主线程也会走 `run()`，同样打开；
- fork 出来的 worker 在 `WorkerRuntime` 里重新创建 IOManager，因此新进程的调度线程各自开启；
- **不要在业务代码里调 `set_hook_enable(false)`**：该线程之后的 socket IO 会退化成真阻塞，事件循环直接被卡住。

## 已知细节

- `usleep` 的 hook 实现是 `addTimer(usec / 1000)`，**不足 1ms 会被截成 0ms 定时器**：不会真正休眠，
  只是立刻让出一次协程。协程里需要“等一下”时用 `>= 1ms`。
- `close()` 在 `t_hook_enable=false` 时也会清理 `FdMgr` 并 `cancelAll`，避免 fd 复用时残留等待者。
- 连接超时、读写超时来自 `FdCtx::getTimeout()`（`SO_RCVTIMEO` / `SO_SNDTIMEO`），到期通过
  `cancelEvent()` 唤醒等待协程并返回 `ETIMEDOUT`。
