# chen 框架模块

## 概述

`chen/` 目录是框架的源代码根目录，每个子目录为一个独立模块。模块按功能分层：

```
应用层（业务代码）
    ├── http / game / rpc / rock     ← 协议实现
    ├── tcp                           ← TCP 服务器基类
    ├── orm / db / email              ← 数据 / 外部服务
    └── data / ds / parser / protocol ← 辅助数据处理
    
基础设施层
    ├── iomanager（继承 schedule + timer）← 事件循环核心
    │   ├── schedule  ← M:N 协程调度器
    │   └── timer     ← 时间堆定时器
    ├── fiber         ← 用户态协程
    ├── hook          ← 系统调用拦截（将阻塞 IO 转为 fiber 异步）
    ├── socket        ← 套接字封装
    ├── thread        ← 线程封装（框架内部使用）
    ├── bytearray     ← 二进制数据缓冲区
    └── config / log  ← 配置 / 日志
```

## 模块索引

### 核心基础设施

| 模块 | 说明 | 文档 |
|------|------|------|
| **iomanager** | epoll + Scheduler + TimerManager，框架的事件循环核心 | [README](iomanager/README.md) |
| **schedule** | M:N 协程调度器，将 fiber 分发到线程池执行 | [README](schedule/README.md) |
| **timer** | 基于时间堆的毫秒级定时器，集成在 IOManager 中 | [README](timer/README.md) |
| **fiber** | 基于 Boost.Context 的用户态协程 | [README](fiber/README.md) |
| **hook** | 系统调用 hook，使阻塞 IO 在 fiber 环境下异步化 | [README](hook/README.md) |
| **thread** | 线程封装 + 信号量 + 互斥锁（框架内部使用） | [README](thread/README.md) |

### 网络 / IO

| 模块 | 说明 | 文档 |
|------|------|------|
| **socket** | Socket API 封装，支持 TCP/UDP/SSL/Unix Domain | [README](socket/README.md) |
| **bytearray** | 二进制数据读写缓冲区（网络字节序、变长编码） | [README](bytearray/README.md) |
| **streams** | 流式读写扩展（负载均衡、服务发现、zlib 压缩流） | [README](streams/README.md) |
| **module** | 动态模块加载（dlopen）+ `Library` dlopen/dlclose 封装 | [README](module/README.md) |

### 协议实现

| 模块 | 说明 | 文档 |
|------|------|------|
| **http** | HTTP/1.1 + HTTP/2 + WebSocket + SSE | [README](http/README.md) |
| **tcp** | TcpServer 基类，TcpServerFactory 工厂 | [README](tcp/README.md) |
| **rpc** | 模板驱动的 RPC 框架，无需 IDL | [README](rpc/README.md) |
| **rock** | 自定义二进制协议框架 | [README](rock/README.md) |
| **game** | 游戏协议服务器（可插拔编解码器） | [README](game/README.md) |

### 数据 / 存储

| 模块 | 说明 | 文档 |
|------|------|------|
| **db** | Redis + MySQL 客户端封装 | [README](db/README.md) |
| **orm** | 轻量级 ORM（表定义 + 查询构建器） | [README](orm/README.md) |
| **data** | 数据加解密、数据读取器 | — |
| **ds** | 数据结构（LRU Cache、RoaringBitmap） | — |
| **email** | SMTP 邮件发送 | — |

### 配置 / 日志 / 工具

| 模块 | 说明 | 文档 |
|------|------|------|
| **log** | 多级别日志系统（支持文件滚动、格式自定义、热加载） | [README](log/README.md) |
| **config** | YAML 驱动配置系统（类型化配置 + 变更监听） | [README](config/README.md) |
| **util** | 工具函数集合（加密、随机、字符串、时间、文件系统等） | [README](util/README.md) |
| **parser** | 解析器（MultiPart 解析） | — |
| **protocol** | 协议定义文件 | — |

## 构建

```bash
# 完整构建
make
# 或
cmake -B build && cmake --build build -j$(nproc)

# 构建 + 打包 SDK
./package_sdk.sh [-v <version>]

# 运行测试
bin/test_rpc_server    # 启动 RPC 测试服务端
bin/test_rpc_client    # 启动 RPC 测试客户端（需先启动 server）
bin/test_ws_server     # 启动 WebSocket 测试服务端
bin/test_ws_client     # 启动 WebSocket 测试客户端
```

## 推荐的编码模式

```
IOManager 创建 → schedule() 提交任务
       ↓
schedule 将任务包装为 fiber 在 work 线程执行
       ↓
Hook 层自动将 socket IO 转为 fiber 异步
       ↓
fiber 执行完毕，自动归还线程池
```

应用层代码通常只接触 `IOManager::schedule()` 和具体协议的 API（`HttpServer`、`RpcClient`、`WSServer` 等），不需要直接操作 Fiber、Thread 或 Scheduler。
