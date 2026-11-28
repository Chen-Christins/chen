/**
 * @file TestQueryBuilder.cpp
 * @brief QueryBuilder 的 SELECT 列构造：覆盖语义、追加聚合列、CAST 收窄
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 */
#include "../TestHarness.hpp"

#include "src/DB/QueryBuilder.hpp"

CHEN_TEST(querybuilder, select_replaces_columns)
{
    auto qb = chen::QueryBuilder::Create("article");
    qb->select("id, title");
    CHEN_CHECK_EQ(qb->getSelectCols(), std::string("id, title"));
    CHEN_CHECK_EQ(qb->buildQuerySQL(), std::string("SELECT id, title FROM article"));
}

CHEN_TEST(querybuilder, select_int64_appends_cast)
{
    auto qb = chen::QueryBuilder::Create("article");
    qb->select("id, COUNT(*) AS cnt");
    qb->selectInt64("COALESCE(SUM(views), 0)", "total");
    CHEN_CHECK_EQ(
        qb->getSelectCols(), std::string("id, COUNT(*) AS cnt, CAST(COALESCE(SUM(views), 0) AS SIGNED) AS total"));
    CHEN_CHECK(qb->buildQuerySQL().find("FROM article") != std::string::npos);
}

CHEN_TEST(querybuilder, select_int64_replaces_star)
{
    auto qb = chen::QueryBuilder::Create("article");
    qb->selectInt64("COALESCE(SUM(views), 0)");
    CHEN_CHECK_EQ(qb->getSelectCols(), std::string("CAST(COALESCE(SUM(views), 0) AS SIGNED)"));
    CHEN_CHECK_EQ(qb->buildQuerySQL(), std::string("SELECT CAST(COALESCE(SUM(views), 0) AS SIGNED) FROM article"));
}

CHEN_TEST(querybuilder, select_double_appends_cast)
{
    auto qb = chen::QueryBuilder::Create("article");
    qb->selectInt64("COUNT(*)");
    qb->selectDouble("AVG(TIMESTAMPDIFF(SECOND, create_time, update_time))", "avg_seconds");
    CHEN_CHECK_EQ(qb->getSelectCols(),
        std::string("CAST(COUNT(*) AS SIGNED), "
                    "CAST(AVG(TIMESTAMPDIFF(SECOND, create_time, update_time)) AS DOUBLE) AS avg_seconds"));
}

CHEN_TEST_MAIN()
