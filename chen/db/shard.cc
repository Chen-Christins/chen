#include "shard.h"

#include <ctime>

#include "../log/log.h"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

// ============================================================================
// HashShardStrategy
// ============================================================================

HashShardStrategy::HashShardStrategy(const std::string& dbFormat, int shardCount, const std::string& tableFormat)
    : m_dbFormat(dbFormat)
    , m_tableFormat(tableFormat)
    , m_shardCount(shardCount) {
    if (m_shardCount <= 0) {
        ERROR(g_logger) << "HashShardStrategy: shardCount must be > 0, got " << m_shardCount;
        m_shardCount = 1;
    }
}

ShardTarget HashShardStrategy::route(const std::string& /*logicalTable*/, const ShardKey& key) {
    ShardTarget target;

    int64_t hashVal = 0;
    switch (key.getType()) {
    case ShardKey::INT64:
        hashVal = key.getInt64();
        break;
    case ShardKey::STRING: {
        // std::hash<std::string> for consistent hashing
        hashVal = static_cast<int64_t>(std::hash<std::string>{}(key.getString()));
        break;
    }
    default:
        ERROR(g_logger) << "HashShardStrategy: unsupported key type " << key.getType();
        return target;
    }

    int shard = static_cast<int>((hashVal % m_shardCount + m_shardCount) % m_shardCount);

    // Replace {shard} in db format
    std::string dbName = m_dbFormat;
    auto pos = dbName.find("{shard}");
    if (pos != std::string::npos) {
        dbName.replace(pos, 7, std::to_string(shard));
    }
    target.dbName = dbName;

    // Replace {shard} in table format if set
    if (!m_tableFormat.empty()) {
        std::string tableName = m_tableFormat;
        pos = tableName.find("{shard}");
        if (pos != std::string::npos) {
            tableName.replace(pos, 7, std::to_string(shard));
        }
        target.tableName = tableName;
    }

    return target;
}

// ============================================================================
// DateShardStrategy
// ============================================================================

DateShardStrategy::DateShardStrategy(const std::string& format)
    : m_format(format) {
}

ShardTarget DateShardStrategy::route(const std::string& /*logicalTable*/, const ShardKey& key) {
    ShardTarget target;

    if (key.getType() != ShardKey::TIME) {
        ERROR(g_logger) << "DateShardStrategy: expected TIME key, got " << key.getType();
        return target;
    }

    time_t t = key.getTime();
    struct tm tm_buf = {};
    localtime_r(&t, &tm_buf);

    char buf[128] = {};
    strftime(buf, sizeof(buf), m_format.c_str(), &tm_buf);
    target.tableName = buf;

    return target;
}

// ============================================================================
// ShardRouter
// ============================================================================

void ShardRouter::addRule(const std::string& logicalTable, IShardStrategy::ptr strategy) {
    m_rules[logicalTable] = std::move(strategy);
}

void ShardRouter::addDB(const std::string& dbName, IDB::ptr conn) {
    m_dbs[dbName] = std::move(conn);
}

ShardTarget ShardRouter::route(const std::string& logicalTable, const ShardKey& key) {
    auto it = m_rules.find(logicalTable);
    if (it == m_rules.end()) {
        // No sharding rule — return unchanged (same DB, same table)
        return ShardTarget{};
    }
    return it->second->route(logicalTable, key);
}

IDB::ptr ShardRouter::getDB(const ShardTarget& target) {
    if (target.dbName.empty()) {
        return nullptr;
    }
    auto it = m_dbs.find(target.dbName);
    if (it == m_dbs.end()) {
        ERROR(g_logger) << "ShardRouter: DB '" << target.dbName << "' not registered";
        return nullptr;
    }
    return it->second;
}

std::pair<IDB::ptr, std::string> ShardRouter::resolve(const std::string& logicalTable, const ShardKey& key) {
    auto target = route(logicalTable, key);
    auto db = target.hasDB() ? getDB(target) : nullptr;
    return {db, target.tableName};
}

} // namespace chen
