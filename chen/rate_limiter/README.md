# RateLimiter / ConnLimiter — 限流器

连接级限流原语，用于在高并发/大流量下保护服务不被资源耗尽打挂。

## 核心类

- **RateLimiter** — 纯令牌桶算法（仅标准库依赖）。
  - 以 `qps` 速率持续补充令牌，桶容量 `burst` 决定瞬时突发上限。
  - `tryAcquire()` 有令牌则消费 1 放行；`qps <= 0` 恒放行（不限流）。
  - 不感知业务语义，可复用于任意"速率"场景。

- **ConnLimiter** — 连接准入控制器，组合令牌桶 + 并发计数。
  - `tryAcquire()`：过速率限制且并发数 < `max_conn` 才放行，放行则占用一个名额。
  - `release()`：连接关闭时归还名额。
  - 附带 `current()` / `getRejectedCount()` 统计。
  - `CreateFromArgs(args)` 从 `TcpServerConf::args` 解析 `accept_qps` / `accept_burst` / `max_conn`。

## 使用

```cpp
#include "chen/rate_limiter/conn_limiter.h"

// 每秒最多 1000 个新连接，突发 2000，并发上限 10000
chen::ConnLimiter::ptr limiter = std::make_shared<chen::ConnLimiter>(1000, 2000, 10000);

// accept 后
if (!limiter->tryAcquire()) {
    client->close();      // 超限，直接关闭
    continue;
}

// 连接结束时
limiter->release();
```

## 设计

- 令牌桶（速率）与并发计数（容量）语义不同，故分成两层：`RateLimiter` 只做速率原语，`ConnLimiter` 组合两者做连接准入。
- 均在 `std::mutex` 保护下，线程安全。
