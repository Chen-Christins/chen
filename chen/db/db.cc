/**
 * @file db.cc
 * @brief 数据库封装基类实现
 */
#include "db.h"

namespace chen {

ISQLData::~ISQLData()
{
}

ISQLUpdate::~ISQLUpdate()
{
}

ISQLQuery::~ISQLQuery()
{
}

IStmt::~IStmt()
{
}

ITransaction::~ITransaction()
{
}

IDB::~IDB()
{
}

int ISQLData::findColumn(const std::string& name)
{
    for (int i = 0; i < getColumnCount(); ++i) {
        if (getColumnName(i) == name) {
            return i;
        }
    }
    return -1;
}

int32_t ISQLData::getInt32(const std::string& col)
{
    return getInt32(findColumn(col));
}

int64_t ISQLData::getInt64(const std::string& col)
{
    return getInt64(findColumn(col));
}

std::string ISQLData::getString(const std::string& col)
{
    return getString(findColumn(col));
}

time_t ISQLData::getTime(const std::string& col)
{
    return getTime(findColumn(col));
}

} // namespace chen
