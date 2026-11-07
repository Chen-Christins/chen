#include "postgresql.h"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <stdarg.h>

#include "../config/config.h"
#include "../log/log.h"
#include "../util/string_util.h"
#include "../util/time_util.h"
#include "../util/util.h"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

static ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_postgresql_dbs =
    Config::Lookup("postgresql.dbs", std::map<std::string, std::map<std::string, std::string>>(), "postgresql dbs");

/**
 * @brief 把 ? 占位符改写为 PostgreSQL 的 $1/$2/...
 *
 * 字符串、双引号标识符、行注释、块注释和 dollar quote 中的 ? 不处理。
 */
static bool rewritePlaceholders(const std::string& sql, std::string& out, int& count)
{
    out.clear();
    out.reserve(sql.size() + 16);
    count = 0;

    size_t i = 0;
    const size_t n = sql.size();
    while (i < n) {
        char c = sql[i];

        if (c == '\'') {
            out.push_back(c);
            ++i;
            while (i < n) {
                if (sql[i] == '\'') {
                    out.push_back('\'');
                    if (i + 1 < n && sql[i + 1] == '\'') {
                        out.push_back('\'');
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                out.push_back(sql[i++]);
            }
            continue;
        }

        if (c == '"') {
            out.push_back(c);
            ++i;
            while (i < n) {
                if (sql[i] == '"') {
                    out.push_back('"');
                    if (i + 1 < n && sql[i + 1] == '"') {
                        out.push_back('"');
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                out.push_back(sql[i++]);
            }
            continue;
        }

        if (c == '-' && i + 1 < n && sql[i + 1] == '-') {
            while (i < n) {
                out.push_back(sql[i]);
                if (sql[i] == '\n') {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        if (c == '/' && i + 1 < n && sql[i + 1] == '*') {
            int depth = 1;
            out.push_back(sql[i++]);
            out.push_back(sql[i++]);
            while (i < n && depth > 0) {
                if (sql[i] == '/' && i + 1 < n && sql[i + 1] == '*') {
                    ++depth;
                    out.push_back(sql[i++]);
                    out.push_back(sql[i++]);
                } else if (sql[i] == '*' && i + 1 < n && sql[i + 1] == '/') {
                    --depth;
                    out.push_back(sql[i++]);
                    out.push_back(sql[i++]);
                } else {
                    out.push_back(sql[i++]);
                }
            }
            continue;
        }

        if (c == '$') {
            size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(sql[j])) || sql[j] == '_')) {
                ++j;
            }
            if (j < n && sql[j] == '$') {
                std::string tag = sql.substr(i, j - i + 1);
                size_t end = sql.find(tag, j + 1);
                if (end == std::string::npos) {
                    out.append(sql, i, std::string::npos);
                    i = n;
                    break;
                }
                out.append(sql, i, end + tag.size() - i);
                i = end + tag.size();
                continue;
            }
            out.push_back(c);
            ++i;
            continue;
        }

        if (c == '?') {
            ++count;
            if (count > 65535) {
                return false;
            }
            out.push_back('$');
            out += std::to_string(count);
            ++i;
            continue;
        }

        out.push_back(c);
        ++i;
    }
    return true;
}

PostgreSQLData::PostgreSQLData(PGresult* res, int err, const std::string& errstr)
    : m_errno(err)
    , m_errstr(errstr)
    , m_res(res)
    , m_cur(-1)
{
}

PostgreSQLData::~PostgreSQLData()
{
    if (m_res) {
        PQclear(m_res);
        m_res = nullptr;
    }
}

bool PostgreSQLData::isValidColumn(int idx) const
{
    return m_res && idx >= 0 && idx < PQnfields(m_res);
}

bool PostgreSQLData::isValidCell(int idx) const
{
    return isValidColumn(idx) && m_cur >= 0 && m_cur < PQntuples(m_res);
}

int PostgreSQLData::getDataCount()
{
    return m_res ? PQntuples(m_res) : 0;
}

int PostgreSQLData::getColumnCount()
{
    return m_res ? PQnfields(m_res) : 0;
}

int PostgreSQLData::getColumnBytes(int idx)
{
    if (!isValidCell(idx) || PQgetisnull(m_res, m_cur, idx)) {
        return 0;
    }
    return PQgetlength(m_res, m_cur, idx);
}

int PostgreSQLData::getColumnType(int idx)
{
    if (!isValidColumn(idx)) {
        return 0;
    }
    return static_cast<int>(PQftype(m_res, idx));
}

std::string PostgreSQLData::getColumnName(int idx)
{
    if (!isValidColumn(idx)) {
        return "";
    }
    const char* name = PQfname(m_res, idx);
    return name ? name : "";
}

bool PostgreSQLData::isNull(int idx)
{
    if (!isValidCell(idx)) {
        return true;
    }
    return PQgetisnull(m_res, m_cur, idx) != 0;
}

int8_t PostgreSQLData::getInt8(int idx)
{
    return static_cast<int8_t>(getInt64(idx));
}

uint8_t PostgreSQLData::getUint8(int idx)
{
    return static_cast<uint8_t>(getUint64(idx));
}

int16_t PostgreSQLData::getInt16(int idx)
{
    return static_cast<int16_t>(getInt64(idx));
}

uint16_t PostgreSQLData::getUint16(int idx)
{
    return static_cast<uint16_t>(getUint64(idx));
}

int32_t PostgreSQLData::getInt32(int idx)
{
    return static_cast<int32_t>(getInt64(idx));
}

uint32_t PostgreSQLData::getUint32(int idx)
{
    return static_cast<uint32_t>(getUint64(idx));
}

int64_t PostgreSQLData::getInt64(int idx)
{
    return static_cast<int64_t>(std::strtoll(getString(idx).c_str(), nullptr, 10));
}

uint64_t PostgreSQLData::getUint64(int idx)
{
    return static_cast<uint64_t>(std::strtoull(getString(idx).c_str(), nullptr, 10));
}

float PostgreSQLData::getFloat(int idx)
{
    return std::strtof(getString(idx).c_str(), nullptr);
}

double PostgreSQLData::getDouble(int idx)
{
    return std::strtod(getString(idx).c_str(), nullptr);
}

std::string PostgreSQLData::getString(int idx)
{
    if (!isValidCell(idx) || PQgetisnull(m_res, m_cur, idx)) {
        return "";
    }
    return std::string(PQgetvalue(m_res, m_cur, idx), PQgetlength(m_res, m_cur, idx));
}

std::string PostgreSQLData::getBlob(int idx)
{
    if (!isValidCell(idx) || PQgetisnull(m_res, m_cur, idx)) {
        return "";
    }
    if (PQfformat(m_res, idx) == 1) {
        return std::string(PQgetvalue(m_res, m_cur, idx), PQgetlength(m_res, m_cur, idx));
    }

    size_t len = 0;
    unsigned char* raw = PQunescapeBytea(reinterpret_cast<const unsigned char*>(PQgetvalue(m_res, m_cur, idx)), &len);
    if (!raw) {
        return "";
    }
    std::string rt(reinterpret_cast<const char*>(raw), len);
    PQfreemem(raw);
    return rt;
}

time_t PostgreSQLData::getTime(int idx)
{
    return TimeUtil::Str2Time(getString(idx));
}

bool PostgreSQLData::next()
{
    if (!m_res) {
        return false;
    }
    ++m_cur;
    return m_cur < PQntuples(m_res);
}

PostgreSQL::PostgreSQL(const std::map<std::string, std::string>& args)
    : m_params(args)
{
}

PostgreSQL::~PostgreSQL()
{
    close();
}

void PostgreSQL::setError(const std::string& msg, int err)
{
    m_errno = err;
    m_errstr = msg;
}

void PostgreSQL::clearError()
{
    m_errno = 0;
    m_errstr.clear();
}

bool PostgreSQL::connect()
{
    // 连接仍然可用且没有记录到错误时才复用；否则关闭重连，避免把一个处于
    // aborted transaction 状态（PQstatus 仍为 CONNECTION_OK）的连接重新放回池中。
    if (m_conn && !m_hasError && PQstatus(m_conn) == CONNECTION_OK) {
        clearError();
        return true;
    }
    close();

    std::vector<std::string> keys_storage;
    std::vector<std::string> vals_storage;
    auto add = [&keys_storage, &vals_storage](const char* key, const std::string& value) {
        if (!value.empty()) {
            keys_storage.push_back(key);
            vals_storage.push_back(value);
        }
    };

    add("host", GetParamValue<std::string>(m_params, "host"));
    add("port", GetParamValue<std::string>(m_params, "port"));
    add("user", GetParamValue<std::string>(m_params, "user"));
    add("password", GetParamValue<std::string>(m_params, "passwd"));
    add("dbname", GetParamValue<std::string>(m_params, "dbname"));
    add("connect_timeout", GetParamValue<std::string>(m_params, "connect_timeout", "10"));
    add("sslmode", GetParamValue<std::string>(m_params, "sslmode", "prefer"));
    add("application_name", GetParamValue<std::string>(m_params, "application_name", "chen"));
    add("options", GetParamValue<std::string>(m_params, "options"));

    std::vector<const char*> keys;
    std::vector<const char*> vals;
    keys.reserve(keys_storage.size() + 1);
    vals.reserve(vals_storage.size() + 1);
    for (size_t i = 0; i < keys_storage.size(); ++i) {
        keys.push_back(keys_storage[i].c_str());
        vals.push_back(vals_storage[i].c_str());
    }
    keys.push_back(nullptr);
    vals.push_back(nullptr);

    m_conn = PQconnectdbParams(keys.data(), vals.data(), 0);
    if (!m_conn || PQstatus(m_conn) != CONNECTION_OK) {
        setError(m_conn ? PQerrorMessage(m_conn) : "PQconnectdbParams failed");
        if (m_conn) {
            PQfinish(m_conn);
            m_conn = nullptr;
        }
        m_hasError = true;
        return false;
    }

    PQsetClientEncoding(m_conn, "UTF8");
    m_poolSize = GetParamValue<int>(m_params, "pool", 5);
    m_lastUsedTime = time(0);
    m_hasError = false;
    clearError();
    return true;
}

bool PostgreSQL::ping()
{
    if (!m_conn) {
        return false;
    }
    PGresult* res = PQexec(m_conn, "SELECT 1");
    bool ok = res && PQresultStatus(res) == PGRES_TUPLES_OK;
    if (res) {
        PQclear(res);
    }
    if (ok) {
        m_hasError = false;
        clearError();
    } else {
        m_hasError = true;
    }
    return ok;
}

void PostgreSQL::close()
{
    if (m_conn) {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
    m_lastUsedTime = 0;
    m_hasError = false;
}

bool PostgreSQL::isNeedCheck()
{
    if ((time(0) - m_lastUsedTime) < 5 && !m_hasError) {
        return false;
    }
    return true;
}

int PostgreSQL::execute(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQL::execute(const char* format, va_list ap)
{
    return execute(StringUtil::Formatv(format, ap));
}

int PostgreSQL::execute(const std::string& sql)
{
    clearError();
    if (!m_conn) {
        m_hasError = true;
        setError("PostgreSQL::execute m_conn is null");
        return m_errno;
    }

    PGresult* res = PQexec(m_conn, sql.c_str());
    if (!res) {
        m_hasError = true;
        setError(PQerrorMessage(m_conn));
        return m_errno;
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK && status != PGRES_EMPTY_QUERY) {
        // 语句失败后连接可能仍为 CONNECTION_OK（例如事务已 aborted），
        // 标记后连接池会在下次复用时 ping / 重连。
        m_hasError = true;
        setError(PQresultErrorMessage(res));
    } else {
        m_hasError = false;
    }
    PQclear(res);
    return m_errno;
}

int64_t PostgreSQL::getLastInsertId()
{
    if (!m_conn) {
        m_hasError = true;
        setError("PostgreSQL::getLastInsertId m_conn is null");
        return 0;
    }

    PGresult* res = PQexec(m_conn, "SELECT lastval()");
    if (!res) {
        m_hasError = true;
        setError(PQerrorMessage(m_conn));
        return 0;
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1 || PQgetisnull(res, 0, 0)) {
        m_hasError = true;
        setError(PQresultErrorMessage(res));
        PQclear(res);
        return 0;
    }

    int64_t id = static_cast<int64_t>(std::strtoll(PQgetvalue(res, 0, 0), nullptr, 10));
    PQclear(res);
    m_hasError = false;
    clearError();
    return id;
}

ISQLData::ptr PostgreSQL::query(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    ISQLData::ptr rt = query(format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr PostgreSQL::query(const char* format, va_list ap)
{
    return query(StringUtil::Formatv(format, ap));
}

ISQLData::ptr PostgreSQL::query(const std::string& sql)
{
    clearError();
    if (!m_conn) {
        m_hasError = true;
        setError("PostgreSQL::query m_conn is null");
        return nullptr;
    }

    PGresult* res = PQexec(m_conn, sql.c_str());
    if (!res) {
        m_hasError = true;
        setError(PQerrorMessage(m_conn));
        return nullptr;
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        // 同 execute：错误可能只发生在事务层，连接本身仍是 CONNECTION_OK。
        m_hasError = true;
        setError(PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }

    m_hasError = false;
    return PostgreSQLData::ptr(new PostgreSQLData(res, 0, ""));
}

IStmt::ptr PostgreSQL::prepare(const std::string& stmt)
{
    if (!m_conn) {
        setError("PostgreSQL::prepare m_conn is null");
        return nullptr;
    }

    std::string sql;
    int paramCount = 0;
    if (!rewritePlaceholders(stmt, sql, paramCount)) {
        setError("PostgreSQL::prepare too many parameters");
        return nullptr;
    }
    return PostgreSQLStmt::Create(shared_from_this(), sql, paramCount);
}

ITransaction::ptr PostgreSQL::openTransaction(bool auto_commit)
{
    return PostgreSQLTransaction::Create(shared_from_this(), auto_commit);
}

PostgreSQLStmt::PostgreSQLStmt(PostgreSQL::ptr db, const std::string& sql, int paramCount)
    : m_db(db)
    , m_sql(sql)
{
    if (paramCount > 0) {
        m_params.resize(static_cast<size_t>(paramCount));
    }
}

PostgreSQLStmt::ptr PostgreSQLStmt::Create(PostgreSQL::ptr db, const std::string& stmt, int paramCount)
{
    return PostgreSQLStmt::ptr(new PostgreSQLStmt(db, stmt, paramCount));
}

void PostgreSQLStmt::setError(const std::string& msg, int err)
{
    m_errno = err;
    m_errstr = msg;
    ERROR(g_logger) << "PostgreSQLStmt error: " << msg;
}

void PostgreSQLStmt::clearError()
{
    m_errno = 0;
    m_errstr.clear();
}

int PostgreSQLStmt::bindParam(int idx, std::string value, bool binary)
{
    if (idx < 1 || static_cast<size_t>(idx) > m_params.size()) {
        setError("bind index out of range: " + std::to_string(idx));
        return -1;
    }
    Param& p = m_params[static_cast<size_t>(idx - 1)];
    p.bound = true;
    p.isNull = false;
    p.binary = binary;
    p.value = std::move(value);
    return 0;
}

int PostgreSQLStmt::bindNullParam(int idx)
{
    if (idx < 1 || static_cast<size_t>(idx) > m_params.size()) {
        setError("bind index out of range: " + std::to_string(idx));
        return -1;
    }
    Param& p = m_params[static_cast<size_t>(idx - 1)];
    p.bound = true;
    p.isNull = true;
    p.binary = false;
    p.value.clear();
    return 0;
}

int PostgreSQLStmt::bindInt8(int idx, const int8_t& value)
{
    return bindParam(idx, std::to_string(static_cast<int>(value)));
}

int PostgreSQLStmt::bindUint8(int idx, const uint8_t& value)
{
    return bindParam(idx, std::to_string(static_cast<unsigned int>(value)));
}

int PostgreSQLStmt::bindInt16(int idx, const int16_t& value)
{
    return bindParam(idx, std::to_string(static_cast<int>(value)));
}

int PostgreSQLStmt::bindUint16(int idx, const uint16_t& value)
{
    return bindParam(idx, std::to_string(static_cast<unsigned int>(value)));
}

int PostgreSQLStmt::bindInt32(int idx, const int32_t& value)
{
    return bindParam(idx, std::to_string(value));
}

int PostgreSQLStmt::bindUint32(int idx, const uint32_t& value)
{
    return bindParam(idx, std::to_string(value));
}

int PostgreSQLStmt::bindInt64(int idx, const int64_t& value)
{
    return bindParam(idx, std::to_string(value));
}

int PostgreSQLStmt::bindUint64(int idx, const uint64_t& value)
{
    return bindParam(idx, std::to_string(value));
}

int PostgreSQLStmt::bindFloat(int idx, const float& value)
{
    return bindParam(idx, StringUtil::Format("%.9g", static_cast<double>(value)));
}

int PostgreSQLStmt::bindDouble(int idx, const double& value)
{
    return bindParam(idx, StringUtil::Format("%.17g", value));
}

int PostgreSQLStmt::bindString(int idx, const char* value)
{
    if (!value) {
        return bindNullParam(idx);
    }
    return bindParam(idx, std::string(value));
}

int PostgreSQLStmt::bindString(int idx, const std::string& value)
{
    return bindParam(idx, value);
}

int PostgreSQLStmt::bindBlob(int idx, const void* value, int64_t size)
{
    if (size < 0) {
        setError("blob size < 0");
        return -1;
    }
    if (!value) {
        if (size == 0) {
            return bindParam(idx, std::string(), true);
        }
        setError("blob value is null");
        return -1;
    }
    return bindParam(idx, std::string(static_cast<const char*>(value), static_cast<size_t>(size)), true);
}

int PostgreSQLStmt::bindBlob(int idx, const std::string& value)
{
    return bindParam(idx, value, true);
}

int PostgreSQLStmt::bindTime(int idx, const time_t& value)
{
    return bindParam(idx, TimeUtil::Time2StrSec(value, "%Y-%m-%d %H:%M:%S"));
}

int PostgreSQLStmt::bindNull(int idx)
{
    return bindNullParam(idx);
}

PGresult* PostgreSQLStmt::exec()
{
    if (!m_db || !m_db->getRaw()) {
        setError("PostgreSQLStmt::exec connection is null");
        return nullptr;
    }
    for (auto& p : m_params) {
        if (!p.bound) {
            setError("PostgreSQLStmt parameter not bound");
            return nullptr;
        }
    }

    std::vector<const char*> values(m_params.size());
    std::vector<int> lengths(m_params.size(), 0);
    std::vector<int> formats(m_params.size(), 0);

    for (size_t i = 0; i < m_params.size(); ++i) {
        Param& p = m_params[i];
        if (p.isNull) {
            values[i] = nullptr;
            continue;
        }
        values[i] = p.binary ? p.value.data() : p.value.c_str();
        lengths[i] = static_cast<int>(p.value.size());
        formats[i] = p.binary ? 1 : 0;
    }

    PGresult* res = PQexecParams(
        m_db->getRaw(), m_sql.c_str(), static_cast<int>(m_params.size()), nullptr, values.empty() ? nullptr : values.data(),
        lengths.empty() ? nullptr : lengths.data(), formats.empty() ? nullptr : formats.data(), 0);
    if (!res) {
        setError(PQerrorMessage(m_db->getRaw()));
        return nullptr;
    }
    return res;
}

int PostgreSQLStmt::execute()
{
    clearError();
    PGresult* res = exec();
    if (!res) {
        return m_errno;
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        setError(PQresultErrorMessage(res));
    }
    PQclear(res);
    return m_errno;
}

ISQLData::ptr PostgreSQLStmt::query()
{
    clearError();
    PGresult* res = exec();
    if (!res) {
        return nullptr;
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        setError(PQresultErrorMessage(res));
        PQclear(res);
        return nullptr;
    }
    return PostgreSQLData::ptr(new PostgreSQLData(res, 0, ""));
}

int64_t PostgreSQLStmt::getLastInsertId()
{
    if (!m_db) {
        return 0;
    }
    return m_db->getLastInsertId();
}

PostgreSQLTransaction::ptr PostgreSQLTransaction::Create(PostgreSQL::ptr db, bool auto_commit)
{
    if (!db) {
        ERROR(g_logger) << "PostgreSQLTransaction::Create db is null";
        return nullptr;
    }
    PostgreSQLTransaction::ptr rt(new PostgreSQLTransaction(db, auto_commit));
    if (rt->begin()) {
        return rt;
    }
    return nullptr;
}

PostgreSQLTransaction::PostgreSQLTransaction(PostgreSQL::ptr db, bool auto_commit)
    : m_db(db)
    , m_autoCommit(auto_commit)
    , m_isFinished(false)
    , m_hasError(false)
{
}

PostgreSQLTransaction::~PostgreSQLTransaction()
{
    if (!m_db || m_isFinished) {
        return;
    }
    if (m_autoCommit && !m_hasError) {
        PostgreSQLTransaction::commit();
    } else {
        PostgreSQLTransaction::rollback();
    }
}

bool PostgreSQLTransaction::begin()
{
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::begin m_db is null";
        return false;
    }
    return execute("BEGIN") == 0;
}

bool PostgreSQLTransaction::commit()
{
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::commit m_db is null";
        return false;
    }
    if (m_isFinished) {
        return !m_hasError;
    }
    if (m_hasError) {
        // PostgreSQL 在语句出错后会把事务标记为 aborted，此时 COMMIT 只会
        // 得到 ROLLBACK；这里显式回滚，确保连接归还连接池前事务已结束。
        rollback();
        return false;
    }
    int rt = execute("COMMIT");
    if (rt == 0) {
        m_isFinished = true;
    } else {
        m_hasError = true;
    }
    return rt == 0;
}

bool PostgreSQLTransaction::rollback()
{
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::rollback m_db is null";
        return false;
    }
    if (m_isFinished) {
        return true;
    }
    int rt = execute("ROLLBACK");
    if (rt == 0) {
        m_isFinished = true;
    } else {
        m_hasError = true;
    }
    return rt == 0;
}

int PostgreSQLTransaction::execute(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLTransaction::execute(const char* format, va_list ap)
{
    if (m_isFinished) {
        ERROR(g_logger) << "transaction is finished, format=" << format;
        return -1;
    }
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::execute m_db is null";
        return -1;
    }
    int rt = m_db->execute(format, ap);
    if (rt) {
        m_hasError = true;
    }
    return rt;
}

int PostgreSQLTransaction::execute(const std::string& sql)
{
    if (m_isFinished) {
        ERROR(g_logger) << "transaction is finished, sql=" << sql;
        return -1;
    }
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::execute m_db is null";
        return -1;
    }
    int rt = m_db->execute(sql);
    if (rt) {
        m_hasError = true;
    }
    return rt;
}

int64_t PostgreSQLTransaction::getLastInsertId()
{
    if (!m_db) {
        ERROR(g_logger) << "PostgreSQLTransaction::getLastInsertId m_db is null";
        return 0;
    }
    return m_db->getLastInsertId();
}

PostgreSQLManager::PostgreSQLManager()
    : m_maxConn(10)
{
}

PostgreSQLManager::~PostgreSQLManager()
{
    for (auto& i : m_conns) {
        for (auto& n : i.second) {
            delete n;
        }
    }
}

PostgreSQL::ptr PostgreSQLManager::get(const std::string& name)
{
    std::unique_lock lock(m_mutex);
    auto it = m_conns.find(name);
    if (it != m_conns.end() && !it->second.empty()) {
        PostgreSQL* rt = it->second.front();
        it->second.pop_front();
        lock.unlock();

        if (!rt->isNeedCheck() || rt->ping() || rt->connect()) {
            rt->m_lastUsedTime = time(0);
            return PostgreSQL::ptr(rt, std::bind(&PostgreSQLManager::freePostgreSQL, this, name, std::placeholders::_1));
        }

        WARN(g_logger) << "reconnect postgresql " << name << " fail";
        delete rt;
        return nullptr;
    }

    auto config = g_postgresql_dbs->getValue();
    auto sit = config.find(name);
    std::map<std::string, std::string> args;
    if (sit != config.end()) {
        args = sit->second;
    } else {
        sit = m_dbDefines.find(name);
        if (sit != m_dbDefines.end()) {
            args = sit->second;
        } else {
            return nullptr;
        }
    }
    lock.unlock();

    PostgreSQL* rt = new PostgreSQL(args);
    if (rt->connect()) {
        rt->m_lastUsedTime = time(0);
        return PostgreSQL::ptr(rt, std::bind(&PostgreSQLManager::freePostgreSQL, this, name, std::placeholders::_1));
    }

    delete rt;
    return nullptr;
}

void PostgreSQLManager::registerPostgreSQL(const std::string& name, const std::map<std::string, std::string>& params)
{
    std::lock_guard lock(m_mutex);
    m_dbDefines[name] = params;
}

void PostgreSQLManager::checkConnection(int sec)
{
    time_t now = time(0);
    std::vector<PostgreSQL*> conns;
    std::unique_lock lock(m_mutex);
    for (auto& i : m_conns) {
        for (auto it = i.second.begin(); it != i.second.end();) {
            if (static_cast<int>(now - (*it)->m_lastUsedTime) >= sec) {
                auto tmp = *it;
                i.second.erase(it++);
                conns.push_back(tmp);
            } else {
                ++it;
            }
        }
    }
    lock.unlock();
    for (auto& i : conns) {
        delete i;
    }
}

int PostgreSQLManager::execute(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(name, format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLManager::execute(const std::string& name, const char* format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(g_logger) << "PostgreSQLManager::execute, get(" << name << ") fail, format=" << format;
        return -1;
    }
    return conn->execute(format, ap);
}

int PostgreSQLManager::execute(const std::string& name, const std::string& sql)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(g_logger) << "PostgreSQLManager::execute, get(" << name << ") fail, sql=" << sql;
        return -1;
    }
    return conn->execute(sql);
}

ISQLData::ptr PostgreSQLManager::query(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    ISQLData::ptr rt = query(name, format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr PostgreSQLManager::query(const std::string& name, const char* format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(g_logger) << "PostgreSQLManager::query, get(" << name << ") fail, format=" << format;
        return nullptr;
    }
    return conn->query(format, ap);
}

ISQLData::ptr PostgreSQLManager::query(const std::string& name, const std::string& sql)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(g_logger) << "PostgreSQLManager::query, get(" << name << ") fail, sql=" << sql;
        return nullptr;
    }
    return conn->query(sql);
}

PostgreSQLTransaction::ptr PostgreSQLManager::openTransaction(const std::string& name, bool auto_commit)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(g_logger) << "PostgreSQLManager::openTransaction, get(" << name << ") fail";
        return nullptr;
    }
    return PostgreSQLTransaction::Create(conn, auto_commit);
}

void PostgreSQLManager::freePostgreSQL(const std::string& name, PostgreSQL* db)
{
    if (!db) {
        ERROR(g_logger) << "PostgreSQLManager::freePostgreSQL db is null";
        return;
    }
    if (db->m_conn && PQstatus(db->m_conn) == CONNECTION_OK) {
        // 防御性清理：正常归还时事务对象已经结束，不应残留事务状态。
        if (PQtransactionStatus(db->m_conn) != PQTRANS_IDLE) {
            PGresult* res = PQexec(db->m_conn, "ROLLBACK");
            if (res) {
                PQclear(res);
            }
        }
        if (PQtransactionStatus(db->m_conn) == PQTRANS_IDLE) {
            std::lock_guard lock(m_mutex);
            if (m_conns[name].size() < static_cast<size_t>(db->m_poolSize)) {
                m_conns[name].push_back(db);
                return;
            }
        }
    }
    delete db;
}

ISQLData::ptr PostgreSQLUtil::Query(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    ISQLData::ptr rt = Query(name, format, ap);
    va_end(ap);
    return rt;
}

ISQLData::ptr PostgreSQLUtil::Query(const std::string& name, const char* format, va_list ap)
{
    auto conn = PostgreSQLMgr::GetInstance()->get(name);
    if (!conn) {
        return nullptr;
    }
    return conn->query(format, ap);
}

ISQLData::ptr PostgreSQLUtil::Query(const std::string& name, const std::string& sql)
{
    auto conn = PostgreSQLMgr::GetInstance()->get(name);
    if (!conn) {
        return nullptr;
    }
    return conn->query(sql);
}

ISQLData::ptr PostgreSQLUtil::TryQuery(const std::string& name, uint32_t count, const char* format, ...)
{
    for (uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, format);
        ISQLData::ptr rt = Query(name, format, ap);
        va_end(ap);
        if (rt) {
            return rt;
        }
    }
    return nullptr;
}

