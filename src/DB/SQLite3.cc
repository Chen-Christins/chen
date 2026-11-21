#include "SQLite3.hpp"

#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "../Util/Env.hpp"
#include "../Util/Util.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

static ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_sqlite3_dbs =
    Config::Lookup("sqlite3.dbs", std::map<std::string, std::map<std::string, std::string>>(), "sqlite3 dbs");

SQLite3::SQLite3(sqlite3* db)
    : db_(db)
{
}

SQLite3::~SQLite3()
{
    close();
}

SQLite3::ptr SQLite3::Create(sqlite3* db)
{
    SQLite3::ptr rt(new SQLite3(db));
    return rt;
}

SQLite3::ptr SQLite3::Create(const std::string& dbname, int flags)
{
    sqlite3* db;
    if (sqlite3_open_v2(dbname.c_str(), &db, flags, nullptr) == SQLITE_OK) {
        return SQLite3::ptr(new SQLite3(db));
    }
    return nullptr;
}

ITransaction::ptr SQLite3::openTransaction(bool auto_commit)
{
    if (!db_) {
        ERROR(logger) << "SQLite3::openTransaction m_db is null";
        return nullptr;
    }
    ITransaction::ptr trans(new SQLite3Transaction(shared_from_this(), auto_commit));
    if (trans->begin()) {
        return trans;
    }
    return nullptr;
}

int SQLite3::getErrno()
{
    if (!db_) {
        ERROR(logger) << "SQLite3::getErrno m_db is null";
        return -1;
    }
    return sqlite3_errcode(db_);
}

std::string SQLite3::getErrStr()
{
    if (!db_) {
        ERROR(logger) << "SQLite3::getErrStr m_db is null";
        return "db is null";
    }
    return sqlite3_errmsg(db_);
}

int SQLite3::close()
{
    int rc = SQLITE_OK;
    if (db_) {
        rc = sqlite3_close(db_);
        if (rc == SQLITE_OK) {
            db_ = nullptr;
        }
    }
    return rc;
}

IStmt::ptr SQLite3::prepare(const std::string& stmt)
{
    if (!db_) {
        ERROR(logger) << "SQLite3::prepare m_db is null";
        return nullptr;
    }
    return SQLite3Stmt::Create(shared_from_this(), stmt.c_str());
}

int SQLite3::execute(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int SQLite3::execute(const char* format, va_list ap)
{
    if (!db_) {
        ERROR(logger) << "SQLite3::execute m_db is null";
        return SQLITE_ERROR;
    }
    std::shared_ptr<char> sql(sqlite3_vmprintf(format, ap), sqlite3_free);
    return sqlite3_exec(db_, sql.get(), 0, 0, 0);
}

ISQLData::ptr SQLite3::query(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    std::shared_ptr<char> sql(sqlite3_vmprintf(format, ap), sqlite3_free);
    va_end(ap);
    if (!db_) {
        ERROR(logger) << "SQLite3::query m_db is null";
        return nullptr;
    }
    auto stmt = SQLite3Stmt::Create(shared_from_this(), sql.get());
    if (!stmt) {
        return nullptr;
    }
    return stmt->query();
}

ISQLData::ptr SQLite3::query(const std::string& sql)
{
    if (!db_) {
        ERROR(logger) << "SQLite3::query m_db is null";
        return nullptr;
    }
    auto stmt = SQLite3Stmt::Create(shared_from_this(), sql.c_str());
    if (!stmt) {
        return nullptr;
    }
    return stmt->query();
}

int SQLite3::execute(const std::string& sql)
{
    if (!db_) {
        ERROR(logger) << "SQLite3::execute m_db is null";
        return SQLITE_ERROR;
    }
    return sqlite3_exec(db_, sql.c_str(), 0, 0, 0);
}

int64_t SQLite3::getLastInsertId()
{
    if (!db_) {
        ERROR(logger) << "SQLite3::getLastInsertId m_db is null";
        return 0;
    }
    return sqlite3_last_insert_rowid(db_);
}

SQLite3Stmt::ptr SQLite3Stmt::Create(SQLite3::ptr db, const char* stmt)
{
    if (!db || !db->getDB()) {
        ERROR(logger) << "SQLite3Stmt::Create db is null";
        return nullptr;
    }
    SQLite3Stmt::ptr rt(new SQLite3Stmt(db));
    if (rt->prepare(stmt) != SQLITE_OK) {
        return nullptr;
    }
    return rt;
}

int64_t SQLite3Stmt::getLastInsertId()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Stmt::getLastInsertId m_db is null";
        return 0;
    }
    return db_->getLastInsertId();
}

