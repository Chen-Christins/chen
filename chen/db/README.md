# DB — 数据库模块

Redis 客户端、MySQL / SQLite3 连接、异步数据库线程。

> 配置与业务侧用法（Redis / MySQL / SQLite3 的 YAML 字段、多库、生命周期）见
> [`docs/db-configuration.md`](../../docs/db-configuration.md)。

## 核心类

| 类 | 说明 |
|----|------|
| `Redis` | Redis 客户端封装，基于 hiredis |
| `RedisMgr` | Redis 连接管理（单例） |
| `MySQL` | MySQL 连接封装 |
| `FoxThread` | 独立数据库线程，避免阻塞 I/O |
| `FoxThreadMgr` | FoxThread 管理器（单例） |

## 设计

```
I/O 线程 (IOManager) → 发起 DB 请求 → 放入 FoxThread 请求队列
                                            ↓
                                      FoxThread 执行 SQL/Redis 命令
                                            ↓
                                      结果回传 I/O 线程
```

数据库操作在独立线程中同步执行，避免阻塞 epoll 事件循环。

`FoxThreadMgr` 采用懒启动：首次调用任意 `dispatch()` 时才根据 `fox_thread` 配置创建并启动线程池，因此不使用 DB 的进程不会产生额外的 libevent 线程。

## 生命周期由业务侧负责

框架 `Application` 不再初始化或释放任何 DB 资源。使用 Redis 的 module 需自行接管：

```cpp
bool MyModule::onServerUp() {
    // 首次使用触发 Redis 连接池初始化（FoxThreadMgr 会随之懒启动）
    chen::RedisMgr::GetInstance();
    return true;
}

bool MyModule::onUnload() {
    // 关机/热重载时释放连接
    chen::RedisMgr::GetInstance()->freeAll();
    return true;
}
```

`RedisMgr` 是单例，由首个使用者触发构建；各 module 需约定由谁负责 `freeAll()`，框架不兜底。

## Redis

配置位于 `redis.config.<name>`（单机 / 集群 / 异步，见配置指南）：

```cpp
auto rds = RedisMgr::GetInstance()->get("blog");
auto reply = rds->cmd("get %s", "key");

// 或静态工具，内部自动 get + 归还
RedisUtil::Cmd("blog", "set %s %s", "key", "value");
```

基于 hiredis_vip（支持 cluster/sentinel）。

## MySQL

配置位于 `mysql.dbs.<name>`：

```cpp
auto conn = MySQLMgr::GetInstance()->get("blog");
auto result = conn->query("SELECT * FROM users WHERE id=%d", id);

// 或静态工具
MySQLUtil::Query("blog", "SELECT * FROM users WHERE id=%d", id);
```

## SQLite3

配置位于 `sqlite3.dbs.<name>`（每个名字对应一个 `.db` 文件）：

```cpp
auto db = SQLite3Mgr::GetInstance()->get("blog");
auto result = db->query("SELECT * FROM users");
```

## ORM

参见 `chen/orm/` 模块。
