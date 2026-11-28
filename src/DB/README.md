# DB — 数据库模块

数据库连接（MySQL / SQLite3 / PostgreSQL / Redis）、参数化 SQL 构建（QueryBuilder）、分库分表路由（Shard）
与异步数据库线程。

> 配置与业务侧用法（Redis / MySQL / SQLite3 / PostgreSQL 的 YAML 字段、多库、生命周期）见
> [`docs/db-configuration.md`](../../docs/db-configuration.md)。

## 核心类

| 类              | 说明                                                      |
|-----------------|-----------------------------------------------------------|
| `IDB`           | 数据库抽象接口（`ISQLData` / `ISQLUpdate` / `ISQLQuery`） |
| `Redis`         | Redis 客户端封装，基于 hiredis                            |
| `RedisMgr`      | Redis 连接管理（单例）                                    |
| `MySQL`         | MySQL 连接封装                                            |
| `PostgreSQL`    | PostgreSQL 连接封装（基于 libpq）                         |
| `PostgreSQLMgr` | PostgreSQL 连接管理（单例）                               |
| `FoxThread`     | 独立数据库线程，避免阻塞 I/O                              |
| `FoxThreadMgr`  | FoxThread 管理器（单例）                                  |
| `QueryBuilder`  | 参数化 SQL 构建器：WHERE / JOIN / 分页 / 聚合 / 子查询    |
| `ShardRouter`   | 分库分表路由入口（`ShardRouterMgr` 单例）                 |

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
`freeAll()` 可以在任意线程调用：`FoxRedis` 的 `redisAsyncContext` 释放会派发回连接所属的 `FoxThread`
执行，避免与事件循环里的 hiredis 回调竞争。

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

## PostgreSQL

基于 libpq，目标 PostgreSQL 12+，配置位于 `postgresql.dbs.<name>`：

```cpp
#include <Chen/DB/PostgreSQL.hpp>

auto conn = PostgreSQLMgr::GetInstance()->get("blog");
auto result = conn->query("SELECT * FROM users WHERE id=%d", id);

// 或静态工具
PostgreSQLUtil::Query("blog", "SELECT * FROM users WHERE id=%d", id);
```

参数化 SQL 仍使用 `?` 占位符，驱动会改写为 PostgreSQL 的 `$1..$n`：

```cpp
auto stmt = conn->prepare("SELECT id, name FROM users WHERE name = ? AND status = ?");
stmt->bindString(1, "alice");
stmt->bindInt32(2, 1);
auto result = stmt->query();
```

实现说明：

- `execute/query` 使用 `PQexec`，支持一次发送多条 DDL 语句；`prepare` 使用 `PQexecParams`，
  不创建服务端 prepared statement，避免连接归还后 `DEALLOCATE` 的问题。
- 占位符改写是词法级的：字符串、双引号标识符、行/块注释和 dollar quote 里的 `?` 不会被处理。
  JSONB 的 `?` 运算符无法自动区分，请改用 `jsonb_exists(col, 'key')`。
- `blob` 以二进制格式绑定为 `bytea`，读取时对文本格式结果自动 `PQunescapeBytea`。
- `getLastInsertId()` 使用 `lastval()`，返回当前连接最近一次序列值。
- 事务通过 `openTransaction(name, auto_commit)` 创建。语句出错后 PostgreSQL 会把事务标记为
  aborted，此时 `commit()` 会显式回滚并返回 `false`；`auto_commit` 事务在出错后析构同样回滚，
  不会把未提交数据留在连接池连接里。连接归还连接池时若仍处于事务中，会先执行 `ROLLBACK`，
  回滚失败则丢弃该连接。
- 连接池大小取配置中的 `pool`，默认 5。

## QueryBuilder

参数化 SQL 构建器，位于 `src/DB/QueryBuilder.hpp`，不依赖 ORM，可单独使用；
ORM 的 `Dao::newQuery()` 返回的也是它。

### 流式 API