int SQLite3Stmt::bindInt8(int idx, const int8_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindUint8(int idx, const uint8_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindInt16(int idx, const int16_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindUint16(int idx, const uint16_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindInt32(int idx, const int32_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindUint32(int idx, const uint32_t& value)
{
    return bind(idx, (int32_t)value);
}

int SQLite3Stmt::bindInt64(int idx, const int64_t& value)
{
    return bind(idx, (int64_t)value);
}

int SQLite3Stmt::bindUint64(int idx, const uint64_t& value)
{
    return bind(idx, (int64_t)value);
}

int SQLite3Stmt::bindFloat(int idx, const float& value)
{
    return bind(idx, (double)value);
}

int SQLite3Stmt::bindDouble(int idx, const double& value)
{
    return bind(idx, (double)value);
}

int SQLite3Stmt::bindString(int idx, const char* value)
{
    return bind(idx, value);
}

int SQLite3Stmt::bindString(int idx, const std::string& value)
{
    return bind(idx, value);
}

int SQLite3Stmt::bindBlob(int idx, const void* value, int64_t size)
{
    return bind(idx, value, size);
}

int SQLite3Stmt::bindBlob(int idx, const std::string& value)
{
    return bind(idx, (void*)&value[0], value.size());
}

int SQLite3Stmt::bindTime(int idx, const time_t& value)
{
    return bind(idx, TimeUtil::Time2StrSec(value));
}

int SQLite3Stmt::bindNull(int idx)
{
    return bind(idx);
}

int SQLite3Stmt::getErrno()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Stmt::getErrno m_db is null";
        return -1;
    }
    return db_->getErrno();
}

std::string SQLite3Stmt::getErrStr()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Stmt::getErrStr m_db is null";
        return "db is null";
    }
    return db_->getErrStr();
}

int SQLite3Stmt::bind(int idx, int32_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(int32) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_int(stmt_, idx, value);
}

int SQLite3Stmt::bind(int idx, uint32_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(uint32) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_int(stmt_, idx, value);
}

int SQLite3Stmt::bind(int idx, double value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(double) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_double(stmt_, idx, value);
}

int SQLite3Stmt::bind(int idx, int64_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(int64) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_int64(stmt_, idx, value);
}

int SQLite3Stmt::bind(int idx, uint64_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(uint64) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_int64(stmt_, idx, value);
}

int SQLite3Stmt::bind(int idx, const char* value, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(text) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!value) {
        ERROR(logger) << "SQLite3Stmt::bind(text) value is null, binding null";
        return sqlite3_bind_null(stmt_, idx);
    }
    return sqlite3_bind_text(stmt_, idx, value, strlen(value), type == COPY ? SQLITE_TRANSIENT : SQLITE_STATIC);
}

int SQLite3Stmt::bind(int idx, const void* value, int len, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(blob) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!value) {
        ERROR(logger) << "SQLite3Stmt::bind(blob) value is null, binding null";
        return sqlite3_bind_null(stmt_, idx);
    }
    return sqlite3_bind_blob(stmt_, idx, value, len, type == COPY ? SQLITE_TRANSIENT : SQLITE_STATIC);
}

int SQLite3Stmt::bind(int idx, const std::string& value, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(string) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_text(stmt_, idx, value.c_str(), value.size(), type == COPY ? SQLITE_TRANSIENT : SQLITE_STATIC);
}

int SQLite3Stmt::bind(int idx)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(null) m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_bind_null(stmt_, idx);
}

int SQLite3Stmt::bind(const char* name, int32_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, int32) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, int32) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value);
}

int SQLite3Stmt::bind(const char* name, uint32_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, uint32) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, uint32) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value);
}

int SQLite3Stmt::bind(const char* name, double value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, double) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, double) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value);
}

int SQLite3Stmt::bind(const char* name, int64_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, int64) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, int64) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value);
}

int SQLite3Stmt::bind(const char* name, uint64_t value)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, uint64) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, uint64) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value);
}

int SQLite3Stmt::bind(const char* name, const char* value, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, text) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, text) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    if (idx == 0) {
        ERROR(logger) << "SQLite3Stmt::bind(name, text) name '" << name << "' not found";
        return SQLITE_ERROR;
    }
    return bind(idx, value, type);
}

int SQLite3Stmt::bind(const char* name, const void* value, int len, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, blob) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, blob) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value, len, type);
}

