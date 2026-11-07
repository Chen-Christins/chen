/**
 * @file postgresql.h
 * @brief PostgreSQL 客户端封装（基于 libpq）
 * @author Christins
 * @date 2026-11-07
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <libpq-fe.h>

#include "../util/singleton.h"
#include "db.h"

namespace chen {

class PostgreSQL;
class PostgreSQLStmt;

/**
 * @brief PostgreSQL 结果集
 */
class PostgreSQLData : public ISQLData {
public:
    typedef std::shared_ptr<PostgreSQLData> ptr;

    PostgreSQLData(PGresult* res, int err, const std::string& errstr);
    ~PostgreSQLData();

    int getErrno() const override { return m_errno; }
    const std::string& getErrStr() const override { return m_errstr; }

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;
    bool next() override;

    PGresult* getRaw() const { return m_res; }

private:
    bool isValidColumn(int idx) const;
    bool isValidCell(int idx) const;

private:
    int m_errno;
    std::string m_errstr;
    PGresult* m_res;
    int m_cur;
};

/**
 * @brief PostgreSQL 连接
 */
class PostgreSQL : public IDB, public std::enable_shared_from_this<PostgreSQL> {
    friend class PostgreSQLManager;
    friend class PostgreSQLStmt;
    friend class PostgreSQLTransaction;

public:
    typedef std::shared_ptr<PostgreSQL> ptr;

    explicit PostgreSQL(const std::map<std::string, std::string>& args);
    ~PostgreSQL();

    bool connect();
    bool ping();
    void close();
    bool isNeedCheck();

    int execute(const char* format, ...) override;

    int execute(const char* format, va_list ap);

    int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;

    ISQLData::ptr query(const char* format, ...) override;

    ISQLData::ptr query(const char* format, va_list ap);

    ISQLData::ptr query(const std::string& sql) override;

    IStmt::ptr prepare(const std::string& stmt) override;
    ITransaction::ptr openTransaction(bool auto_commit = false) override;

    int getErrno() override { return m_errno; }
    std::string getErrStr() override { return m_errstr; }

    PGconn* getRaw() const { return m_conn; }

private:
    void setError(const std::string& msg, int err = 1);
    void clearError();

private:
    std::map<std::string, std::string> m_params;
    PGconn* m_conn = nullptr;
    int m_errno = 0;
    std::string m_errstr;
    uint64_t m_lastUsedTime = 0;
    bool m_hasError = false;
    int m_poolSize = 5;
};

/**
 * @brief PostgreSQL 逻辑预编译语句
 *
 * SQL 在构造时把 ? 占位符改写为 $1/$2/...，执行时通过 PQexecParams
 * 发送参数。不维护服务端 prepared statement 名称，避免连接生命周期
 * 和 DEALLOCATE 问题。
 */
class PostgreSQLStmt : public IStmt {
public:
    typedef std::shared_ptr<PostgreSQLStmt> ptr;

    static PostgreSQLStmt::ptr Create(PostgreSQL::ptr db, const std::string& stmt, int paramCount);

    int bindInt8(int idx, const int8_t& value) override;
    int bindUint8(int idx, const uint8_t& value) override;
    int bindInt16(int idx, const int16_t& value) override;
    int bindUint16(int idx, const uint16_t& value) override;
    int bindInt32(int idx, const int32_t& value) override;
    int bindUint32(int idx, const uint32_t& value) override;
    int bindInt64(int idx, const int64_t& value) override;
    int bindUint64(int idx, const uint64_t& value) override;
    int bindFloat(int idx, const float& value) override;
    int bindDouble(int idx, const double& value) override;
    int bindString(int idx, const char* value) override;
    int bindString(int idx, const std::string& value) override;
    int bindBlob(int idx, const void* value, int64_t size) override;
    int bindBlob(int idx, const std::string& value) override;
    int bindTime(int idx, const time_t& value) override;
    int bindNull(int idx) override;