ISQLData::ptr PostgreSQLUtil::TryQuery(const std::string& name, uint32_t count, const std::string& sql)
{
    for (uint32_t i = 0; i < count; ++i) {
        ISQLData::ptr rt = Query(name, sql);
        if (rt) {
            return rt;
        }
    }
    return nullptr;
}

int PostgreSQLUtil::Execute(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = Execute(name, format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLUtil::Execute(const std::string& name, const char* format, va_list ap)
{
    auto conn = PostgreSQLMgr::GetInstance()->get(name);
    if (!conn) {
        return -1;
    }
    return conn->execute(format, ap);
}

int PostgreSQLUtil::Execute(const std::string& name, const std::string& sql)
{
    auto conn = PostgreSQLMgr::GetInstance()->get(name);
    if (!conn) {
        return -1;
    }
    return conn->execute(sql);
}

int PostgreSQLUtil::TryExecute(const std::string& name, uint32_t count, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = TryExecute(name, count, format, ap);
    va_end(ap);
    return rt;
}

int PostgreSQLUtil::TryExecute(const std::string& name, uint32_t count, const char* format, va_list ap)
{
    int rt = -1;
    for (uint32_t i = 0; i < count; ++i) {
        va_list cp;
        va_copy(cp, ap);
        rt = Execute(name, format, cp);
        va_end(cp);
        if (rt == 0) {
            return 0;
        }
    }
    return rt;
}

int PostgreSQLUtil::TryExecute(const std::string& name, uint32_t count, const std::string& sql)
{
    int rt = -1;
    for (uint32_t i = 0; i < count; ++i) {
        rt = Execute(name, sql);
        if (rt == 0) {
            return 0;
        }
    }
    return rt;
}

} // namespace chen