int SQLite3Stmt::bind(const char* name, const std::string& value, Type type)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, string) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, string) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx, value, type);
}

int SQLite3Stmt::bind(const char* name)
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::bind(name, null) m_stmt is null";
        return SQLITE_ERROR;
    }
    if (!name) {
        ERROR(logger) << "SQLite3Stmt::bind(name, null) name is null";
        return SQLITE_ERROR;
    }
    auto idx = sqlite3_bind_parameter_index(stmt_, name);
    return bind(idx);
}

int SQLite3Stmt::step()
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::step m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_step(stmt_);
}

int SQLite3Stmt::reset()
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::reset m_stmt is null";
        return SQLITE_ERROR;
    }
    return sqlite3_reset(stmt_);
}

int SQLite3Stmt::prepare(const char* stmt)
{
    if (!db_ || !db_->getDB()) {
        ERROR(logger) << "SQLite3Stmt::prepare m_db is null";
        return SQLITE_ERROR;
    }
    auto rt = finish();
    if (rt != SQLITE_OK) {
        return rt;
    }
    return sqlite3_prepare_v2(db_->getDB(), stmt, strlen(stmt), &stmt_, nullptr);
}

int SQLite3Stmt::finish()
{
    auto rc = SQLITE_OK;
    if (stmt_) {
        rc = sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
    return rc;
}

SQLite3Stmt::SQLite3Stmt(SQLite3::ptr db)
    : db_(db)
    , stmt_(nullptr)
{
}

SQLite3Stmt::~SQLite3Stmt()
{
    finish();
}

ISQLData::ptr SQLite3Stmt::query()
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::query m_stmt is null";
        return nullptr;
    }
    return SQLite3Data::ptr(new SQLite3Data(shared_from_this(), 0, ""));
}

int SQLite3Stmt::execute()
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Stmt::execute m_stmt is null";
        return SQLITE_ERROR;
    }
    int rt = step();
    if (rt == SQLITE_DONE) {
        rt = SQLITE_OK;
    }
    return rt;
}

SQLite3Data::SQLite3Data(std::shared_ptr<SQLite3Stmt> stmt, int err, const char* errstr)
    : errno_(err)
    , first_(true)
    , errstr_(errstr)
    , stmt_(stmt)
{
}

int SQLite3Data::getDataCount()
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getDataCount m_stmt is null";
        return 0;
    }
    return sqlite3_data_count(stmt_->stmt_);
}

int SQLite3Data::getColumnCount()
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getColumnCount m_stmt is null";
        return 0;
    }
    return sqlite3_column_count(stmt_->stmt_);
}

int SQLite3Data::getColumnBytes(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getColumnBytes m_stmt is null";
        return 0;
    }
    return sqlite3_column_bytes(stmt_->stmt_, idx);
}

int SQLite3Data::getColumnType(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getColumnType m_stmt is null";
        return 0;
    }
    return sqlite3_column_type(stmt_->stmt_, idx);
}

std::string SQLite3Data::getColumnName(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getColumnName m_stmt is null";
        return "";
    }
    const char* name = sqlite3_column_name(stmt_->stmt_, idx);
    if (name) {
        return name;
    }
    return "";
}

bool SQLite3Data::isNull(int idx)
{
    return false;
}

int8_t SQLite3Data::getInt8(int idx)
{
    return getInt32(idx);
}

uint8_t SQLite3Data::getUint8(int idx)
{
    return getInt32(idx);
}

int16_t SQLite3Data::getInt16(int idx)
{
    return getInt32(idx);
}

uint16_t SQLite3Data::getUint16(int idx)
{
    return getInt32(idx);
}

int32_t SQLite3Data::getInt32(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getInt32 m_stmt is null";
        return 0;
    }
    return sqlite3_column_int(stmt_->stmt_, idx);
}

uint32_t SQLite3Data::getUint32(int idx)
{
    return getInt32(idx);
}

int64_t SQLite3Data::getInt64(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getInt64 m_stmt is null";
        return 0;
    }
    return sqlite3_column_int64(stmt_->stmt_, idx);
}

uint64_t SQLite3Data::getUint64(int idx)
{
    return getInt64(idx);
}

float SQLite3Data::getFloat(int idx)
{
    return getDouble(idx);
}

double SQLite3Data::getDouble(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getDouble m_stmt is null";
        return 0.0;
    }
    return sqlite3_column_double(stmt_->stmt_, idx);
}

