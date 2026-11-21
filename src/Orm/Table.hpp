/**
 * @file Table.hpp
 * @brief Table class definition
 * @details This file contains the definition of the Table class, which represents a database table.
 * @author Christins
 * @date 2025-05-09
 * @copyright GPL-3.0
 */
#pragma once

#include <string>

#include "Column.hpp"
#include "Index.hpp"

namespace chen {
namespace orm {

class Table {
public:
    typedef std::shared_ptr<Table> ptr;
    const std::string& getName() const { return name_; }
    const std::string& getNamespace() const { return namespace_; }
    const std::string& getDesc() const { return desc_; }

    const std::vector<Column::ptr>& getCols() const { return cols_; }
    const std::vector<Index::ptr>& getIdxs() const { return idxs_; }
    bool init(const tinyxml2::XMLElement& node);

    void gen(const std::string& path);

    std::string getFileName() const;

private:
    enum DBType {
        DB_SQLITE3 = 1,
        DB_MYSQL,
        DB_POSTGRESQL,
    };
    void gen_inc(const std::string& path);
    void gen_src(const std::string& path);
    std::string genToStringInc();
    std::string genToStringSrc(const std::string& class_name);
    std::string getToInsertSQL(const std::string& class_name);
    std::string getToUpdateSQL(const std::string& class_name);
    std::string getToDeleteSQL(const std::string& class_name);

    bool isPostgreSQL() const { return db_type_ == DB_POSTGRESQL; }
    /**
     * @brief 生成 SQL 标识符
     *
     * PostgreSQL 使用双引号，其他方言保持原样。返回值已经按 C++ 字符串
     * 字面量转义，可直接拼进生成代码的 "..." 中。
     */
    std::string sqlIdent(const std::string& name) const;
    std::string sqlTable() const { return sqlIdent(name_); }
    std::string getIndexName(const Index::ptr& idx) const;

    std::vector<Column::ptr> getPKs() const;
    Column::ptr getCol(const std::string& name) const;

    std::string genWhere() const;

    void gen_dao_inc(std::ofstream& ofs);
    void gen_dao_src(std::ofstream& ofs);
    void gen_dao_migrate_src(std::ofstream& ofs);
    void gen_pg_trigger_sql(std::ofstream& ofs, const std::string& indent, bool drop_first);
    void gen_pg_index_sql(std::ofstream& ofs, const std::string& indent, bool if_not_exists, bool drop_first);

private:
    std::string name_;
    std::string namespace_;
    std::string desc_;
    std::string subfix_ = "_info";
    int version_ = 1;
    DBType db_type_ = DB_SQLITE3;
    std::string dbclass_ = "chen::IDB";
    std::string queryclass_ = "chen::IDB";
    std::string updateclass_ = "chen::IDB";
    std::vector<Column::ptr> cols_;
    std::vector<Index::ptr> idxs_;
};

} // namespace orm
} // namespace chen
