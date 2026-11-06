# ORM — 对象关系映射

自研轻量 ORM 代码生成器，从 XML 表定义生成业务结构体（`*Info`）和 DAO 类（`*Dao`），支持 SQLite、MySQL 和 PostgreSQL，参数化查询。

> 本文只讲表定义、代码生成和 DAO。参数化 SQL 构建器（`chen/db/query_builder.h`）和
> 分库分表（`chen/db/shard.h`）属于 db 模块，完整说明见 [`chen/db/README.md`](../db/README.md)。

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

| 属性        | 说明                                                                                   | 默认值    |
|-------------|----------------------------------------------------------------------------------------|-----------|
| `name`      | 数据库表名                                                                             | 必填      |
| `namespace` | C++ 命名空间（`.` 分隔）                                                               | 必填      |
| `db`        | 数据库方言：`sqlite3` / `mysql` / `postgresql`（也接受 `sqlite`、`postgres`、`pgsql`） | `sqlite3` |
| `version`   | 数据库版本号，用于版本化迁移                                                           | 1         |
| `desc`      | 表注释                                                                                 | 空        |

> 不写 `db` 时按 SQLite3 生成。已有的 MySQL 项目需要显式补上 `db="mysql"`，
> 否则建表、迁移和 DAO SQL 都会按 SQLite3 方言生成。非法 `db` 值会让生成器直接失败。

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

| 方法                         | 说明                                                                                 |
|------------------------------|--------------------------------------------------------------------------------------|
| `Insert(info, conn)`         | 插入新记录，自动回填自增主键，成功后 `markClean()`                                   |
| `InsertOrUpdate(info, conn)` | 插入或更新（MySQL: `REPLACE INTO`；PostgreSQL: `ON CONFLICT`），成功后 `markClean()` |
| `Update(info, conn)`         | 只更新被修改过的字段（脏字段追踪），成功后 `markClean()`                             |
| `Delete(info, conn)`         | 按主键删除                                                                           |
| `Delete(id, conn)`           | 按主键值删除                                                                         |
| `DeleteBy<Col>(val, conn)`   | 按索引列删除                                                                         |
| `BatchInsert(infos, conn)`   | 单条 SQL 批量插入多行                                                                |
| `BatchUpdate(infos, conn)`   | 事务内逐条更新，一条失败全部回滚                                                     |
| `BatchDelete(ids, conn)`     | DELETE WHERE id IN (...)                                                             |

### 查询

| 方法                                                          | 说明                                           |
|---------------------------------------------------------------|------------------------------------------------|
| `QueryAll(results, conn)`                                     | 查询全表                                       |
| `Query(id, conn)`                                             | 按主键查单条，返回 `ptr`（未找到返回 nullptr） |
| `QueryBy<Col>(val, conn)`                                     | 按唯一索引查单条，返回 `ptr`                   |
| `QueryBy<Col>(results, val, conn)`                            | 按普通索引查多条                               |
| `QueryBy<Col>Pages(results, total, val, offset, limit, conn)` | 按索引分页查询，含 COUNT                       |

### 自定义查询（QueryBuilder）

