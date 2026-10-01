# DB / Redis 配置指南（业务侧）

框架只提供数据库/缓存的**接口与实现库**，不再主动初始化或释放任何连接。
是否使用 Redis / MySQL / SQLite3、用几个库、何时建连与释放，全部由业务 module 决定。

本文说明如何在 `bin/conf/system.yml`（或业务自己的 `.yml`）中配置，以及模块里如何取用。

## 设计约定

- **名字即逻辑库**：所有 DB/Redis 配置都是 `名字 -> 参数` 的映射，每个名字对应一个独立连接池。
  业务代码通过名字寻址，例如 `get("blog")`、`query("user", sql)`。
- **框架不兜底**：`Application` 不再初始化 Redis、也不在关机时 `freeAll()`。
  使用方 module 必须在自己的生命周期里接管（见 [模块生命周期](#模块生命周期)）。
- **懒启动**：`FoxThreadMgr` 在首次 `dispatch()` 时才会按 `fox_thread` 配置创建线程池，
  不使用 DB 的进程不会产生额外的 libevent 线程。

## 配置键总览

| 配置键 | 类型 | 说明 |
|--------|------|------|
| `redis.config` | `map<name, map<string,string>>` | Redis 连接池，`name` 为逻辑名 |
| `fox_thread` | `map<name, {num, advance}>` | FoxThread 线程池，异步 Redis 依赖 |
| `mysql.dbs` | `map<name, map<string,string>>` | MySQL 连接池 |
| `sqlite3.dbs` | `map<name, map<string,string>>` | SQLite3 连接池（每个 `name` 一个文件） |

---

## Redis

### 配置结构

```yaml
redis:
  config:
    blog:                         # 逻辑名，业务用 get("blog") 取用
      host: 127.0.0.1:6379        # ip:port
      type: fox_redis             # 见下方类型表
      pool: 2                     # 连接数
      timeout: 100                # 超时（毫秒）
      passwd: ""                  # 可选
      log_enable: 1               # 可选，默认 1
    session:
      host: 127.0.0.1:6379
      type: redis
      pool: 1
      timeout: 100
```

> 早先的 `redis.enable` 开关已移除，框架不再据此初始化 Redis；是否存在 `redis.config` 及是否调用由业务决定。

### 字段说明

| 字段 | 必填 | 说明 |
|------|------|------|
| `host` | 是 | 单机/异步单机：`ip:port`；集群：节点种子串（如 `10.0.0.1:7000,10.0.0.2:7000,...`） |
| `type` | 是 | 客户端类型，见下表 |
| `pool` | 是 | 该逻辑名预建的连接数 |
| `timeout` / `timeout_com` | 否 | 命令超时，单位**毫秒**；两者同时存在时 `timeout_com` 优先 |
| `passwd` | 否 | 认证密码 |
| `log_enable` | 否 | 是否打印命令日志，默认 `1` |

### 客户端类型

| `type` | 类 | 特点 |
|--------|-----|------|
| `redis` | `Redis` | 同步单机 |
| `redis_cluster` | `RedisCluster` | 同步集群 |
| `fox_redis` | `FoxRedis` | **异步单机**，基于 `FoxThread`，不阻塞协程 |
| `fox_redis_cluster` | `FoxRedisCluster` | 异步集群 |

生产推荐 `fox_redis` / `fox_redis_cluster`：命令投递到独立线程执行，epoll 事件循环不被阻塞。

### FoxThread 线程配置

异步类型（`fox_redis*`）依赖 `fox_thread` 中名为 `redis` 的线程池（名字在实现里固定为 `redis`）：

```yaml
fox_thread:
  redis:
    num: 1          # num == 1 用单线程 FoxThread；> 1 用 FoxThreadPool
    advance: 0
```

- `num` 必填且需 `> 0`，否则该线程池初始化失败
- 首次调用 `RedisMgr::GetInstance()` 时触发 `FoxThread` 懒启动

### 模块生命周期

Redis 连接由使用方 module 负责建连与释放：

```cpp
#include <chen/db/redis.h>

bool MyModule::onServerUp() {
    // 首次使用即按 redis.config 建立连接池（FoxThread 随之懒启动）
    chen::RedisMgr::GetInstance();
    return true;
}

bool MyModule::onUnload() {
    // 关机/热重载时释放全部连接
    chen::RedisMgr::GetInstance()->freeAll();
    return true;
}
```

> `RedisMgr` 是单例，由首个使用者触发构建。多个 module 共用时需约定由谁负责 `freeAll()`，避免重复释放或漏放。

### 使用

```cpp
// 方式一：拿连接自己发命令
auto rds = chen::RedisMgr::GetInstance()->get("blog");
auto reply = rds->cmd("set %s %s", "k", "v");

// 方式二：静态工具（内部自动 get + 归还）
chen::RedisUtil::Cmd("blog", "hset %s %s %s", "article:1", "views", "10");
```

`IRedis::cmd` 与原生命令一致，支持格式化与 `vector<string>` 参数两种形式。

---

## MySQL

### 配置结构

```yaml
mysql:
  dbs:
    blog:
      host: 127.0.0.1
      port: 3306
      user: root
      passwd: secret
      dbname: blog
      pool: 5
    user:
      host: 10.0.0.2
      port: 3306
      user: app
      passwd: secret2
      dbname: user
```

| 字段 | 说明 |
|------|------|
| `host` / `port` | 实例地址与端口 |
| `user` / `passwd` | 账号密码 |
| `dbname` | 默认库名 |
| `pool` | 可选，连接池大小 |

### 使用

```cpp
#include <chen/db/mysql.h>

// 直接取连接（可做事务）
auto conn = chen::MySQLMgr::GetInstance()->get("blog");
conn->execute("update users set name='x' where id=%d", 1);
auto res = conn->query("select id, name from users");

// 或静态工具（内部自动 get + 归还）
chen::MySQLUtil::Query("blog", "select * from users where id=%d", 1);
chen::MySQLUtil::Execute("blog", "delete from users where id=%d", 1);

// 事务
auto trx = chen::MySQLMgr::GetInstance()->openTransaction("blog", false);
trx->begin();
trx->execute("insert into t(a) values(1)");
trx->commit();
```

---

## SQLite3

### 配置结构

```yaml
sqlite3:
  dbs:
    blog:
      path: /data/blog.db                 # 相对路径会拼到 server.work_path 下
      sql: PRAGMA synchronous = OFF       # 可选，连接建立后执行的初始化 SQL
    cache:
      path: /data/cache.db
```

| 字段 | 说明 |
|------|------|
| `path` | 数据库文件路径；不含 `:` 时按 `server.work_path` 解析为绝对路径 |
| `sql` | 可选，连接建立后执行的初始化语句（如 PRAGMA） |

### 使用

```cpp
#include <chen/db/sqlite3.h>

auto db = chen::SQLite3Mgr::GetInstance()->get("blog");
db->execute("insert into t(a) values(?)", 1);
auto res = db->query("select * from t");

// 或由管理器直接执行
chen::SQLite3Mgr::GetInstance()->execute("blog", "delete from t where a=1");
```

---

## 为什么是「多个库」

`dbs` / `config` 支持多个命名条目，每个名字有独立参数与连接池：

1. **连接多个库/实例**：如 `blog`、`user`、`log` 指向不同 MySQL 实例或 schema
2. **每库独立连接池**：`get(name)` 从对应池取、归还时也按 name 回池，互不干扰
3. **读写分离 / 分库分表**：配 `master`/`slave` 或 `shard0`/`shard1`，业务按需选名字
4. **业务与权限隔离**：不同 module 用不同账号 / 库 / 字符集
5. **SQLite3 特有**：每个名字对应一个 `.db` 文件，天然多文件隔离

## 运行时注册

除配置文件外，也可在代码里注册（用于覆盖配置或动态添加）：

```cpp
chen::MySQLMgr::GetInstance()->registerMySQL("tmp",
    {{"host", "127.0.0.1"}, {"port", "3306"}, {"user", "root"},
     {"passwd", ""}, {"dbname", "tmp"}});

chen::SQLite3Mgr::GetInstance()->registerSQLite3("tmp", {{"path", "/data/tmp.db"}});
```

`get(name)` 的查找顺序：连接池 → 配置文件 → 运行时注册。

## 配置变更注意

- MySQL / SQLite3 的参数在**每次 `get()` 时读取配置**，新增名字立即生效；
  已建连的旧名字仍用旧参数，需业务层重建连接或重启。
- Redis 在 `RedisManager` 构造时**快照 `redis.config`**，改配置后需重新初始化
  （释放 `freeAll()` 后重新 `GetInstance()`，或重启/热重载模块）才生效。

## 相关文档

- 模块生命周期：[`docs/module-lifecycle.md`](module-lifecycle.md)
- 模块开发指南：[`docs/module-development.md`](module-development.md)
- 数据库模块说明：[`chen/db/README.md`](../chen/db/README.md)
