#include "Shard.hpp"

#include <ctime>

#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

ShardKey ShardKey::Int64(int64_t v)
{
    ShardKey k;
    k.type_ = INT64;
    k.int_val_ = v;
    return k;
}

ShardKey ShardKey::String(const std::string& v)
{
    ShardKey k;
    k.type_ = STRING;
    k.str_val_ = v;
    return k;
}

ShardKey ShardKey::Time(time_t v)
{
    ShardKey k;
    k.type_ = TIME;
    k.time_val_ = v;
    return k;
}

// ============================================================================
// HashShardStrategy
// ============================================================================

HashShardStrategy::HashShardStrategy(const std::string& db_format, int shard_count, const std::string& table_format)
    : db_format_(db_format)
    , table_format_(table_format)
    , shard_count_(shard_count)
{
    if (shard_count_ <= 0) {
        ERROR(g_logger) << "HashShardStrategy: shardCount must be > 0, got " << shard_count_;
        shard_count_ = 1;
    }
}

ShardTarget HashShardStrategy::route(const std::string& /*logical_table*/, const ShardKey& key)
{
    ShardTarget target;

    int64_t hash_val = 0;
    switch (key.getType()) {
    case ShardKey::INT64:
        hash_val = key.getInt64();
        break;
    case ShardKey::STRING: {
        // std::hash<std::string> for consistent hashing
        hash_val = static_cast<int64_t>(std::hash<std::string>{}(key.getString()));
        break;
    }
    default:
        ERROR(g_logger) << "HashShardStrategy: unsupported key type " << key.getType();
        return target;
    }

    int shard = static_cast<int>((hash_val % shard_count_ + shard_count_) % shard_count_);

    // Replace {shard} in db format
    std::string db_name = db_format_;
    auto pos = db_name.find("{shard}");
    if (pos != std::string::npos) {
        db_name.replace(pos, 7, std::to_string(shard));
    }
    target.db_name = db_name;

    // Replace {shard} in table format if set
    if (!table_format_.empty()) {
        std::string table_name = table_format_;
        pos = table_name.find("{shard}");
        if (pos != std::string::npos) {
            table_name.replace(pos, 7, std::to_string(shard));
        }
        target.table_name = table_name;
    }

    return target;
}

// ============================================================================
// DateShardStrategy
// ============================================================================

DateShardStrategy::DateShardStrategy(const std::string& format)
    : format_(format)
{
}

ShardTarget DateShardStrategy::route(const std::string& /*logical_table*/, const ShardKey& key)
{
    ShardTarget target;

    if (key.getType() != ShardKey::TIME) {
        ERROR(g_logger) << "DateShardStrategy: expected TIME key, got " << key.getType();
        return target;
    }

    time_t t = key.getTime();
    struct tm tm_buf = {};
    localtime_r(&t, &tm_buf);

    char buf[128] = {};
    strftime(buf, sizeof(buf), format_.c_str(), &tm_buf);
    target.table_name = buf;

    return target;
}

// ============================================================================
// ShardRouter
// ============================================================================

void ShardRouter::addRule(const std::string& logical_table, IShardStrategy::ptr strategy)
{
    rules_[logical_table] = std::move(strategy);
}

void ShardRouter::addDB(const std::string& db_name, IDB::ptr conn)
{
    dbs_[db_name] = std::move(conn);
}

ShardTarget ShardRouter::route(const std::string& logical_table, const ShardKey& key)
{
    auto it = rules_.find(logical_table);
    if (it == rules_.end()) {
        // No sharding rule — return unchanged (same DB, same table)
        return ShardTarget{};
    }
    return it->second->route(logical_table, key);
}

IDB::ptr ShardRouter::getDB(const ShardTarget& target)
{
    if (target.db_name.empty()) {
        return nullptr;
    }
    auto it = dbs_.find(target.db_name);
    if (it == dbs_.end()) {
        ERROR(g_logger) << "ShardRouter: DB '" << target.db_name << "' not registered";
        return nullptr;
    }
    return it->second;
}

std::pair<IDB::ptr, std::string> ShardRouter::resolve(const std::string& logical_table, const ShardKey& key)
{
    auto target = route(logical_table, key);
    auto db = target.hasDB() ? getDB(target) : nullptr;
    return {db, target.table_name};
}

} // namespace chen
