/**
 * @file MySQL.hpp
 * @brief MySQL 客户端封装
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 */
#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <mysql/mysql.h>

#include "../Util/Singleton.hpp"
#include "DB.hpp"

namespace chen {

// typedef std::shared_ptr<MYSQL_RES> MySQLResPtr;
// typedef std::shared_ptr<MYSQL> MySQLPtr;
class MySQL;
class MySQLStmt;
// class IMySQLUpdate {
// public:
//     typedef std::shared_ptr<IMySQLUpdate> ptr;
//     virtual ~IMySQLUpdate() {}
//     virtual int cmd(const char* format, ...) = 0;
//     virtual int cmd(const char* format, va_list ap) = 0;
//     virtual int cmd(const std::string& sql) = 0;
//     virtual std::shared_ptr<MySQL> getMySQL() = 0;
// };

struct MySQLTime {
    MySQLTime(time_t t);
    time_t ts;
};

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts);
bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt);

class MySQLRes : public ISQLData {
public:
    typedef std::shared_ptr<MySQLRes> ptr;
    typedef std::function<bool(MYSQL_ROW row, int field_count, int row_no)> data_cb;
    MySQLRes(MYSQL_RES* res, int eno, const char* estr);

    MYSQL_RES* get() const { return data_.get(); }

    int getErrno() const override { return errno_; }
    const std::string& getErrStr() const override { return errstr_; }

    bool foreach(data_cb cb);

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

private:
    int errno_;
    std::string errstr_;
    MYSQL_ROW cur_;
    unsigned long* cur_length_;
    std::shared_ptr<MYSQL_RES> data_;
    std::vector<std::string> column_names_;
};

class MySQLStmtRes : public ISQLData {
    friend class MySQLStmt;

public:
    typedef std::shared_ptr<MySQLStmtRes> ptr;
    static MySQLStmtRes::ptr Create(std::shared_ptr<MySQLStmt> stmt);
    ~MySQLStmtRes();

    int getErrno() const override { return errno_; }
    const std::string& getErrStr() const override { return errstr_; }

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

private:
    MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno, const std::string& estr);
    struct Data {
        /** 结果集这一列在绑定缓冲里怎么存：绑定类型与读取方式都由它决定 */
        enum class Storage {
            INT,    // 整型系列，统一按 int64 绑定（宽度与符号交给服务端转换）
            FLOAT,  // 4 字节浮点
            DOUBLE, // 8 字节浮点
            TIME,   // MYSQL_TIME
            TEXT    // 文本（DECIMAL / 字符串 / 二进制），数值 getter 走文本解析
        };

        Data();
        ~Data();

        void alloc(size_t size);

        /** 按 storage 解码，越界/NULL 由调用方先处理 */
        int64_t readInt64() const;
        uint64_t readUint64() const;
        double readDouble() const;

        bool is_null;
        bool error;
        bool is_unsigned;
        enum_field_types type;
        Storage storage;
        unsigned long length;
        int32_t data_length;
        char* data;
    };

    /** 取列数据，越界返回 nullptr（getter 统一的入口） */
    const Data* dataAt(int idx) const;

private:
    int errno_;
    std::string errstr_;
    std::shared_ptr<MySQLStmt> stmt_;
    std::vector<MYSQL_BIND> binds_;
    std::vector<Data> datas_;
    std::vector<std::string> column_names_;
};

class MySQLManager;
class MySQL : public IDB, public std::enable_shared_from_this<MySQL> {
    friend class MySQLManager;

public:
    typedef std::shared_ptr<MySQL> ptr;

    MySQL(const std::map<std::string, std::string>& args);

    bool connect();
    bool ping();

    virtual int execute(const char* format, ...) override;

    int execute(const char* format, va_list ap);

    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<MYSQL> getRaw();

    uint64_t getAffectedRows();

    ISQLData::ptr query(const char* format, ...) override;

    ISQLData::ptr query(const char* format, va_list ap);

    ISQLData::ptr query(const std::string& sql) override;

    ITransaction::ptr openTransaction(bool auto_commit) override;
    chen::IStmt::ptr prepare(const std::string& sql) override;

    template <typename... Args>
    int execStmt(const char* stmt, Args&&... args);

    template <class... Args>
    ISQLData::ptr queryStmt(const char* stmt, Args&&... args);

    const char* cmd();

    bool use(const std::string& dbname);
    int getErrno() override;
    std::string getErrStr() override;
    uint64_t getInsertId();

private:
    bool isNeedCheck();

private:
    std::map<std::string, std::string> params_;
    std::shared_ptr<MYSQL> mysql_;

    std::string cmd_;
    std::string dbname_;

    uint64_t last_used_time_;
    bool has_error_;
    int32_t pool_size_;
};

class MySQLTransaction : public ITransaction {
public:
    typedef std::shared_ptr<MySQLTransaction> ptr;

    static MySQLTransaction::ptr Create(MySQL::ptr mysql, bool auto_commit);
    ~MySQLTransaction();

    bool begin() override;
    bool commit() override;
    bool rollback() override;

    virtual int execute(const char* format, ...) override;

    int execute(const char* format, va_list ap);

    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;

    bool isAutoCommit() const { return auto_commit_; }
    bool isFinished() const { return is_finished_; }
    bool isError() const { return has_error_; }

private:
    MySQLTransaction(MySQL::ptr mysql, bool auto_commit);

private:
    MySQL::ptr mysql_;
    bool auto_commit_;
    bool is_finished_;
    bool has_error_;
};