通过 `Dao::newQuery()` 获取预配置好表名的 QueryBuilder，无需硬编码表名。需要表别名时可以传入别名参数。
QueryBuilder 本身位于 `chen/db/query_builder.h`，完整 API 见 [`chen/db/README.md`](../db/README.md#querybuilder)。

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

> **注意**：`QueryByBuilder` 和 `QueryByBuilderPages` 会自动覆盖 QueryBuilder 的 SELECT 列为表全部列（保证结果能正确映射到
> Info 结构体），其他子句（WHERE、JOIN、ORDER BY、GROUP BY、HAVING）正常生效。

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

QueryBuilder 是 db 模块的组件（`chen/db/query_builder.h`），流式条件、JOIN、聚合、子查询、
INSERT / UPDATE / DELETE 与直接执行的完整说明见 [`chen/db/README.md`](../db/README.md#querybuilder)。

---

## XML 列类型参考

| XML type    | C++ 类型      | SQLite    | MySQL             | PostgreSQL                  |
|-------------|---------------|-----------|-------------------|-----------------------------|
| `int8`      | `int8_t`      | INTEGER   | tinyint           | smallint                    |
| `int16`     | `int16_t`     | INTEGER   | smallint          | smallint                    |
| `int32`     | `int32_t`     | INTEGER   | int               | integer                     |
| `int64`     | `int64_t`     | INTEGER   | bigint            | bigint                      |
| `uint8`     | `uint8_t`     | INTEGER   | tinyint unsigned  | smallint                    |
| `uint16`    | `uint16_t`    | INTEGER   | smallint unsigned | integer                     |
| `uint32`    | `uint32_t`    | INTEGER   | int unsigned      | bigint                      |
| `uint64`    | `uint64_t`    | INTEGER   | bigint unsigned   | numeric(20,0)               |
| `float`     | `float`       | REAL      | float             | real                        |
| `double`    | `double`      | REAL      | double            | double precision            |
| `string`    | `std::string` | TEXT      | varchar(N)        | character varying(N)        |
| `text`      | `std::string` | TEXT      | text              | text                        |
| `blob`      | `std::string` | BLOB      | blob              | bytea                       |
| `datetime`  | `int64_t`     | TIMESTAMP | datetime          | timestamp without time zone |
| `timestamp` | `int64_t`     | TIMESTAMP | timestamp         | timestamp without time zone |

> `datetime` 在 MySQL 中生成 `DATETIME`，支持 `1970-01-01` 及 2038 之后的时间；空时间默认值为
> `'1970-01-01 00:00:00'`。
> `timestamp` 生成 MySQL `TIMESTAMP`，受 `1970-01-01 00:00:01` ~ `2038-01-19 03:14:07` 范围限制，
> 空时间默认值仍为 `'1980-01-01 00:00:00'`，需要宽范围时间时请使用 `datetime`。
>
> PostgreSQL 下 `datetime` / `timestamp` 都映射为 `timestamp without time zone`，空时间默认值
> 为 `'1970-01-01 00:00:00'`；`uint64` 映射为 `numeric(20,0)`，因此不能作为自增列。
> 数值默认值为 `0`，字符串为空串，`blob` 为 `''::bytea`。

## DDL 迁移

```cpp
// 建表
CategoryInfoDao::CreateTableSQLite3(conn);
CategoryInfoDao::CreateTableMySQL(conn);
CategoryInfoDao::CreateTablePostgreSQL(conn);

// 按 XML 定义自动迁移表结构（ADD/MODIFY/DROP COLUMN）
CategoryInfoDao::MigrateTableSQLite3(conn);
CategoryInfoDao::MigrateTableMySQL(conn);
CategoryInfoDao::MigrateTablePostgreSQL(conn);
```

- SQLite3 迁移：检测类型变更或删除列时自动 `RENAME → CREATE → INSERT → DROP` 重建表
- MySQL 迁移：使用 `MODIFY COLUMN` / `DROP COLUMN` / `ADD COLUMN` 逐列修改
- PostgreSQL 迁移：查询 `pg_attribute` 获取现有列，使用 `ALTER COLUMN ... TYPE / SET NOT NULL / SET DEFAULT`
  和 `ADD/DROP COLUMN` 逐列修改，并同步注释、索引和更新触发器（不重建表）
- `Migrate()` 会自动按 XML 的 `db` 选择对应方言的建表/迁移函数

## PostgreSQL

表定义写 `db="postgresql"` 后，生成的代码使用 PostgreSQL 方言：

- 表名、列名、索引名统一使用双引号标识符。
- 自增列生成 `GENERATED BY DEFAULT AS IDENTITY`；只允许一个自增列，且必须是整数类型
  （`uint64` 映射为 `numeric`，会被拒绝）。
- `Insert` 使用 `RETURNING` 回填自增主键；`InsertOrUpdate` 使用
  `ON CONFLICT (<主键>) DO UPDATE`，没有主键时退化为 `ON CONFLICT DO NOTHING`。
- `update="current_timestamp"`（或 `now()`）通过 `BEFORE UPDATE` 触发器实现；
  触发器函数和触发器名均为 `<table>_update`。PG 12/13 不支持 `CREATE OR REPLACE TRIGGER`，
  因此每次先 `DROP TRIGGER IF EXISTS` 再创建。
- 普通索引和唯一索引以 `<table>_<col1>_<col2>` 命名，建表时使用
  `CREATE [UNIQUE] INDEX IF NOT EXISTS`，迁移时先 `DROP INDEX IF EXISTS` 再重建。
- 生成 DAO 的 SQL 仍使用 `?` 占位符，由 PostgreSQL 驱动改写为 `$1..$n`。
  SQL 中作为运算符的 `?`（如 JSONB）无法与占位符区分，请改用 `jsonb_exists()`。
- `InsertOrUpdate` 显式指定自增主键时不会自动推进 identity 序列，后续不指定主键的插入
  可能发生主键冲突，需要业务侧同步序列或避免显式写入自增主键。

> 运行期驱动配置见 [`docs/db-configuration.md`](../../docs/db-configuration.md#postgresql) 和
> [`chen/db/README.md`](../db/README.md#postgresql)。

---

## 分库分表

分片路由是 db 模块的组件（`chen/db/shard.h`），与 ORM 解耦，核心概念、初始化、
路由用法与自定义策略见 [`chen/db/README.md`](../db/README.md#分库分表)。