```cpp
#include <Chen/DB/QueryBuilder.hpp>

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

### 聚合列收窄（SUM / AVG）

MySQL 的 `SUM()` / `AVG()` 返回 DECIMAL。`selectInt64()` / `selectDouble()` 会**追加**一列并包上
`CAST(... AS SIGNED)` / `CAST(... AS DOUBLE)`，省掉手写 CAST：

```cpp
auto qb = QueryBuilder::Create("article")
    ->select("c.id, COUNT(a.id) AS article_count")
    ->selectInt64("COALESCE(SUM(a.views), 0)", "view_count");
// → SELECT c.id, COUNT(a.id) AS article_count,
//          CAST(COALESCE(SUM(a.views), 0) AS SIGNED) AS view_count FROM article
```

与 `select()` 的区别：`select()` 是覆盖语义（DAO 内部也用它），`selectInt64()` / `selectDouble()` 是追加语义。
`SIGNED` 是 MySQL 语法，PostgreSQL / SQLite 的聚合本来就按文本解码，不需要这层包装。

### 结果集类型解码

`ISQLData` 的 getter 语义在各后端一致：`getInt64()` 读整数（DECIMAL / 聚合结果也按文本解析）、
`getDouble()` 读浮点、`getString()` / `getBlob()` 读原始字节（按列长度，不截断 `\0`）、`getTime()` 读时间列。

| 后端                               | 解码方式                                                                                                                                                                             |
|------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| MySQL 文本协议（`MySQLRes`）       | 服务端已把每列转成字符串：整数/浮点走 `TypeUtil::Atoi/Atof`；`getUint64()` 用 `strtoull`，避免 `BIGINT UNSIGNED` 超过 `INT64_MAX` 时被截断                                           |
| MySQL 预处理语句（`MySQLStmtRes`） | 二进制协议，按列的 `storage` 分五类绑定，映射见下表                                                                                                                                  |
| SQLite3                            | 走 `sqlite3_column_int64/double/text`；`getTime()` 兼容 INTEGER（Unix 秒 / 毫秒）、REAL（Unix 秒 / 儒略日）与文本                                                                    |
| PostgreSQL                         | 结果固定按文本返回（`PQexecParams` 的 `resultFormat = 0`）：整数/浮点走 `strtoll/strtod`，`boolean` 的 `t`/`f` 映射成 1/0，时间走 `TimeUtil::Str2Time`（支持 `Z`、`+08:00`、小数秒） |

`MySQLStmtRes` 的列类型映射：

| MySQL 类型                                                                     | 绑定类型              | 读取                                        |
|--------------------------------------------------------------------------------|-----------------------|---------------------------------------------|
| TINY / SHORT / LONG / INT24 / LONGLONG / YEAR / BIT                            | `MYSQL_TYPE_LONGLONG` | 整数；`UNSIGNED_FLAG`（含 BIT）决定符号     |
| FLOAT                                                                          | `MYSQL_TYPE_FLOAT`    | `getFloat()` 直接取，`getDouble()` 精确提升 |
| DOUBLE                                                                         | `MYSQL_TYPE_DOUBLE`   | `getDouble()` 精确                          |
| DECIMAL / NEWDECIMAL                                                           | `MYSQL_TYPE_STRING`   | 文本解析（`SUM()` / `AVG()` 走这里）        |
| TIMESTAMP / DATETIME / DATE / NEWDATE                                          | `MYSQL_TIME`          | `getTime()` 返回时间戳                      |
| TIME                                                                           | `MYSQL_TIME`          | `getTime()` 返回时长秒数（可为负）          |
| VARCHAR / VAR_STRING / STRING / ENUM / SET / JSON / NULL                       | `MYSQL_TYPE_STRING`   | `getString()` / `getBlob()`                 |
| TINYBLOB / BLOB / MEDIUMBLOB / LONGBLOB / GEOMETRY（以及共用枚举的 TEXT 家族） | `MYSQL_TYPE_BLOB`     | 原始字节，不做字符集转换                    |
| 其它（TIMESTAMP2 等）                                                          | `MYSQL_TYPE_STRING`   | 文本兜底                                    |

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
> 见 [`src/Orm/README.md`](../Orm/README.md)。

## 分库分表

框架在 `src/DB/Shard.hpp` 提供独立的分片路由层，与 ORM 解耦，可用于任何数据库操作。

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
#include <Chen/DB/Shard.hpp>

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
[`src/Orm/README.md`](../Orm/README.md)。