std::string SQLite3Data::getString(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getString m_stmt is null";
        return "";
    }
    const char* v = (const char*)sqlite3_column_text(stmt_->stmt_, idx);
    if (v) {
        return std::string(v, getColumnBytes(idx));
    }
    return "";
}

std::string SQLite3Data::getBlob(int idx)
{
    if (!stmt_ || !stmt_->stmt_) {
        ERROR(logger) << "SQLite3Data::getBlob m_stmt is null";
        return "";
    }
    const char* v = (const char*)sqlite3_column_blob(stmt_->stmt_, idx);
    if (v) {
        return std::string(v, getColumnBytes(idx));
    }
    return "";
}

time_t SQLite3Data::getTime(int idx)
{
    auto str = getString(idx);
    return TimeUtil::Str2Time(str.c_str());
}

bool SQLite3Data::next()
{
    if (!stmt_) {
        ERROR(logger) << "SQLite3Data::next m_stmt is null";
        return false;
    }
    int rt = stmt_->step();
    if (first_) {
        errno_ = stmt_->getErrno();
        errstr_ = stmt_->getErrStr();
        first_ = false;
    }
    return rt == SQLITE_ROW;
}

SQLite3Transaction::SQLite3Transaction(SQLite3::ptr db, bool auto_commit, Type type)
    : db_(db)
    , type_(type)
    , status_(0)
    , auto_commit_(auto_commit)
{
}

SQLite3Transaction::~SQLite3Transaction()
{
    if (!db_) {
        return;
    }
    if (status_ == 1) {
        if (auto_commit_) {
            SQLite3Transaction::commit();
        } else {
            SQLite3Transaction::rollback();
        }

        if (status_ == 1) {
            ERROR(logger) << db_ << " auto_commit=" << auto_commit_ << " fail";
        }
    }
}

int SQLite3Transaction::execute(const char* format, ...)
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::execute m_db is null";
        return SQLITE_ERROR;
    }
    va_list ap;
    va_start(ap, format);
    int rt = db_->execute(format, ap);
    va_end(ap);
    return rt;
}

int SQLite3Transaction::execute(const std::string& sql)
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::execute m_db is null";
        return SQLITE_ERROR;
    }
    return db_->execute(sql);
}

int64_t SQLite3Transaction::getLastInsertId()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::getLastInsertId m_db is null";
        return 0;
    }
    return db_->getLastInsertId();
}

bool SQLite3Transaction::begin()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::begin m_db is null";
        return false;
    }
    if (status_ == 0) {
        const char* sql = "BEGIN";
        if (type_ == IMMEDIATE) {
            sql = "BEGIN IMMEDIATE";
        } else if (type_ == EXCLUSIVE) {
            sql = "BEGIN EXCLUSIVE";
        }
        int rt = db_->execute(sql);
        if (rt == SQLITE_OK) {
            status_ = 1;
        }
        return rt == SQLITE_OK;
    }
    return false;
}

bool SQLite3Transaction::commit()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::commit m_db is null";
        return false;
    }
    if (status_ == 1) {
        int rc = db_->execute("COMMIT");
        if (rc == SQLITE_OK) {
            status_ = 2;
        }
        return rc == SQLITE_OK;
    }
    return false;
}

bool SQLite3Transaction::rollback()
{
    if (!db_) {
        ERROR(logger) << "SQLite3Transaction::rollback m_db is null";
        return false;
    }
    if (status_ == 1) {
        int rc = db_->execute("ROLLBACK");
        if (rc == SQLITE_OK) {
            status_ = 3;
        }
        return rc == SQLITE_OK;
    }
    return false;
}

SQLite3Manager::SQLite3Manager()
    : max_conn_(10)
{
}

SQLite3Manager::~SQLite3Manager()
{
    for (auto& i : conns_) {
        for (auto& n : i.second) {
            delete n;
        }
    }
}