    int execute() override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query() override;

    int getErrno() override { return m_errno; }
    std::string getErrStr() override { return m_errstr; }

private:
    struct Param {
        bool bound = false;
        bool isNull = true;
        bool binary = false;
        std::string value;
    };

    PostgreSQLStmt(PostgreSQL::ptr db, const std::string& sql, int paramCount);

    int bindParam(int idx, std::string value, bool binary = false);
    int bindNullParam(int idx);
    PGresult* exec();
    void setError(const std::string& msg, int err = 1);
    void clearError();

private:
    PostgreSQL::ptr m_db;
    std::string m_sql;
    std::vector<Param> m_params;
    int m_errno = 0;
    std::string m_errstr;
};

/**
 * @brief PostgreSQL 事务
 */
class PostgreSQLTransaction : public ITransaction {
public:
    typedef std::shared_ptr<PostgreSQLTransaction> ptr;

    static PostgreSQLTransaction::ptr Create(PostgreSQL::ptr db, bool auto_commit);
    ~PostgreSQLTransaction();

    bool begin() override;
    bool commit() override;
    bool rollback() override;

    int execute(const char* format, ...) override;

    int execute(const char* format, va_list ap);

    int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;

    bool isAutoCommit() const { return m_autoCommit; }
    bool isFinished() const { return m_isFinished; }
    bool isError() const { return m_hasError; }

private:
    PostgreSQLTransaction(PostgreSQL::ptr db, bool auto_commit);

private:
    PostgreSQL::ptr m_db;
    bool m_autoCommit;
    bool m_isFinished;
    bool m_hasError;
};

/**
 * @brief PostgreSQL 连接池管理器
 */
class PostgreSQLManager {
public:
    PostgreSQLManager();
    ~PostgreSQLManager();

    PostgreSQL::ptr get(const std::string& name);
    void registerPostgreSQL(const std::string& name, const std::map<std::string, std::string>& params);

    void checkConnection(int sec = 30);

    uint32_t getMaxConn() const { return m_maxConn; }
    void setMaxConn(uint32_t v) { m_maxConn = v; }

    int execute(const std::string& name, const char* format, ...);

    int execute(const std::string& name, const char* format, va_list ap);

    int execute(const std::string& name, const std::string& sql);

    ISQLData::ptr query(const std::string& name, const char* format, ...);

    ISQLData::ptr query(const std::string& name, const char* format, va_list ap);

    ISQLData::ptr query(const std::string& name, const std::string& sql);

    PostgreSQLTransaction::ptr openTransaction(const std::string& name, bool auto_commit);

private:
    void freePostgreSQL(const std::string& name, PostgreSQL* db);

private:
    uint32_t m_maxConn;
    std::mutex m_mutex;
    std::map<std::string, std::list<PostgreSQL*>> m_conns;
    std::map<std::string, std::map<std::string, std::string>> m_dbDefines;
};

/**
 * @brief PostgreSQL 静态工具
 */
class PostgreSQLUtil {
public:
    static ISQLData::ptr Query(const std::string& name, const char* format, ...);

    static ISQLData::ptr Query(const std::string& name, const char* format, va_list ap);

    static ISQLData::ptr Query(const std::string& name, const std::string& sql);

    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const char* format, ...);

    static ISQLData::ptr TryQuery(const std::string& name, uint32_t count, const std::string& sql);

    static int Execute(const std::string& name, const char* format, ...);

    static int Execute(const std::string& name, const char* format, va_list ap);

    static int Execute(const std::string& name, const std::string& sql);

    static int TryExecute(const std::string& name, uint32_t count, const char* format, ...);

    static int TryExecute(const std::string& name, uint32_t count, const char* format, va_list ap);

    static int TryExecute(const std::string& name, uint32_t count, const std::string& sql);
};

typedef chen::Singleton<PostgreSQLManager> PostgreSQLMgr;

} // namespace chen
