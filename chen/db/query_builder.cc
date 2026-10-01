#include "query_builder.h"

#include <sstream>

#include "../log/log.h"

namespace chen {

static chen::Logger::ptr g_logger = LOG_NAME("system");

QueryBuilder::ptr QueryBuilder::Create(const std::string& table) {
    QueryBuilder::ptr qb(new QueryBuilder);
    qb->m_table = table;
    return qb;
}

QueryBuilder::ptr QueryBuilder::select(const std::string& cols) {
    m_selectCols = cols;
    return shared_from_this();
}

// ---- Internal helpers ----

void QueryBuilder::addWhereClause(const std::string& conjunction, const std::string& clause, Param p) {
    m_whereClauses.push_back(clause);
    m_whereConjunctions.push_back(conjunction);
    m_whereParams.push_back(std::move(p));
}

void QueryBuilder::addHavingClause(const std::string& clause, Param p) {
    m_havingClauses.push_back(clause);
    m_havingParams.push_back(std::move(p));
}

void QueryBuilder::bindParamArray(chen::IStmt::ptr stmt, const std::vector<Param>& params, int& idx) const {
    if (!stmt) {
        ERROR(g_logger) << "QueryBuilder::bindParamArray stmt is null";
        return;
    }
    for (auto& p : params) {
        switch (p.type) {
        case Param::INT32_VAL:
            stmt->bindInt32(idx, static_cast<int32_t>(p.intVal));
            break;
        case Param::INT64_VAL:
            stmt->bindInt64(idx, p.intVal);
            break;
        case Param::STRING_VAL:
            stmt->bindString(idx, p.strVal);
            break;
        case Param::FLOAT_VAL:
            stmt->bindFloat(idx, static_cast<float>(p.dblVal));
            break;
        case Param::DOUBLE_VAL:
            stmt->bindDouble(idx, p.dblVal);
            break;
        }
        ++idx;
    }
}

void QueryBuilder::buildWhereClause(std::stringstream& ss) const {
    if (!m_whereClauses.empty()) {
        ss << " WHERE ";
        for (size_t i = 0; i < m_whereClauses.size(); ++i) {
            if (i > 0) {
                ss << " " << m_whereConjunctions[i] << " ";
            }
            ss << m_whereClauses[i];
        }
    }
}

// ---- WHERE (AND) ----

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, int64_t val) {
    addWhereClause("AND", col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, const std::string& val) {
    addWhereClause("AND", col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::where(const std::string& col, const std::string& op, double val) {
    addWhereClause("AND", col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, int64_t val) {
    if (condition) return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, const std::string& val) {
    if (condition) return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIf(bool condition, const std::string& col, const std::string& op, double val) {
    if (condition) return where(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, int64_t val) {
    addWhereClause("AND", sql, {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, const std::string& val) {
    addWhereClause("AND", sql, {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQL(const std::string& sql, double val) {
    addWhereClause("AND", sql, {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, int64_t val) {
    if (condition) return whereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, const std::string& val) {
    if (condition) return whereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereSQLIf(bool condition, const std::string& sql, double val) {
    if (condition) return whereSQL(sql, val);
    return shared_from_this();
}

// ---- WHERE (OR) ----

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, int64_t val) {
    addWhereClause("OR", col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, const std::string& val) {
    addWhereClause("OR", col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhere(const std::string& col, const std::string& op, double val) {
    addWhereClause("OR", col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, int64_t val) {
    if (condition) return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, const std::string& val) {
    if (condition) return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereIf(bool condition, const std::string& col, const std::string& op, double val) {
    if (condition) return orWhere(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, int64_t val) {
    addWhereClause("OR", sql, {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, const std::string& val) {
    addWhereClause("OR", sql, {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQL(const std::string& sql, double val) {
    addWhereClause("OR", sql, {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, int64_t val) {
    if (condition) return orWhereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, const std::string& val) {
    if (condition) return orWhereSQL(sql, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereSQLIf(bool condition, const std::string& sql, double val) {
    if (condition) return orWhereSQL(sql, val);
    return shared_from_this();
}

// ---- WHERE IN ----

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, const std::vector<int64_t>& vals) {
    if (vals.empty()) {
        return shared_from_this();
    }
    std::stringstream ss;
    ss << col << " IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) ss << ", ";
        ss << "?";
    }
    ss << ")";
    m_whereClauses.push_back(ss.str());
    m_whereConjunctions.push_back("AND");
    for (auto v : vals) {
        m_whereParams.push_back({Param::INT64_VAL, v, 0.0, ""});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, const std::vector<std::string>& vals) {
    if (vals.empty()) {
        return shared_from_this();
    }
    std::stringstream ss;
    ss << col << " IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) ss << ", ";
        ss << "?";
    }
    ss << ")";
    m_whereClauses.push_back(ss.str());
    m_whereConjunctions.push_back("AND");
    for (auto& v : vals) {
        m_whereParams.push_back({Param::STRING_VAL, 0, 0.0, v});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereIn(const std::string& col, QueryBuilder::ptr sub) {
    if (!sub) return shared_from_this();
    // Build subquery SQL without pagination (subquery in IN clause shouldn't have LIMIT/OFFSET)
    std::string subSql = sub->buildQuerySQL(sub->m_selectCols, false);
    m_whereClauses.push_back(col + " IN (" + subSql + ")");
    m_whereConjunctions.push_back("AND");
    // Merge subquery's WHERE + HAVING params into parent
    for (auto& p : sub->m_whereParams) {
        m_whereParams.push_back(p);
    }
    for (auto& p : sub->m_havingParams) {
        m_whereParams.push_back(p);
    }
    return shared_from_this();
}

// ---- NULL / NOT NULL ----

QueryBuilder::ptr QueryBuilder::whereNull(const std::string& col) {
    m_whereClauses.push_back(col + " IS NULL");
    m_whereConjunctions.push_back("AND");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotNull(const std::string& col) {
    m_whereClauses.push_back(col + " IS NOT NULL");
    m_whereConjunctions.push_back("AND");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNullIf(bool cond, const std::string& col) {
    if (cond) return whereNull(col);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotNullIf(bool cond, const std::string& col) {
    if (cond) return whereNotNull(col);
    return shared_from_this();
}

// ---- WHERE NOT IN ----

QueryBuilder::ptr QueryBuilder::whereNotIn(const std::string& col, const std::vector<int64_t>& vals) {
    if (vals.empty()) return shared_from_this();
    std::stringstream ss;
    ss << col << " NOT IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) ss << ", ";
        ss << "?";
    }
    ss << ")";
    m_whereClauses.push_back(ss.str());
    m_whereConjunctions.push_back("AND");
    for (auto v : vals) {
        m_whereParams.push_back({Param::INT64_VAL, v, 0.0, ""});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotIn(const std::string& col, const std::vector<std::string>& vals) {
    if (vals.empty()) return shared_from_this();
    std::stringstream ss;
    ss << col << " NOT IN (";
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) ss << ", ";
        ss << "?";
    }
    ss << ")";
    m_whereClauses.push_back(ss.str());
    m_whereConjunctions.push_back("AND");
    for (auto& v : vals) {
        m_whereParams.push_back({Param::STRING_VAL, 0, 0.0, v});
    }
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotInIf(bool cond, const std::string& col, const std::vector<int64_t>& vals) {
    if (cond) return whereNotIn(col, vals);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereNotInIf(bool cond, const std::string& col, const std::vector<std::string>& vals) {
    if (cond) return whereNotIn(col, vals);
    return shared_from_this();
}

// ---- WHERE BETWEEN ----

QueryBuilder::ptr QueryBuilder::whereBetween(const std::string& col, int64_t lo, int64_t hi) {
    m_whereClauses.push_back(col + " BETWEEN ? AND ?");
    m_whereConjunctions.push_back("AND");
    m_whereParams.push_back({Param::INT64_VAL, lo, 0.0, ""});
    m_whereParams.push_back({Param::INT64_VAL, hi, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetween(const std::string& col, const std::string& lo, const std::string& hi) {
    m_whereClauses.push_back(col + " BETWEEN ? AND ?");
    m_whereConjunctions.push_back("AND");
    m_whereParams.push_back({Param::STRING_VAL, 0, 0.0, lo});
    m_whereParams.push_back({Param::STRING_VAL, 0, 0.0, hi});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetweenIf(bool cond, const std::string& col, int64_t lo, int64_t hi) {
    if (cond) return whereBetween(col, lo, hi);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereBetweenIf(bool cond, const std::string& col, const std::string& lo, const std::string& hi) {
    if (cond) return whereBetween(col, lo, hi);
    return shared_from_this();
}

// ---- WHERE LIKE ----

QueryBuilder::ptr QueryBuilder::whereLike(const std::string& col, const std::string& val) {
    addWhereClause("AND", col + " LIKE ?", {Param::STRING_VAL, 0, 0.0, "%" + val + "%"});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLeftLike(const std::string& col, const std::string& val) {
    addWhereClause("AND", col + " LIKE ?", {Param::STRING_VAL, 0, 0.0, val + "%"});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLikeIf(bool cond, const std::string& col, const std::string& val) {
    if (cond) return whereLike(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::whereLeftLikeIf(bool cond, const std::string& col, const std::string& val) {
    if (cond) return whereLeftLike(col, val);
    return shared_from_this();
}

// ---- OR NULL / NOT NULL ----

QueryBuilder::ptr QueryBuilder::orWhereNull(const std::string& col) {
    m_whereClauses.push_back(col + " IS NULL");
    m_whereConjunctions.push_back("OR");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNotNull(const std::string& col) {
    m_whereClauses.push_back(col + " IS NOT NULL");
    m_whereConjunctions.push_back("OR");
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNullIf(bool cond, const std::string& col) {
    if (cond) return orWhereNull(col);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::orWhereNotNullIf(bool cond, const std::string& col) {
    if (cond) return orWhereNotNull(col);
    return shared_from_this();
}

// ---- HAVING ----

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, int64_t val) {
    addHavingClause(col + " " + op + " ?", {Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, const std::string& val) {
    addHavingClause(col + " " + op + " ?", {Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::having(const std::string& col, const std::string& op, double val) {
    addHavingClause(col + " " + op + " ?", {Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, int64_t val) {
    if (condition) return having(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, const std::string& val) {
    if (condition) return having(col, op, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::havingIf(bool condition, const std::string& col, const std::string& op, double val) {
    if (condition) return having(col, op, val);
    return shared_from_this();
}

// ---- JOIN ----

QueryBuilder::ptr QueryBuilder::join(const std::string& type, const std::string& table, const std::string& on) {
    m_joins.push_back(type + " JOIN " + table + " ON " + on);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::join(const std::string& table, const std::string& on) {
    return join("INNER", table, on);
}

// ---- ORDER BY / LIMIT / OFFSET / GROUP BY ----

QueryBuilder::ptr QueryBuilder::orderBy(const std::string& col, const std::string& dir) {
    m_orderBy = col + " " + dir;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::limit(int32_t n) {
    m_hasLimit = true;
    m_limit = n;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::offset(int32_t n) {
    m_hasOffset = true;
    m_offset = n;
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::groupBy(const std::string& col) {
    m_groupBy = col;
    return shared_from_this();
}

// ---- UPDATE SET ----

QueryBuilder::ptr QueryBuilder::set(const std::string& col, int64_t val) {
    m_setClauses.push_back(col + " = ?");
    m_setParams.push_back({Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::set(const std::string& col, const std::string& val) {
    m_setClauses.push_back(col + " = ?");
    m_setParams.push_back({Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::set(const std::string& col, double val) {
    m_setClauses.push_back(col + " = ?");
    m_setParams.push_back({Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, int64_t val) {
    if (condition) return set(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, const std::string& val) {
    if (condition) return set(col, val);
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::setIf(bool condition, const std::string& col, double val) {
    if (condition) return set(col, val);
    return shared_from_this();
}

// ---- INSERT ----

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, int64_t val) {
    m_insertCols.push_back(col);
    m_insertParams.push_back({Param::INT64_VAL, val, 0.0, ""});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, const std::string& val) {
    m_insertCols.push_back(col);
    m_insertParams.push_back({Param::STRING_VAL, 0, 0.0, val});
    return shared_from_this();
}

QueryBuilder::ptr QueryBuilder::insert(const std::string& col, double val) {
    m_insertCols.push_back(col);
    m_insertParams.push_back({Param::DOUBLE_VAL, 0, val, ""});
    return shared_from_this();
}

// ---- SQL builders ----

std::string QueryBuilder::buildCountSQL() const {
    std::stringstream ss;
    ss << "SELECT COUNT(*) FROM " << m_table;
    for (auto& j : m_joins) {
        ss << " " << j;
    }
    buildWhereClause(ss);
    return ss.str();
}

std::string QueryBuilder::buildQuerySQL(const std::string& selectCols, bool withPagination) const {
    std::stringstream ss;
    ss << "SELECT " << selectCols << " FROM " << m_table;
    for (auto& j : m_joins) {
        ss << " " << j;
    }
    buildWhereClause(ss);
    if (!m_groupBy.empty()) {
        ss << " GROUP BY " << m_groupBy;
    }
    if (!m_havingClauses.empty()) {
        ss << " HAVING ";
        for (size_t i = 0; i < m_havingClauses.size(); ++i) {
            if (i) ss << " AND ";
            ss << m_havingClauses[i];
        }
    }
    if (!m_orderBy.empty()) {
        ss << " ORDER BY " << m_orderBy;
    }
    if (withPagination) {
        if (m_hasLimit) {
            ss << " LIMIT ?";
        }
        if (m_hasOffset) {
            ss << " OFFSET ?";
        }
    }
    return ss.str();
}

std::string QueryBuilder::buildQuerySQL() const {
    return buildQuerySQL(m_selectCols, true);
}

std::string QueryBuilder::buildUpdateSQL() const {
    std::stringstream ss;
    ss << "UPDATE " << m_table << " SET ";
    for (size_t i = 0; i < m_setClauses.size(); ++i) {
        if (i) ss << ", ";
        ss << m_setClauses[i];
    }
    buildWhereClause(ss);
    if (m_hasLimit) {
        ss << " LIMIT ?";
    }
    return ss.str();
}

std::string QueryBuilder::buildDeleteSQL() const {
    std::stringstream ss;
    ss << "DELETE FROM " << m_table;
    buildWhereClause(ss);
    return ss.str();
}

std::string QueryBuilder::buildInsertSQL() const {
    std::stringstream ss;
    ss << "INSERT INTO " << m_table << " (";
    for (size_t i = 0; i < m_insertCols.size(); ++i) {
        if (i) ss << ", ";
        ss << m_insertCols[i];
    }
    ss << ") VALUES (";
    for (size_t i = 0; i < m_insertParams.size(); ++i) {
        if (i) ss << ", ";
        ss << "?";
    }
    ss << ")";
    return ss.str();
}

// ---- Parameter binding ----

void QueryBuilder::bindParams(chen::IStmt::ptr stmt) const {
    int idx = 1;
    bindParamArray(stmt, m_whereParams, idx);
    bindParamArray(stmt, m_havingParams, idx);
    if (m_hasLimit) {
        stmt->bindInt32(idx, m_limit);
        ++idx;
    }
    if (m_hasOffset) {
        stmt->bindInt32(idx, m_offset);
        ++idx;
    }
}

void QueryBuilder::bindQueryParams(chen::IStmt::ptr stmt) const {
    int idx = 1;
    bindParamArray(stmt, m_whereParams, idx);
    bindParamArray(stmt, m_havingParams, idx);
}

void QueryBuilder::bindUpdateParams(chen::IStmt::ptr stmt) const {
    int idx = 1;
    bindParamArray(stmt, m_setParams, idx);
    bindParamArray(stmt, m_whereParams, idx);
    if (m_hasLimit) {
        stmt->bindInt32(idx, m_limit);
        ++idx;
    }
}

void QueryBuilder::bindInsertParams(chen::IStmt::ptr stmt) const {
    int idx = 1;
    bindParamArray(stmt, m_insertParams, idx);
}

// ---- Executors ----

int QueryBuilder::executeUpdate(chen::IDB::ptr conn) {
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeUpdate conn is null";
        return -1;
    }
    std::string sql = buildUpdateSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql
            << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    bindUpdateParams(stmt);
    return stmt->execute();
}

int QueryBuilder::executeDelete(chen::IDB::ptr conn) {
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeDelete conn is null";
        return -1;
    }
    std::string sql = buildDeleteSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql
            << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    int idx = 1;
    bindParamArray(stmt, m_whereParams, idx);
    return stmt->execute();
}

int QueryBuilder::executeInsert(chen::IDB::ptr conn) {
    int64_t unused;
    return executeInsert(conn, unused);
}

int QueryBuilder::executeInsert(chen::IDB::ptr conn, int64_t& lastInsertId) {
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeInsert conn is null";
        return -1;
    }
    std::string sql = buildInsertSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql
            << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return conn->getErrno();
    }
    bindInsertParams(stmt);
    int rt = stmt->execute();
    if (rt == 0) {
        lastInsertId = stmt->getLastInsertId();
    }
    return rt;
}

int QueryBuilder::executeCount(int64_t& total, chen::IDB::ptr conn) const {
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeCount conn is null";
        return -1;
    }
    std::string sql = buildCountSQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql
            << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
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

ISQLData::ptr QueryBuilder::executeQuery(chen::IDB::ptr conn) const {
    if (!conn) {
        ERROR(g_logger) << "QueryBuilder::executeQuery conn is null";
        return nullptr;
    }
    std::string sql = buildQuerySQL();
    auto stmt = conn->prepare(sql);
    if (!stmt) {
        ERROR(g_logger) << "stmt=" << sql
            << " errno=" << conn->getErrno() << " errstr=" << conn->getErrStr();
        return nullptr;
    }
    bindParams(stmt);
    return stmt->query();
}

int QueryBuilder::queryScalarInt64(int64_t& result, chen::IDB::ptr conn, const std::string& col) const {
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

int QueryBuilder::queryScalarDouble(double& result, chen::IDB::ptr conn, const std::string& col) const {
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
