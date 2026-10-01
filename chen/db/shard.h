/**
 * @file shard.h
 * @brief 分库分表路由框架，提供 ShardKey（分片键）、ShardTarget（路由结果）、
 *        IShardStrategy（可插拔策略）、ShardRouter（路由入口）
 * @author Christins
 * @date 2026-07-02
 * @copyright GPL-3.0
 */
#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <unordered_map>

#include "../util/singleton.h"
#include "db.h"

namespace chen {

// ============================================================================
// ShardKey — 分片键
// ============================================================================

class ShardKey {
public:
    enum Type { NONE, INT64, STRING, TIME };

    static ShardKey Int64(int64_t v) {
        ShardKey k;
        k.m_type = INT64;
        k.m_intVal = v;
        return k;
    }
    static ShardKey String(const std::string& v) {
        ShardKey k;
        k.m_type = STRING;
        k.m_strVal = v;
        return k;
    }
    static ShardKey Time(time_t v) {
        ShardKey k;
        k.m_type = TIME;
        k.m_timeVal = v;
        return k;
    }

    Type getType() const { return m_type; }
    int64_t getInt64() const { return m_intVal; }
    const std::string& getString() const { return m_strVal; }
    time_t getTime() const { return m_timeVal; }

private:
    ShardKey() = default;
    Type m_type = NONE;
    int64_t m_intVal = 0;
    std::string m_strVal;
    time_t m_timeVal = 0;
};

// ============================================================================
// ShardTarget — 路由结果
// ============================================================================

struct ShardTarget {
    std::string dbName;     /**< 逻辑库名，用于从 ShardRouter 获取对应连接 */
    std::string tableName;  /**< 物理表名，空字符串表示不改变表名 */

    bool hasDB() const { return !dbName.empty(); }
    bool hasTable() const { return !tableName.empty(); }
};

// ============================================================================
// IShardStrategy — 可插拔分片策略接口
// ============================================================================

class IShardStrategy {
public:
    typedef std::shared_ptr<IShardStrategy> ptr;
    virtual ~IShardStrategy() = default;

    /**
     * @brief 根据逻辑表名和分片键计算物理目标
     * @param logicalTable 逻辑表名（XML 中的表名）
     * @param key 分片键
     * @return 目标库名和表名
     */
    virtual ShardTarget route(const std::string& logicalTable, const ShardKey& key) = 0;
};

// ============================================================================
// HashShardStrategy — Hash / 取模 策略
// ============================================================================

/**
 * @brief 按 hash(key) % shardCount 路由到不同分库（可选分表）
 *
 * dbFormat 中的 {shard} 替换为分片编号。
 * 如果 tableFormat 非空，同时将表名中的 {shard} 替换。
 *
 * 示例：
 *   HashShardStrategy("user_shard_{shard}", 4)
 *     route("user", Int64(7))  → dbName="user_shard_3", tableName=""
 *   HashShardStrategy("db_{shard}", 8, "user_{shard}")
 *     route("user", Int64(7))  → dbName="db_7", tableName="user_7"
 */
class HashShardStrategy : public IShardStrategy {
public:
    typedef std::shared_ptr<HashShardStrategy> ptr;

    /**
     * @param dbFormat   库名模板，{shard} 替换为分片编号
     * @param shardCount 分片总数
     * @param tableFormat 表名模板（可选），为空则只分库不分表
     */
    HashShardStrategy(const std::string& dbFormat, int shardCount,
                      const std::string& tableFormat = "");

    ShardTarget route(const std::string& logicalTable, const ShardKey& key) override;

private:
    std::string m_dbFormat;
    std::string m_tableFormat;
    int m_shardCount;
};

// ============================================================================
// DateShardStrategy — 日期分表策略
// ============================================================================

/**
 * @brief 按时间分表，表名由 strftime(format, key) 生成
 *
 * 只分表不分库，dbName 为空字符串。
 *
 * 示例：
 *   DateShardStrategy("log_%Y%m")  → route("log", Time(now)) → tableName="log_202607"
 *   DateShardStrategy("log_%Y%m%d") → route("log", Time(now)) → tableName="log_20260702"
 */
class DateShardStrategy : public IShardStrategy {
public:
    typedef std::shared_ptr<DateShardStrategy> ptr;

    /**
     * @param format strftime 格式字符串，如 "log_%Y%m"、"event_%Y%m%d"
     */
    explicit DateShardStrategy(const std::string& format);

    ShardTarget route(const std::string& logicalTable, const ShardKey& key) override;

private:
    std::string m_format;
};

// ============================================================================
// ShardRouter — 分片路由入口
// ============================================================================

class ShardRouter {
public:
    typedef std::shared_ptr<ShardRouter> ptr;

    // ---- 注册 ----

    /**
     * @brief 注册分片规则
     * @param logicalTable 逻辑表名
     * @param strategy 分片策略
     */
    void addRule(const std::string& logicalTable, IShardStrategy::ptr strategy);

    /**
     * @brief 注册数据库连接
     * @param dbName 逻辑库名
     * @param conn 数据库连接
     */
    void addDB(const std::string& dbName, IDB::ptr conn);

    // ---- 路由 ----

    /**
     * @brief 查询分片目标
     * @param logicalTable 逻辑表名
     * @param key 分片键
     * @return 目标库名和物理表名
     */
    ShardTarget route(const std::string& logicalTable, const ShardKey& key);

    /**
     * @brief 获取目标库的数据库连接
     * @param target 路由结果
     * @return 连接指针，未注册时返回 nullptr
     */
    IDB::ptr getDB(const ShardTarget& target);

    /**
     * @brief 一步到位：路由 + 获取连接
     * @return (连接, 物理表名)
     */
    std::pair<IDB::ptr, std::string> resolve(const std::string& logicalTable, const ShardKey& key);

private:
    std::unordered_map<std::string, IShardStrategy::ptr> m_rules;

    std::unordered_map<std::string, IDB::ptr> m_dbs;
};

typedef chen::Singleton<ShardRouter> ShardRouterMgr;

} // namespace chen