class MySQLStmt : public IStmt, public std::enable_shared_from_this<MySQLStmt> {
public:
    typedef std::shared_ptr<MySQLStmt> ptr;
    static MySQLStmt::ptr Create(MySQL::ptr db, const std::string& stmt);

    ~MySQLStmt();
    int bind(int idx, const int8_t& value);
    int bind(int idx, const uint8_t& value);
    int bind(int idx, const int16_t& value);
    int bind(int idx, const uint16_t& value);
    int bind(int idx, const int32_t& value);
    int bind(int idx, const uint32_t& value);
    int bind(int idx, const int64_t& value);
    int bind(int idx, const uint64_t& value);
    int bind(int idx, const float& value);
    int bind(int idx, const double& value);
    int bind(int idx, const std::string& value);
    int bind(int idx, const char* value);
    int bind(int idx, const void* value, int len);
    // int bind(int idx, const MYSQL_TIME& value, int type = MYSQL_TYPE_TIMESTAMP);
    // for null type
    int bind(int idx);

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
    // int bindTime(int idx, const MYSQL_TIME& value, int type = MYSQL_TYPE_TIMESTAMP);
    int bindTime(int idx, const time_t& value) override;
    int bindNull(int idx) override;

    int getErrno() override;
    std::string getErrStr() override;

    int execute() override;
    int64_t getLastInsertId() override;
    ISQLData::ptr query() override;

    MYSQL_STMT* getRaw() const { return stmt_; }

private:
    MySQLStmt(MySQL::ptr db, MYSQL_STMT* stmt);

private:
    MySQL::ptr mysql_;
    MYSQL_STMT* stmt_;
    std::vector<MYSQL_BIND> binds_;
};

class MySQLManager {
public:
    MySQLManager();
    ~MySQLManager();

    MySQL::ptr get(const std::string& name);
    void registerMySQL(const std::string& name, const std::map<std::string, std::string>& params);

    void checkConnection(int sec = 30);

    uint32_t getMaxConn() const { return max_conn_; }
    void setMaxConn(uint32_t v) { max_conn_ = v; }

    int execute(const std::string& name, const char* format, ...);

    int execute(const std::string& name, const char* format, va_list ap);

    int execute(const std::string& name, const std::string& sql);

    ISQLData::ptr query(const std::string& name, const char* format, ...);

    ISQLData::ptr query(const std::string& name, const char* format, va_list ap);

    ISQLData::ptr query(const std::string& name, const std::string& sql);

    MySQLTransaction::ptr openTransaction(const std::string& name, bool auto_commit);

private:
    void freeMySQL(const std::string& name, MySQL* m);

private:
    uint32_t max_conn_;
    std::mutex mutex_;
    std::map<std::string, std::list<MySQL*>> conns_;
    std::map<std::string, std::map<std::string, std::string>> db_defines_;
};

class MySQLUtil {
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

typedef chen::Singleton<MySQLManager> MySQLMgr;

namespace {

template <size_t N, typename... Args>
struct MySQLBinder {
    static int Bind(std::shared_ptr<MySQLStmt> stmt) { return 0; }
};

template <typename... Args>
int bindX(MySQLStmt::ptr stmt, Args&... args)
{
    return MySQLBinder<1, Args...>::Bind(stmt, args...);
}
} // namespace

template <typename... Args>
int MySQL::execStmt(const char* stmt, Args&&... args)
{
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if (!st) {
        return -1;
    }
    int rt = bindX(st, args...);
    if (rt != 0) {
        return rt;
    }
    return st->execute();
}

template <class... Args>
ISQLData::ptr MySQL::queryStmt(const char* stmt, Args&&... args)
{
    auto st = MySQLStmt::Create(shared_from_this(), stmt);
    if (!st) {
        return nullptr;
    }
    int rt = bindX(st, args...);
    if (rt != 0) {
        return nullptr;
    }
    return st->query();
}

namespace {

template <size_t N, typename Head, typename... Tail>
struct MySQLBinder<N, Head, Tail...> {
    static int Bind(MySQLStmt::ptr stmt, const Head&, Tail&...)
    {
        // static_assert(false, "invalid type");
        static_assert(sizeof...(Tail) < 0, "invalid type");
        return 0;
    }
};

#define XX(type, type2)                                                  \
    template <size_t N, typename... Tail>                                \
    struct MySQLBinder<N, type, Tail...> {                               \
        static int Bind(MySQLStmt::ptr stmt, type2 value, Tail&... tail) \
        {                                                                \
            int rt = stmt->bind(N, value);                               \
            if (rt != 0) {                                               \
                return rt;                                               \
            }                                                            \
            return MySQLBinder<N + 1, Tail...>::Bind(stmt, tail...);     \
        }                                                                \
    };

// template<size_t N, typename... Tail>
// struct MySQLBinder<N, const char(&)[], Tail...> {
//     static int Bind(MySQLStmt::ptr stmt
//                     , const char value[]
//                     , const Tail&... tail) {
//         int rt = stmt->bind(N, (const char*)value);
//         if(rt != 0) {
//             return rt;
//         }
//         return MySQLBinder<N + 1, Tail...>::Bind(stmt, tail...);
//     }
// };

XX(char*, char*);
XX(const char*, char*);
XX(std::string, std::string&);
XX(int8_t, int8_t&);
XX(uint8_t, uint8_t&);
XX(int16_t, int16_t&);
XX(uint16_t, uint16_t&);
XX(int32_t, int32_t&);
XX(uint32_t, uint32_t&);
XX(int64_t, int64_t&);
XX(uint64_t, uint64_t&);
XX(float, float&);
XX(double, double&);
// XX(MYSQL_TIME, MYSQL_TIME&);
#undef XX
} // namespace
} // namespace chen
