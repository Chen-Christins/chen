# ORM — 对象关系映射

自研轻量 ORM 代码生成器，从 XML 表定义生成业务结构体（`*Info`）和 DAO 类（`*Dao`），支持 SQLite 和 MySQL，参数化查询。

## 快速开始

### 1. 编写 XML 表定义

```xml
<!-- orm_conf/category.xml -->
<table name="category" namespace="blog.data" version="1" desc="文章分类表">
    <columns>
        <column name="id" type="int64" desc="主键" auto_increment="true"/>
        <column name="name" type="string" length="50" desc="分类名" default=""/>
        <column name="parent_id" type="int64" desc="父分类ID" default="0"/>
        <column name="status" type="int32" desc="0:停用 1:启用" default="0"/>
    </columns>
    <indexs>
        <index name="PK" type="pk" cols="id"/>
        <index name="unique_name" type="uniq" cols="name"/>
    </indexs>
</table>
```

| 属性 | 说明 | 默认值 |
|------|------|--------|
| `name` | 数据库表名 | 必填 |
| `namespace` | C++ 命名空间（`.` 分隔） | 必填 |
| `version` | 数据库版本号，用于版本化迁移 | 1 |
| `desc` | 表注释 | 空 |

### 2. 运行代码生成器

```bash
bin/orm                                    # 默认扫描 ./orm_conf/*.xml
bin/orm -i /path/to/xml -o /path/to/output # 指定输入输出目录
```

生成文件：
- `category_info.h` — 业务结构体 `CategoryInfo` 及 DAO 声明 `CategoryInfoDao`
- `category_info.cc` — DAO 实现

### 3. 使用生成的代码

```cpp
#include "blog/data/category_info.h"

using namespace blog::data;

// 插入
auto info = std::make_shared<CategoryInfo>();
info->setName("技术文章");
info->setStatus(1);
CategoryInfoDao::Insert(info, conn);

// 按主键查询
auto cat = CategoryInfoDao::Query(1, conn);
if (cat) {
    std::cout << cat->getName() << std::endl;
}

// 按唯一索引查询
auto cat2 = CategoryInfoDao::QueryByName("技术文章", conn);

// 查询全部
std::vector<CategoryInfo::ptr> list;
CategoryInfoDao::QueryAll(list, conn);
```

---

## 生成的 DAO 方法

### 写入

| 方法 | 说明 |
|------|------|
| `Insert(info, conn)` | 插入新记录，自动回填自增主键，成功后 `markClean()` |
| `InsertOrUpdate(info, conn)` | 插入或替换（MySQL: REPLACE INTO），成功后 `markClean()` |
| `Update(info, conn)` | 只更新被修改过的字段（脏字段追踪），成功后 `markClean()` |
| `Delete(info, conn)` | 按主键删除 |
| `Delete(id, conn)` | 按主键值删除 |
| `DeleteBy<Col>(val, conn)` | 按索引列删除 |
| `BatchInsert(infos, conn)` | 单条 SQL 批量插入多行 |
| `BatchUpdate(infos, conn)` | 事务内逐条更新，一条失败全部回滚 |
| `BatchDelete(ids, conn)` | DELETE WHERE id IN (...) |

### 查询

| 方法 | 说明 |
|------|------|
| `QueryAll(results, conn)` | 查询全表 |
| `Query(id, conn)` | 按主键查单条，返回 `ptr`（未找到返回 nullptr） |
| `QueryBy<Col>(val, conn)` | 按唯一索引查单条，返回 `ptr` |
| `QueryBy<Col>(results, val, conn)` | 按普通索引查多条 |
| `QueryBy<Col>Pages(results, total, val, offset, limit, conn)` | 按索引分页查询，含 COUNT |

### 自定义查询（QueryBuilder）

通过 `Dao::newQuery()` 获取预配置好表名的 QueryBuilder，无需硬编码表名。需要表别名时可以传入别名参数。

```cpp
#include <chen/db/query_builder.h>

// 无别名
auto qb = CategoryInfoDao::newQuery()
    ->whereLike("name", "技术")              // name LIKE '%技术%'
    ->where("status", "=", 1)
    ->orderBy("parent_id", "ASC")
    ->limit(20);

// 带别名（用于 JOIN 多表查询）
auto qb = CategoryInfoDao::newQuery("c")
    ->join("articles a", "c.id = a.category_id")
    ->where("a.status", "=", 1);

std::vector<CategoryInfo::ptr> results;
CategoryInfoDao::QueryByBuilder(results, qb, conn);
```

