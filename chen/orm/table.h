/**
 * @file table.h
 * @brief Table class definition
 * @details This file contains the definition of the Table class, which represents a database table.
 * @author Christins
 * @date 2025-05-09
 * @copyright GPL-3.0
 */
#pragma once

#include <string>

#include "column.h"
#include "index.h"

namespace chen {
namespace orm {

class Table {
public:
    typedef std::shared_ptr<Table> ptr;
    const std::string& getName() const { return m_name; }
    const std::string& getNamespace() const { return m_namespace; }
    const std::string& getDesc() const { return m_desc; }

    const std::vector<Column::ptr>& getCols() const { return m_cols; }
    const std::vector<Index::ptr>& getIdxs() const { return m_idxs; }
    bool init(const tinyxml2::XMLElement& node);

    void gen(const std::string& path);

    std::string getFileName() const;
private:
    enum DBType {
        DB_SQLITE3 = 1,
        DB_MYSQL,
    };
    void gen_inc(const std::string& path);
    void gen_src(const std::string& path);
    std::string genToStringInc();
    std::string genToStringSrc(const std::string& class_name);
    std::string getToInsertSQL(const std::string& class_name);
    std::string getToUpdateSQL(const std::string& class_name);
    std::string getToDeleteSQL(const std::string& class_name);

    std::vector<Column::ptr> getPKs() const;
    Column::ptr getCol(const std::string& name) const;

    std::string genWhere() const;

    void gen_dao_inc(std::ofstream& ofs);
    void gen_dao_src(std::ofstream& ofs);
    void gen_dao_migrate_src(std::ofstream& ofs);
private:
    std::string m_name;
    std::string m_namespace;
    std::string m_desc;
    std::string m_subfix = "_info";
    int m_version = 1;
    DBType m_dbType = DB_SQLITE3;
    std::string m_dbclass = "chen::IDB";
    std::string m_queryclass = "chen::IDB";
    std::string m_updateclass = "chen::IDB";
    std::vector<Column::ptr> m_cols;
    std::vector<Index::ptr> m_idxs;
};

}
}
