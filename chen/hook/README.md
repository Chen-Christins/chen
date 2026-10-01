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
