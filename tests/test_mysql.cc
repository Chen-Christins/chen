#include <iostream>
#include "chen/db/mysql.h"
#include "chen/iomanager/iomanager.h"

void run() {
    std::map<std::string, std::string> params;
    params["host"] = "192.168.3.243";
    params["port"] = "3306";
    params["user"] = "chen";
    params["passwd"] = "123456";
    params["dbname"] = "test";

    chen::MySQL::ptr mysql(new chen::MySQL(params));
    if (!mysql->connect()) {
        std::cout << "connect mysql failed" << std::endl;
        return;
    }

    std::cout << "connect mysql success" << std::endl;

    if (!mysql->ping()) {
        std::cout << "mysql ping failed" << std::endl;
        return;
    }
    std::cout << "mysql ping success" << std::endl;

    auto basic = mysql->query("SELECT 1 AS v");
    if (!basic || !basic->next()) {
        std::cout << "basic query failed" << std::endl;
        return;
    }
    std::cout << "basic query value=" << basic->getInt32(0) << std::endl;

    int stmtValue = 7;
    auto stmtRes = mysql->queryStmt("SELECT ? + 1 AS v", stmtValue);
    if (!stmtRes || !stmtRes->next()) {
        std::cout << "queryStmt failed" << std::endl;
        return;
    }
    std::cout << "queryStmt value=" << stmtRes->getInt32(0) << std::endl;

    if (mysql->execute("DROP TEMPORARY TABLE IF EXISTS test_mysql_wrapper") != 0) {
        std::cout << "drop temp table failed: " << mysql->getErrStr() << std::endl;
        return;
    }

    if (mysql->execute("CREATE TEMPORARY TABLE test_mysql_wrapper (id INT PRIMARY KEY, name VARCHAR(32))") != 0) {
        std::cout << "create temp table failed: " << mysql->getErrStr() << std::endl;
        return;
    }

    if (mysql->execStmt("INSERT INTO test_mysql_wrapper(id, name) VALUES(?, ?)", 1, std::string("alice")) != 0) {
        std::cout << "execStmt insert failed: " << mysql->getErrStr() << std::endl;
        return;
    }

    auto queryOne = mysql->query("SELECT name FROM test_mysql_wrapper WHERE id = 1");
    if (!queryOne || !queryOne->next()) {
        std::cout << "query inserted row failed" << std::endl;
        return;
    }
    std::cout << "inserted row name=" << queryOne->getString(0) << std::endl;

    auto tx = mysql->openTransaction(false);
    if (!tx) {
        std::cout << "open transaction failed" << std::endl;
        return;
    }

    if (tx->execute("INSERT INTO test_mysql_wrapper(id, name) VALUES(2, 'tx_user')") != 0) {
        std::cout << "transaction insert failed" << std::endl;
        return;
    }

    if (!tx->rollback()) {
        std::cout << "transaction rollback failed" << std::endl;
        return;
    }

    auto countRes = mysql->query("SELECT COUNT(*) FROM test_mysql_wrapper WHERE id = 2");
    if (!countRes || !countRes->next()) {
        std::cout << "query rollback result failed" << std::endl;
        return;
    }
    std::cout << "rollback check count=" << countRes->getInt32(0) << std::endl;
    std::cout << "mysql wrapper interface test done" << std::endl;
}

int main(int argc, char** argv) {
    
    chen::IOManager iom(1);
    iom.schedule(run);

    return 0;
}