#### QueryByBuilderPages — 分页自定义查询

```cpp
auto qb = CategoryInfoDao::newQuery()
    ->where("status", "=", 1)
    ->whereNotNull("url");                   // url IS NOT NULL

int64_t total = 0;
std::vector<CategoryInfo::ptr> results;
CategoryInfoDao::QueryByBuilderPages(results, total, qb, 0, 20, conn);
```

#### ParseRow — 手动解析

```cpp
auto data = qb->executeQuery(conn);
if (data) {
    while (data->next()) {
        auto info = CategoryInfoDao::ParseRow(data);
    }
}
```

> **注意**：`QueryByBuilder` 和 `QueryByBuilderPages` 会自动覆盖 QueryBuilder 的 SELECT 列为表全部列（保证结果能正确映射到 Info 结构体），其他子句（WHERE、JOIN、ORDER BY、GROUP BY、HAVING）正常生效。

### 数据库迁移

```cpp
// 版本化迁移：自动检测 DB 版本，链式执行到 XML 定义版本
CategoryInfoDao::Migrate(conn);

// 版本间数据迁移：覆盖生成的 onMigrate_vN 钩子
// 在 category_info.cc 中：
int CategoryInfoDao::onMigrate_v2(IDB::ptr conn) {
    // v1 → v2：拆分 name 为 first_name + last_name
    conn->execute("UPDATE category SET first_name = ...");
    return 0;
}
```

迁移内部自动维护 `schema_version` 表，不需手动管理。

---

## 脏字段追踪

每个 Info 对象维护 `m_flags` 位图，通过 setter 自动标记变更：

```cpp
auto info = CategoryInfoDao::Query(1, conn);   // m_flags = 0
info->setName("新名称");                         // m_flags |= bit(1)
info->setStatus(0);                              // m_flags |= bit(3)
CategoryInfoDao::Update(info, conn);             // 只 UPDATE name, status
// m_flags 自动归零

info->isDirty();   // 是否有未保存修改
info->markClean(); // 手动清除标记
```

Insert / InsertOrUpdate 成功后也会自动清除标记，新创建的对象所有字段均视为 clean。

---

## QueryBuilder 参考

### 流式 API

```cpp
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

---

## XML 列类型参考

| XML type | C++ 类型 | SQLite | MySQL |
|----------|---------|--------|-------|
| `int8` | `int8_t` | INTEGER | tinyint |
| `int16` | `int16_t` | INTEGER | smallint |
| `int32` | `int32_t` | INTEGER | int |
| `int64` | `int64_t` | INTEGER | bigint |
| `uint8` | `uint8_t` | INTEGER | tinyint unsigned |
| `float` | `float` | REAL | float |
| `double` | `double` | REAL | double |
| `string` | `std::string` | TEXT | varchar(N) |
| `text` | `std::string` | TEXT | text |
| `blob` | `std::string` | BLOB | blob |
| `timestamp` | `int64_t` | TIMESTAMP | timestamp |

## DDL 迁移

```cpp
// 建表
CategoryInfoDao::CreateTableSQLite3(conn);
CategoryInfoDao::CreateTableMySQL(conn);

// 按 XML 定义自动迁移表结构（ADD/MODIFY/DROP COLUMN）
CategoryInfoDao::MigrateTableSQLite3(conn);
CategoryInfoDao::MigrateTableMySQL(conn);
```

- SQLite3 迁移：检测类型变更或删除列时自动 `RENAME → CREATE → INSERT → DROP` 重建表
- MySQL 迁移：使用 `MODIFY COLUMN` / `DROP COLUMN` / `ADD COLUMN` 逐列修改

---

## 分库分表

框架在 `chen/db/shard.h` 提供独立的分片路由层，与 ORM 解耦，可用于任何数据库操作。

### 核心概念

| 类 | 说明 |
|----|------|
| `ShardKey` | 分片键，支持 `Int64`（hash 取模）、`String`（租户）、`Time`（日期） |
| `ShardTarget` | 路由结果：逻辑库名 + 物理表名 |
| `IShardStrategy` | 可插拔策略接口，实现 `route(logicalTable, key)` 即可自定义 |
| `HashShardStrategy` | Hash/取模 分库，可选分表 |
| `DateShardStrategy` | 按时间分表（strftime 格式化表名） |
| `ShardRouter` | Singleton 路由入口，管理规则和连接池 |

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
