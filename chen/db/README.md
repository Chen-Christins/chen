# DB — 数据库模块

数据库连接（MySQL / SQLite3 / Redis）、参数化 SQL 构建（QueryBuilder）、分库分表路由（Shard）
与异步数据库线程。

> 配置与业务侧用法（Redis / MySQL / SQLite3 的 YAML 字段、多库、生命周期）见
> [`docs/db-configuration.md`](../../docs/db-configuration.md)。

## 核心类

| 类             | 说明                                                      |
|----------------|-----------------------------------------------------------|
| `IDB`          | 数据库抽象接口（`ISQLData` / `ISQLUpdate` / `ISQLQuery`） |
| `Redis`        | Redis 客户端封装，基于 hiredis                            |
| `RedisMgr`     | Redis 连接管理（单例）                                    |
| `MySQL`        | MySQL 连接封装                                            |
| `FoxThread`    | 独立数据库线程，避免阻塞 I/O                              |
| `FoxThreadMgr` | FoxThread 管理器（单例）                                  |
| `QueryBuilder` | 参数化 SQL 构建器：WHERE / JOIN / 分页 / 聚合 / 子查询    |
| `ShardRouter`  | 分库分表路由入口（`ShardRouterMgr` 单例）                 |

## 设计

```
I/O 线程 (IOManager) → 发起 DB 请求 → 放入 FoxThread 请求队列
                                            ↓
                                      FoxThread 执行 SQL/Redis 命令
                                            ↓
                                      结果回传 I/O 线程
```

数据库操作在独立线程中同步执行，避免阻塞 epoll 事件循环。

`FoxThreadMgr` 采用懒启动：首次调用任意 `dispatch()` 时才根据 `fox_thread` 配置创建并启动线程池，因此不使用 DB 的进程不会产生额外的
libevent 线程。

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

## QueryBuilder

参数化 SQL 构建器，位于 `chen/db/query_builder.h`，不依赖 ORM，可单独使用；
ORM 的 `Dao::newQuery()` 返回的也是它。

### 流式 API

```cpp
#include <chen/db/query_builder.h>

auto qb = QueryBuilder::Create("table_name")
    ->select("col1, col2")           // SELECT 列（DAO 方法会自动覆盖）
    ->where("col", "=", 123)         // col = ?
    ->where("name", "LIKE", "%x%")   // name LIKE ?
    ->orWhere("status", "=", 0)      // OR status = ?
    ->whereIn("id", {1, 2, 3})       // id IN (?, ?, ?)
    ->whereNotIn("id", {4, 5})       // id NOT IN (?, ?)
    ->whereNull("deleted_at")        // deleted_at IS NULL
    ->whereNotNull("email")          // email IS NOT NULL
    ->whereBetween("age", 18, 65)    // age BETWEEN ? AND ?
    ->whereLike("title", "关键词")    // title LIKE '%关键词%'
    ->whereLeftLike("title", "前缀")  // title LIKE '前缀%'
    ->whereIf(cond, "x", ">", 10)    // 仅 cond 为 true 时生效
    ->join("LEFT", "t2", "t1.id = t2.ref_id")
    ->groupBy("type")                // GROUP BY type
    ->having("count", ">", 5)        // HAVING count > ?
    ->orderBy("id", "DESC")          // ORDER BY id DESC
    ->limit(20)
    ->offset(0);
```

全部 `where*` 方法均有对应的 `orWhere*`（OR 连接）和 `*If(cond, ...)`（条件生效）变体。

### 子查询

```cpp
auto sub = QueryBuilder::Create("comment")
    ->select("article_id")
    ->where("status", "=", 1);

auto qb = QueryBuilder::Create("article")
    ->whereIn("id", sub);
// → WHERE id IN (SELECT article_id FROM comment WHERE status = ?)
```

子查询的参数自动合并到父查询，参数绑定顺序正确。LIMIT/OFFSET 在子查询中会被忽略（`IN` 子句不应分页）。

### 直接执行（不需要 DAO）

```cpp
auto data = qb->executeQuery(conn);        // SELECT 原始结果集
qb->executeCount(total, conn);             // COUNT
qb->set("status", 1)->executeUpdate(conn); // UPDATE
qb->executeDelete(conn);                   // DELETE
qb->insert("name", "hello")               // INSERT
  ->executeInsert(conn, newId);
```

> 与 Dao 配合的用法（`newQuery()` / `QueryByBuilder` / `QueryByBuilderPages` / `ParseRow`）
> 见 [`chen/orm/README.md`](../orm/README.md)。

## 分库分表

框架在 `chen/db/shard.h` 提供独立的分片路由层，与 ORM 解耦，可用于任何数据库操作。

### 核心概念

| 类                  | 说明                                                                |
|---------------------|---------------------------------------------------------------------|
| `ShardKey`          | 分片键，支持 `Int64`（hash 取模）、`String`（租户）、`Time`（日期） |
| `ShardTarget`       | 路由结果：逻辑库名 + 物理表名                                       |
| `IShardStrategy`    | 可插拔策略接口，实现 `route(logicalTable, key)` 即可自定义          |
| `HashShardStrategy` | Hash/取模 分库，可选分表                                            |
| `DateShardStrategy` | 按时间分表（strftime 格式化表名）                                   |
| `ShardRouter`       | Singleton 路由入口，管理规则和连接池                                |

### 初始化

```cpp
#include <chen/db/shard.h>

auto router = chen::ShardRouterMgr::GetInstance();

// 注册分片规则
router->addRule("notification",
    std::make_shared<chen::DateShardStrategy>("notification_%Y%m"));        // 按月分表
router->addRule("access_log",
    std::make_shared<chen::DateShardStrategy>("access_log_%Y%m%d"));        // 按日分表
router->addRule("user",
    std::make_shared<chen::HashShardStrategy>("user_shard_{shard}", 4));   // hash 分 4 库

// 注册分库连接
for (int i = 0; i < 4; i++) {
    router->addDB(fmt::format("user_shard_{}", i), mysqlConn);
}
```

### 使用

下面示例结合了 ORM 的 Dao；纯 SQL 场景直接用 `QueryBuilder::Create(table)` 配合 `IDB` 即可。

```cpp
// 按月分表：通知查询
auto [db, table] = ShardRouterMgr::GetInstance()->resolve(
    "notification", ShardKey::Time(notif.getCreateTime()));
auto qb = QueryBuilder::Create(table)->where(...);
NotificationInfoDao::QueryByBuilder(results, qb, db);

// Hash 分库：用户查询
auto [db2, _] = ShardRouterMgr::GetInstance()->resolve(
    "user", ShardKey::Int64(userId));
UserInfoDao::Query(userId, db2);
```

### 自定义策略

```cpp
class TenantShardStrategy : public IShardStrategy {
    ShardTarget route(const std::string& table, const ShardKey& key) override {
        // 按租户 ID 路由到不同库
        return ShardTarget{
            .dbName = "tenant_" + key.getString(),
            .tableName = ""           // 不分表
        };
    }
};
router->addRule("order", std::make_shared<TenantShardStrategy>());
```

> **边界**：跨分片查询、动态增删分片、分布式事务不在框架范围内，属于分布式中间件范畴。

## ORM

表定义（XML）、代码生成、Dao、脏字段追踪、数据库迁移见
[`chen/orm/README.md`](../orm/README.md)。
