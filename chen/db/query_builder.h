/**
 * @file query_builder.h
 * @brief 参数化 SQL 查询构建器，支持动态 WHERE、JOIN、ORDER BY、分页、聚合、INSERT/UPDATE/DELETE
 * @author Christins
 * @date 2026-06-05
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "db.h"

namespace chen {

class QueryBuilder : public std::enable_shared_from_this<QueryBuilder> {
public:
    typedef std::shared_ptr<QueryBuilder> ptr;

    /**
     * @brief 创建查询构建器
     * @param table 表名
     */
    static QueryBuilder::ptr Create(const std::string& table);

    /**
     * @brief 设置 SELECT 列表达式
     * @param cols 列表达式，默认 "*"
     */
    QueryBuilder::ptr select(const std::string& cols);

    /**
     * @brief 获取 SELECT 列表达式的列
     * @return SELECT 列表达式
     */
    const std::string& getSelectCols() const { return m_selectCols; }

    // ---- WHERE (AND) ----

    QueryBuilder::ptr where(const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr where(const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr where(const std::string& col, const std::string& op, double val);
    QueryBuilder::ptr whereIf(bool condition, const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr whereIf(bool condition, const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr whereIf(bool condition, const std::string& col, const std::string& op, double val);

    QueryBuilder::ptr whereSQL(const std::string& sql, int64_t val);
    QueryBuilder::ptr whereSQL(const std::string& sql, const std::string& val);
    QueryBuilder::ptr whereSQL(const std::string& sql, double val);
    QueryBuilder::ptr whereSQLIf(bool condition, const std::string& sql, int64_t val);
    QueryBuilder::ptr whereSQLIf(bool condition, const std::string& sql, const std::string& val);
    QueryBuilder::ptr whereSQLIf(bool condition, const std::string& sql, double val);

    QueryBuilder::ptr whereIn(const std::string& col, const std::vector<int64_t>& vals);
    QueryBuilder::ptr whereIn(const std::string& col, const std::vector<std::string>& vals);
    /**
     * @brief WHERE col IN (subquery)
     * @param col 列名
     * @param sub 子查询 QueryBuilder（SELECT/WHERE/JOIN 等正常使用，LIMIT/OFFSET 会被忽略）
     */
    QueryBuilder::ptr whereIn(const std::string& col, QueryBuilder::ptr sub);

    QueryBuilder::ptr whereNull(const std::string& col);
    QueryBuilder::ptr whereNotNull(const std::string& col);
    QueryBuilder::ptr whereNotIn(const std::string& col, const std::vector<int64_t>& vals);
    QueryBuilder::ptr whereNotIn(const std::string& col, const std::vector<std::string>& vals);
    QueryBuilder::ptr whereBetween(const std::string& col, int64_t lo, int64_t hi);
    QueryBuilder::ptr whereBetween(const std::string& col, const std::string& lo, const std::string& hi);
    QueryBuilder::ptr whereLike(const std::string& col, const std::string& val);
    QueryBuilder::ptr whereLeftLike(const std::string& col, const std::string& val);

    QueryBuilder::ptr whereNullIf(bool cond, const std::string& col);
    QueryBuilder::ptr whereNotNullIf(bool cond, const std::string& col);
    QueryBuilder::ptr whereNotInIf(bool cond, const std::string& col, const std::vector<int64_t>& vals);
    QueryBuilder::ptr whereNotInIf(bool cond, const std::string& col, const std::vector<std::string>& vals);
    QueryBuilder::ptr whereBetweenIf(bool cond, const std::string& col, int64_t lo, int64_t hi);
    QueryBuilder::ptr whereBetweenIf(bool cond, const std::string& col, const std::string& lo, const std::string& hi);
    QueryBuilder::ptr whereLikeIf(bool cond, const std::string& col, const std::string& val);
    QueryBuilder::ptr whereLeftLikeIf(bool cond, const std::string& col, const std::string& val);

    // ---- WHERE (OR) ----

    QueryBuilder::ptr orWhere(const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr orWhere(const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr orWhere(const std::string& col, const std::string& op, double val);
    QueryBuilder::ptr orWhereIf(bool condition, const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr orWhereIf(bool condition, const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr orWhereIf(bool condition, const std::string& col, const std::string& op, double val);

    QueryBuilder::ptr orWhereSQL(const std::string& sql, int64_t val);
    QueryBuilder::ptr orWhereSQL(const std::string& sql, const std::string& val);
    QueryBuilder::ptr orWhereSQL(const std::string& sql, double val);
    QueryBuilder::ptr orWhereSQLIf(bool condition, const std::string& sql, int64_t val);
    QueryBuilder::ptr orWhereSQLIf(bool condition, const std::string& sql, const std::string& val);
    QueryBuilder::ptr orWhereSQLIf(bool condition, const std::string& sql, double val);

    QueryBuilder::ptr orWhereNull(const std::string& col);
    QueryBuilder::ptr orWhereNotNull(const std::string& col);
    QueryBuilder::ptr orWhereNullIf(bool cond, const std::string& col);
    QueryBuilder::ptr orWhereNotNullIf(bool cond, const std::string& col);

    // ---- HAVING ----

    QueryBuilder::ptr having(const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr having(const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr having(const std::string& col, const std::string& op, double val);
    QueryBuilder::ptr havingIf(bool condition, const std::string& col, const std::string& op, int64_t val);
    QueryBuilder::ptr havingIf(bool condition, const std::string& col, const std::string& op, const std::string& val);
    QueryBuilder::ptr havingIf(bool condition, const std::string& col, const std::string& op, double val);

    // ---- JOIN ----

    /**
     * @brief 添加 JOIN
     * @param type JOIN 类型 ("INNER", "LEFT", "RIGHT")
     * @param table 要 JOIN 的表名
     * @param on ON 条件 (e.g. "a.id = r.article_id")
     */
    QueryBuilder::ptr join(const std::string& type, const std::string& table, const std::string& on);

    /**
     * @brief 添加 INNER JOIN（便捷方法）
     */
    QueryBuilder::ptr join(const std::string& table, const std::string& on);

    // ---- ORDER BY / LIMIT / OFFSET / GROUP BY ----

    QueryBuilder::ptr orderBy(const std::string& col, const std::string& dir);
    bool hasOrderBy() const { return !m_orderBy.empty(); }
    QueryBuilder::ptr limit(int32_t n);
    QueryBuilder::ptr offset(int32_t n);
    QueryBuilder::ptr groupBy(const std::string& col);

    // ---- UPDATE SET ----

    QueryBuilder::ptr set(const std::string& col, int64_t val);
    QueryBuilder::ptr set(const std::string& col, const std::string& val);
    QueryBuilder::ptr set(const std::string& col, double val);
    QueryBuilder::ptr setIf(bool condition, const std::string& col, int64_t val);
    QueryBuilder::ptr setIf(bool condition, const std::string& col, const std::string& val);
    QueryBuilder::ptr setIf(bool condition, const std::string& col, double val);

    // ---- INSERT ----

    QueryBuilder::ptr insert(const std::string& col, int64_t val);
    QueryBuilder::ptr insert(const std::string& col, const std::string& val);
    QueryBuilder::ptr insert(const std::string& col, double val);

    // ---- SQL builders ----

    std::string buildCountSQL() const;

    /**
     * @brief 构建 SELECT SQL
     * @param selectCols 要查询的列表达式，覆盖 select() 设置的列
     * @param withPagination 是否包含 LIMIT/OFFSET 子句，默认 true。
     *                       QueryByBuilderPages 需要传 false 以自行追加分页。
     */
    std::string buildQuerySQL(const std::string& selectCols, bool withPagination = true) const;
    std::string buildQuerySQL() const;

    std::string buildUpdateSQL() const;
    std::string buildDeleteSQL() const;
    std::string buildInsertSQL() const;

    // ---- Parameter binding ----

    /// Bind WHERE + HAVING + LIMIT/OFFSET params (for SELECT queries)
    void bindParams(chen::IStmt::ptr stmt) const;
    /// Bind WHERE + HAVING params only (no LIMIT/OFFSET, for COUNT queries)
    void bindQueryParams(chen::IStmt::ptr stmt) const;
    /// Get count of WHERE + HAVING params (for index calculation after bindQueryParams)
    int getQueryParamCount() const { return static_cast<int>(m_whereParams.size() + m_havingParams.size()); }

    /// Bind SET params then WHERE params (for UPDATE queries)
    void bindUpdateParams(chen::IStmt::ptr stmt) const;
    /// Bind INSERT params
    void bindInsertParams(chen::IStmt::ptr stmt) const;

    // ---- Executors ----

    int executeUpdate(chen::IDB::ptr conn);
    int executeDelete(chen::IDB::ptr conn);
    int executeInsert(chen::IDB::ptr conn);
    int executeInsert(chen::IDB::ptr conn, int64_t& lastInsertId);
    int executeCount(int64_t& total, chen::IDB::ptr conn) const;

    /**
     * @brief 执行 SELECT 查询并返回原始结果集
     * @param conn 数据库连接
     * @return ISQLData 结果集，调用者通过 next() 遍历，失败返回 nullptr
     */
    ISQLData::ptr executeQuery(chen::IDB::ptr conn) const;

    /**
     * @brief 执行单列查询（如 SELECT id, SELECT DISTINCT day(...)）
     * @tparam T 列值类型（int32_t / int64_t / std::string）
     * @param results 输出向量
     * @param conn 数据库连接
     * @param col 列名（空字符串 = 第 0 列）
     * @return 0 成功，非 0 失败
     */
    template<typename T>
    int queryColumn(std::vector<T>& results, chen::IDB::ptr conn, const std::string& col = "") const;

    /**
     * @brief 执行 GROUP BY / 两列聚合查询，以 std::pair 形式返回
     *        典型场景：SELECT status, COUNT(*) AS cnt ... GROUP BY status
     * @tparam K 第一列类型
     * @tparam V 第二列类型
     * @return 0 成功，非 0 失败
     */
    template<typename K, typename V>
    int queryPairs(std::vector<std::pair<K, V>>& results, chen::IDB::ptr conn) const;

    /**
     * @brief 执行标量查询（如 SELECT SUM(views), SELECT COUNT(*)）
     * @param result 输出值
     * @param conn 数据库连接
     * @param col 列名（空字符串 = 第 0 列）
     * @return 0 成功，非 0 失败
     */
    int queryScalarInt64(int64_t& result, chen::IDB::ptr conn, const std::string& col = "") const;

    /**
     * @brief 执行标量查询（返回 double，如 SELECT AVG(...)）
     */
    int queryScalarDouble(double& result, chen::IDB::ptr conn, const std::string& col = "") const;

private:
    struct Param {
        enum Type { INT32_VAL, INT64_VAL, STRING_VAL, FLOAT_VAL, DOUBLE_VAL };
        Type type;
        int64_t intVal = 0;
        double dblVal = 0.0;
        std::string strVal;
    };

    void addWhereClause(const std::string& conjunction, const std::string& clause, Param p);
    void addHavingClause(const std::string& clause, Param p);
    void bindParamArray(chen::IStmt::ptr stmt, const std::vector<Param>& params, int& idx) const;
    void buildWhereClause(std::stringstream& ss) const;

    std::string m_table;
    std::string m_selectCols = "*";
    std::vector<std::string> m_whereClauses;
    std::vector<std::string> m_whereConjunctions; // AND or OR, same size as m_whereClauses (first entry unused)
    std::vector<Param> m_whereParams;
    std::vector<std::string> m_setClauses;
    std::vector<Param> m_setParams;
    std::vector<std::string> m_insertCols;
    std::vector<Param> m_insertParams;
    std::vector<std::string> m_joins;
    std::string m_orderBy;
    bool m_hasLimit = false;
    int32_t m_limit = 0;
    bool m_hasOffset = false;
    int32_t m_offset = 0;
    std::string m_groupBy;
    std::vector<std::string> m_havingClauses;
    std::vector<Param> m_havingParams;
};

namespace detail {

template <typename T>
T queryColumnGet(ISQLData::ptr rt, int idx) {
    if constexpr (std::is_same_v<T, int32_t>) {
        return rt->getInt32(idx);
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return rt->getInt64(idx);
    } else if constexpr (std::is_same_v<T, double>) {
        return rt->getDouble(idx);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return rt->getString(idx);
    } else {
        static_assert(sizeof(T) == 0, "Unsupported column type");
    }
}

} // namespace detail

template <typename T>
int QueryBuilder::queryColumn(std::vector<T>& results, IDB::ptr conn, const std::string& col) const {
    auto rt = executeQuery(conn);
    if (!rt) {
        return -1;
    }
    int idx = col.empty() ? 0 : rt->findColumn(col);
    if (idx < 0) {
        return -2;
    }
    while (rt->next()) {
        results.push_back(detail::queryColumnGet<T>(rt, idx));
    }
    return 0;
}

template <typename K, typename V>
int QueryBuilder::queryPairs(std::vector<std::pair<K, V>>& results, IDB::ptr conn) const {
    auto rt = executeQuery(conn);
    if (!rt) {
        return -1;
    }
    while (rt->next()) {
        results.emplace_back(detail::queryColumnGet<K>(rt, 0), detail::queryColumnGet<V>(rt, 1));
    }
    return 0;
}

} // namespace chen