SQLite3::ptr SQLite3Manager::get(const std::string& name)
{
    std::unique_lock lock(mutex_);
    auto it = conns_.find(name);
    if (it != conns_.end()) {
        if (!it->second.empty()) {
            SQLite3* rt = it->second.front();
            it->second.pop_front();
            lock.unlock();
            // Health check: if DB handle is null or stale, close and reopen
            if (!rt->db_ || (time(0) - rt->last_used_time_) > 30) {
                if (!rt->db_ || sqlite3_get_autocommit(rt->db_)) {
                    // Stale connection — allocate a fresh one below
                } else {
                    // Connection has an active transaction, still usable
                    rt->last_used_time_ = time(0);
                    return SQLite3::ptr(rt, std::bind(&SQLite3Manager::freeSQLite3, this, name, std::placeholders::_1));
                }
            } else {
                rt->last_used_time_ = time(0);
                return SQLite3::ptr(rt, std::bind(&SQLite3Manager::freeSQLite3, this, name, std::placeholders::_1));
            }
            // Stale connection, fall through to create a new one (old one will be replaced in pool)
            delete rt;
        }
    }
    auto config = g_sqlite3_dbs->getValue();
    auto sit = config.find(name);
    std::map<std::string, std::string> args;
    if (sit != config.end()) {
        args = sit->second;
    } else {
        sit = db_defines_.find(name);
        if (sit != db_defines_.end()) {
            args = sit->second;
        } else {
            return nullptr;
        }
    }
    lock.unlock();
    std::string path = GetParamValue<std::string>(args, "path");
    if (path.empty()) {
        ERROR(logger) << "open db name=" << name << " path is null";
        return nullptr;
    }

    if (path.find(":") == std::string::npos) {
        path = EnvMgr::GetInstance()->getAbsoluteWorkPath(path);
    }

    sqlite3* db;
    if (sqlite3_open_v2(path.c_str(), &db, SQLite3::CREATE | SQLite3::READWRITE, nullptr)) {
        ERROR(logger) << "open db name=" << name << " path=" << path << " fail";
        return nullptr;
    }

    SQLite3* rt = new SQLite3(db);
    std::string sql = GetParamValue<std::string>(args, "sql");
    if (!sql.empty()) {
        if (rt->execute(sql)) {
            ERROR(logger) << "execute sql=" << sql << " errno=" << rt->getErrno() << " errstr=" << rt->getErrStr();
            delete rt;
            return nullptr;
        }
    }
    rt->last_used_time_ = time(0);
    return SQLite3::ptr(rt, std::bind(&SQLite3Manager::freeSQLite3, this, name, std::placeholders::_1));
}

void SQLite3Manager::registerSQLite3(const std::string& name, const std::map<std::string, std::string>& params)
{
    std::unique_lock lock(mutex_);
    db_defines_[name] = params;
}

void SQLite3Manager::checkConnection(int sec)
{
    time_t now = time(0);
    std::vector<SQLite3*> conns;
    std::unique_lock lock(mutex_);
    for (auto& i : conns_) {
        for (auto it = i.second.begin(); it != i.second.end();) {
            if ((int)(now - (*it)->last_used_time_) >= sec) {
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

int SQLite3Manager::execute(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    int rt = execute(name, format, ap);
    va_end(ap);
    return rt;
}

int SQLite3Manager::execute(const std::string& name, const char* format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(logger) << "SQLite3Manager::execute, get(" << name << ") fail, format=" << format;
        return -1;
    }
    return conn->execute(format, ap);
}

int SQLite3Manager::execute(const std::string& name, const std::string& sql)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(logger) << "SQLite3Manager::execute, get(" << name << ") fail, sql=" << sql;
        return -1;
    }
    return conn->execute(sql);
}

ISQLData::ptr SQLite3Manager::query(const std::string& name, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    auto res = query(name, format, ap);
    va_end(ap);
    return res;
}

ISQLData::ptr SQLite3Manager::query(const std::string& name, const char* format, va_list ap)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(logger) << "SQLite3Manager::query, get(" << name << ") fail, format=" << format;
        return nullptr;
    }
    return conn->query(format, ap);
}

ISQLData::ptr SQLite3Manager::query(const std::string& name, const std::string& sql)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(logger) << "SQLite3Manager::query, get(" << name << ") fail, sql=" << sql;
        return nullptr;
    }
    return conn->query(sql);
}

SQLite3Transaction::ptr SQLite3Manager::openTransaction(const std::string& name, bool auto_commit)
{
    auto conn = get(name);
    if (!conn) {
        ERROR(logger) << "SQLite3Manager::openTransaction, get(" << name << ") fail";
        return nullptr;
    }
    SQLite3Transaction::ptr trans(new SQLite3Transaction(conn, auto_commit));
    return trans;
}

void SQLite3Manager::freeSQLite3(const std::string& name, SQLite3* m)
{
    if (!m) {
        ERROR(logger) << "SQLite3Manager::freeSQLite3 m is null";
        return;
    }
    if (m->db_) {
        std::unique_lock lock(mutex_);
        if (conns_[name].size() < max_conn_) {
            conns_[name].push_back(m);
            return;
        }
    }
    delete m;
}

} // namespace chen
