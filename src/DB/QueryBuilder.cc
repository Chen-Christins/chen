#include "QueryBuilder.hpp"

#include <sstream>

#include "../Log/Log.hpp"

namespace chen {

static chen::Logger::ptr g_logger = LOG_NAME("system");

QueryBuilder::ptr QueryBuilder::Create(const std::string& table)
{
    QueryBuilder::ptr qb(new QueryBuilder);
    qb->table_ = table;
    return qb;
}

QueryBuilder::ptr QueryBuilder::select(const std::string& cols)
{
    select_cols_ = cols;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::appendSelect(const std::string& expr)
{
    if (select_cols_.empty() || select_cols_ == "*") {
        select_cols_ = expr;
    } else {
        select_cols_ += ", ";
        select_cols_ += expr;
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::selectInt64(const std::string& expr, const std::string& alias)
{
    return appendSelect("CAST(" + expr + " AS SIGNED)" + (alias.empty() ? "" : " AS " + alias));
}

QueryBuilder::ptr QueryBuilder::selectDouble(const std::string& expr, const std::string& alias)
{
    return appendSelect("CAST(" + expr + " AS DOUBLE)" + (alias.empty() ? "" : " AS " + alias));
}

// ---- Internal helpers ----

void QueryBuilder::addWhereClause(const std::string& conjunction, const std::string& clause, Param p)
{
    where_clauses_.push_back(clause);
    where_conjunctions_.push_back(conjunction);
    where_params_.push_back(std::move(p));
}

void QueryBuilder::addHavingClause(const std::string& clause, Param p)
{
    having_clauses_.push_back(clause);
    having_params_.push_back(std::move(p));
}

void QueryBuilder::bindParamArray(chen::IStmt::ptr stmt, const std::vector<Param>& params, int& idx) const
{
    if (!stmt) {
        ERROR(g_logger) << "QueryBuilder::bindParamArray stmt is null";
        return;
    }
    for (auto& p : params) {
        switch (p.type) {
        case Param::INT32_VAL:
            stmt->bindInt32(idx, static_cast<int32_t>(p.int_val));
            break;
        case Param::INT64_VAL:
            stmt->bindInt64(idx, p.int_val);
            break;
        case Param::STRING_VAL:
            stmt->bindString(idx, p.str_val);
            break;
        case Param::FLOAT_VAL:
            stmt->bindFloat(idx, static_cast<float>(p.dbl_val));
            break;
        case Param::DOUBLE_VAL:
            stmt->bindDouble(idx, p.dbl_val);
            break;
        }
        ++idx;
    }
}

void QueryBuilder::buildWhereClause(std::stringstream& ss) const
{
    if (!where_clauses_.empty()) {
        ss << " WHERE ";
        for (size_t i = 0; i < where_clauses_.size(); ++i) {
            if (i > 0) {
                ss << " " << where_conjunctions_[i] << " ";
            }
            ss << where_clauses_[i];
        }
    }
}

// ---- WHERE (AND) ----

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, int64_t val)
{
    addWhereClause("AND", col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, const std::string& val)
{
    addWhereClause("AND", col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, double val)
{
    addWhereClause("AND", col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, int64_t val)
{
    if (condition)
        return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, const std::string& val)
{
    if (condition)
        return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, double val)
{
    if (condition)
        return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, int64_t val)
{
    addWhereClause("AND", sql, {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, const std::string& val)
{
    addWhereClause("AND", sql, {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, double val)
{
    addWhereClause("AND", sql, {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, int64_t val)
{
    if (condition)
        return whereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, const std::string& val)
{
    if (condition)
        return whereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, double val)
{
    if (condition)
        return whereSQL(sql, val);
    return shared_from_this();
}

// ---- WHERE (OR) ----

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, int64_t val)
{
    addWhereClause("OR", col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, const std::string& val)
{
    addWhereClause("OR", col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, double val)
{
    addWhereClause("OR", col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, int64_t val)
{
    if (condition)
        return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, const std::string& val)
{
    if (condition)
        return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, double val)
{
    if (condition)
        return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, int64_t val)
{
    addWhereClause("OR", sql, {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, const std::string& val)
{
    addWhereClause("OR", sql, {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, double val)
{
    addWhereClause("OR", sql, {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, int64_t val)
{
    if (condition)
        return orWhereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, const std::string& val)
{
    if (condition)
        return orWhereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, double val)
{
    if (condition)
        return orWhereSQL(sql, val);
    return shared_from_this();
}

// ---- WHERE IN ----

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, const std::vector<int64_t>& vals)
{
    if (vals.empty()) {
        return shared_from_this();
    }
    std::stringstream ss;
    ss << col << " IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
            ss << ", ";
        ss << "?";
    }
    ss << ")";
    where_clauses_.push_back(ss.str());
    where_conjunctions_.push_back("AND");
    for (auto v : vals) {
        where_params_.push_back({Param::INT64_VAL, v, 0.0, ""});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, const std::vector<std::string>& vals)
{
    if (vals.empty()) {
        return shared_from_this();
    }
    std::stringstream ss;
    ss << col << " IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
            ss << ", ";
        ss << "?";
    }
    ss << ")";
    where_clauses_.push_back(ss.str());
    where_conjunctions_.push_back("AND");
    for (auto& v : vals) {
        where_params_.push_back({Param::STRING_VAL, 0, 0.0, v});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, QueryBuilder::ptr sub)
{
    if (!sub)
        return shared_from_this();
    // Build subquery SQL without pagination (subquery in IN clause shouldn't have LIMIT/OFFSET)
    std::string sub_sql = sub->buildQuerySQL(sub->select_cols_, false);
    where_clauses_.push_back(col + " IN (" + sub_sql + ")");
    where_conjunctions_.push_back("AND");
    // Merge subquery's WHERE + HAVING params into parent
    for (auto& p : sub->where_params_) {
        where_params_.push_back(p);
    }
    for (auto& p : sub->having_params_) {
        where_params_.push_back(p);
    }
    return shared_from_this();
}

// ---- NULL / NOT NULL ----

QueryBuilder::ptr QueryBuilder::whereNull(const std::string& col)
{
    where_clauses_.push_back(col + " IS NULL");
    where_conjunctions_.push_back("AND");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotNull(const std::string& col)
{
    where_clauses_.push_back(col + " IS NOT NULL");
    where_conjunctions_.push_back("AND");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNullIf(bool cond, const std::string& col)
{
    if (cond)
        return whereNull(col);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotNullIf(bool cond, const std::string& col)
{
    if (cond)
        return whereNotNull(col);
    return shared_from_this();
}

// ---- WHERE NOT IN ----

QueryBuilder::ptr QueryBuilder::whereNotIn(const std::string& col, const std::vector<int64_t>& vals)
{
    if (vals.empty())
        return shared_from_this();
    std::stringstream ss;
    ss << col << " NOT IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
            ss << ", ";
        ss << "?";
    }
    ss << ")";
    where_clauses_.push_back(ss.str());
    where_conjunctions_.push_back("AND");
    for (auto v : vals) {
        where_params_.push_back({Param::INT64_VAL, v, 0.0, ""});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotIn(const std::string& col, const std::vector<std::string>& vals)
{
    if (vals.empty())
        return shared_from_this();
    std::stringstream ss;
    ss << col << " NOT IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i)
            ss << ", ";
        ss << "?";
    }
    ss << ")";
    where_clauses_.push_back(ss.str());
    where_conjunctions_.push_back("AND");
    for (auto& v : vals) {
        where_params_.push_back({Param::STRING_VAL, 0, 0.0, v});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotInIf(bool cond, const std::string& col, const std::vector<int64_t>& vals)
{
    if (cond)
        return whereNotIn(col, vals);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotInIf(bool cond, const std::string& col, const std::vector<std::string>& vals)
{
    if (cond)
        return whereNotIn(col, vals);
    return shared_from_this();
}

// ---- WHERE BETWEEN ----

QueryBuilder::ptr QueryBuilder::whereBetween(const std::string& col, int64_t lo, int64_t hi)
{
    where_clauses_.push_back(col + " BETWEEN ? AND ?");
    where_conjunctions_.push_back("AND");
    where_params_.push_back({Param::INT64_VAL, lo, 0.0, ""});
    where_params_.push_back({Param::INT64_VAL, hi, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetween(const std::string& col, const std::string& lo, const std::string& hi)
{
    where_clauses_.push_back(col + " BETWEEN ? AND ?");
    where_conjunctions_.push_back("AND");
    where_params_.push_back({Param::STRING_VAL, 0, 0.0, lo});
    where_params_.push_back({Param::STRING_VAL, 0, 0.0, hi});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetweenIf(bool cond, const std::string& col, int64_t lo, int64_t hi)
{
    if (cond)
        return whereBetween(col, lo, hi);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetweenIf(bool cond, const std::string& col, const std::string& lo, const std::string& hi)
{
    if (cond)
        return whereBetween(col, lo, hi);
    return shared_from_this();
}

// ---- WHERE LIKE ----

QueryBuilder::ptr QueryBuilder::whereLike(const std::string& col, const std::string& val)
{
    addWhereClause("AND", col + " LIKE ?", {Param::STRING_VAL, 0, 0.0, "%" + val + "%"});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLeftLike(const std::string& col, const std::string& val)
{
    addWhereClause("AND", col + " LIKE ?", {Param::STRING_VAL, 0, 0.0, val + "%"});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLikeIf(bool cond, const std::string& col, const std::string& val)
{
    if (cond)
        return whereLike(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLeftLikeIf(bool cond, const std::string& col, const std::string& val)
{
    if (cond)
        return whereLeftLike(col, val);
    return shared_from_this();
}

// ---- OR NULL / NOT NULL ----

QueryBuilder::ptr QueryBuilder::orWhereNull(const std::string& col)
{
    where_clauses_.push_back(col + " IS NULL");
    where_conjunctions_.push_back("OR");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNotNull(const std::string& col)
{
    where_clauses_.push_back(col + " IS NOT NULL");
    where_conjunctions_.push_back("OR");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNullIf(bool cond, const std::string& col)
{
    if (cond)
        return orWhereNull(col);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNotNullIf(bool cond, const std::string& col)
{
    if (cond)
        return orWhereNotNull(col);
    return shared_from_this();
}

// ---- HAVING ----

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, int64_t val)
{
    addHavingClause(col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, const std::string& val)
{
    addHavingClause(col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, double val)
{
    addHavingClause(col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, int64_t val)
{
    if (condition)
        return having(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, const std::string& val)
{
    if (condition)
        return having(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, double val)
{
    if (condition)
        return having(col, op, val);
    return shared_from_this();
}

// ---- JOIN ----

QueryBuilder::ptr QueryBuilder::join(const std::string& type, const std::string& table, const std::string& on)
{
    joins_.push_back(type + " JOIN " + table + " ON " + on);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::join(const std::string& table, const std::string& on)
{
    return join("INNER", table, on);
}

// ---- ORDER BY / LIMIT / OFFSET / GROUP BY ----

QueryBuilder::ptr QueryBuilder::orderBy(const std::string& col, const std::string& dir)
{
    order_by_ = col + " " + dir;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::limit(int32_t n)
{
    has_limit_ = true;
    limit_ = n;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::offset(int32_t n)
{
    has_offset_ = true;
    offset_ = n;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::groupBy(const std::string& col)
{
    group_by_ = col;
    return shared_from_this();
}

// ---- UPDATE SET ----

QueryBuilder::ptr QueryBuilder::set(const std::string& col, int64_t val)
{
    set_clauses_.push_back(col + " = ?");
    set_params_.push_back({Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::set(const std::string& col, const std::string& val)
{
    set_clauses_.push_back(col + " = ?");
    set_params_.push_back({Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::set(const std::string& col, double val)
{
    set_clauses_.push_back(col + " = ?");
    set_params_.push_back({Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, int64_t val)
{
    if (condition)
        return set(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, const std::string& val)
{
    if (condition)
        return set(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, double val)
{
    if (condition)
        return set(col, val);
    return shared_from_this();
}

// ---- INSERT ----

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, int64_t val)
{
    insert_cols_.push_back(col);
    insert_params_.push_back({Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, const std::string& val)
{
    insert_cols_.push_back(col);
    insert_params_.push_back({Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, double val)
{
    insert_cols_.push_back(col);
    insert_params_.push_back({Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

// ---- SQL builders ----

std::string QueryBuilder::buildCountSQL() const
{
    std::stringstream ss;
    ss << "SELECT COUNT(*) FROM " << table_;
    for (auto& j : joins_) {
        ss << " " << j;
    }
    buildWhereClause(ss);
    return ss.str();
}

std::string QueryBuilder::buildQuerySQL(const std::string& select_cols, bool with_pagination) const
{
    std::stringstream ss;
    ss << "SELECT " << select_cols << " FROM " << table_;
    for (auto& j : joins_) {
        ss << " " << j;
    }
    buildWhereClause(ss);
    if (!group_by_.empty()) {
        ss << " GROUP BY " << group_by_;
    }
    if (!having_clauses_.empty()) {
        ss << " HAVING ";
        for (size_t i = 0; i < having_clauses_.size(); ++i) {
            if (i)
                ss << " AND ";
            ss << having_clauses_[i];
        }
    }
    if (!order_by_.empty()) {
        ss << " ORDER BY " << order_by_;
    }
    if (with_pagination) {
        if (has_limit_) {
            ss << " LIMIT ?";
        }
        if (has_offset_) {
            ss << " OFFSET ?";
        }
    }
    return ss.str();
}

std::string QueryBuilder::buildQuerySQL() const
{
    return buildQuerySQL(select_cols_, true);
}

std::string QueryBuilder::buildUpdateSQL() const
{
    std::stringstream ss;
    ss << "UPDATE " << table_ << " SET ";
    for (size_t i = 0; i < set_clauses_.size(); ++i) {
        if (i)
            ss << ", ";
        ss << set_clauses_[i];
    }
    buildWhereClause(ss);
    if (has_limit_) {
        ss << " LIMIT ?";
    }
    return ss.str();
}

std::string QueryBuilder::buildDeleteSQL() const
{
    std::stringstream ss;
    ss << "DELETE FROM " << table_;
    buildWhereClause(ss);
    return ss.str();
}

std::string QueryBuilder::buildInsertSQL() const
{
    std::stringstream ss;
    ss << "INSERT INTO " << table_ << " (";
    for (size_t i = 0; i < insert_cols_.size(); ++i) {
        if (i)
            ss << ", ";
        ss << insert_cols_[i];
    }
    ss << ") VALUES (";
    for (size_t i = 0; i < insert_params_.size(); ++i) {
        if (i)
            ss << ", ";
        ss << "?";
    }
    ss << ")";
    return ss.str();
}

// ---- Parameter binding ----

void QueryBuilder::bindParams(chen::IStmt::ptr stmt) const
{
    int idx = 1;
    bindParamArray(stmt, where_params_, idx);
    bindParamArray(stmt, having_params_, idx);
    if (has_limit_) {
        stmt->bindInt32(idx, limit_);
        ++idx;
    }
    if (has_offset_) {
        stmt->bindInt32(idx, offset_);
        ++idx;
    }
}

void QueryBuilder::bindQueryParams(chen::IStmt::ptr stmt) const
{
    int idx = 1;
    bindParamArray(stmt, where_params_, idx);
    bindParamArray(stmt, having_params_, idx);
}

void QueryBuilder::bindUpdateParams(chen::IStmt::ptr stmt) const
{
    int idx = 1;
    bindParamArray(stmt, set_params_, idx);
    bindParamArray(stmt, where_params_, idx);
    if (has_limit_) {
        stmt->bindInt32(idx, limit_);
        ++idx;
    }
}

void QueryBuilder::bindInsertParams(chen::IStmt::ptr stmt) const
{
    int idx = 1;
    bindParamArray(stmt, insert_params_, idx);
}

// ---- Executors ----

int QueryBuilder::executeUpdate(chen::IDB::ptr conn)
{
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeUpdate conn is null";
        return -1;
    }
    std::string sql = buildUpdateSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    bindUpdateParams(stmt);
    return stmt->execute();
}

int QueryBuilder::executeDelete(chen::IDB::ptr conn)
{
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeDelete conn is null";
        return -1;
    }
    std::string sql = buildDeleteSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    int idx = 1;
    bindParamArray(stmt, where_params_, idx);
    return stmt->execute();
}

int QueryBuilder::executeInsert(chen::IDB::ptr conn)
{
    int64_t unused;
    return executeInsert(conn, unused);
}

int QueryBuilder::executeInsert(chen::IDB::ptr conn, int64_t& last_insert_id)
{
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeInsert conn is null";
        return -1;
    }
    std::string sql = buildInsertSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    bindInsertParams(stmt);
    int rt = stmt->execute();
    if (rt == 0) {
        last_insert_id = stmt->getLastInsertId();
    }
    return rt;
}

int QueryBuilder::executeCount(int64_t& total, chen::IDB::ptr conn) const
{
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeCount conn is null";
        return -1;
    }
    std::string sql = buildCountSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    bindQueryParams(stmt);
    auto rt = stmt->query();
    if (!rt) {
        return stmt->getErrno();
    }
    if (rt->next()) {
        total = rt->getInt64(0);
    }
    return 0;
}

ISQLData::ptr QueryBuilder::executeQuery(chen::IDB::ptr conn) const
{
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeQuery conn is null";
        return nullptr;
    }
    std::string sql = buildQuerySQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return nullptr;
    }
    bindParams(stmt);
    return stmt->query();
}

int QueryBuilder::queryScalarInt64(int64_t& result, chen::IDB::ptr conn, const std::string& col) const
{
    auto rt = executeQuery(conn);
    if (!rt) {
        return -1;
    }
    if (!rt->next()) {
        return -2;
    }
    int idx = col.empty() ? 0 : rt->findColumn(col);
    if (idx < 0) {
        return -3;
    }
    result = rt->getInt64(idx);
    return 0;
}

int QueryBuilder::queryScalarDouble(double& result, chen::IDB::ptr conn, const std::string& col) const
{
    auto rt = executeQuery(conn);
    if (!rt) {
        return -1;
    }
    if (!rt->next()) {
        return -2;
    }
    int idx = col.empty() ? 0 : rt->findColumn(col);
    if (idx < 0) {
        return -3;
    }
    result = rt->getDouble(idx);
    return 0;
}

} // namespace chen
