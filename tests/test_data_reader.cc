/**
 * @file test_data_reader.cpp
 * @brief 测试数据读取器功能
 * @author Christins
 * @date 2024-11-06
 */
#include "../chen/data/data_reader.h"
#include <iostream>
#include <iomanip>

using namespace chen::data;

void printTableInfo(const DataReader& reader) {
    std::cout << "\n=== 数据加载信息 ===" << std::endl;
    std::cout << "表数量: " << reader.getTableCount() << std::endl;

    auto table_names = reader.getAllTableNames();
    std::cout << "表名列表: ";
    for (const auto& name : table_names) {
        std::cout << name << " ";
    }
    std::cout << std::endl;
}

void printTableContent(const TableData::ptr& table) {
    if (!table) {
        std::cout << "表不存在!" << std::endl;
        return;
    }

    std::cout << "\n=== 表: " << table->getName() << " ===" << std::endl;
    std::cout << "行数: " << table->getRowCount() << std::endl;

    // 打印表头
    const auto& headers = table->getHeaders();
    std::cout << "\n";
    for (const auto& header : headers) {
        std::cout << std::setw(12) << header << "\t";
    }
    std::cout << std::endl;

    // 打印分隔线
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << std::setw(12) << "----------" << "\t";
    }
    std::cout << std::endl;

    // 打印数据行
    table->forEachRow([&headers](size_t index, const RowData& row) {
        for (const auto& header : headers) {
            auto it = row.find(header);
            if (it != row.end()) {
                std::visit([](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>) {
                        std::cout << std::setw(12) << "NULL" << "\t";
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        std::cout << std::setw(12) << arg << "\t";
                    } else {
                        std::cout << std::setw(12) << arg << "\t";
                    }
                }, it->second);
            } else {
                std::cout << std::setw(12) << "" << "\t";
            }
        }
        std::cout << std::endl;
    });
}

void testFieldAccessor(const DataReader& reader) {
    std::cout << "\n=== 测试字段访问器 ===" << std::endl;

    auto table = reader.getTable("example_config");
    if (!table) {
        std::cout << "找不到 example_config 表" << std::endl;
        return;
    }

    // 测试第一行数据
    RowAccessor row(table.get(), 0);
    if (row.isValid()) {
        std::cout << "第一行数据:" << std::endl;
        std::cout << "id: " << static_cast<int32_t>(row["id"]) << std::endl;
        std::cout << "name: " << static_cast<std::string>(row["name"]) << std::endl;
        std::cout << "level: " << static_cast<int32_t>(row["level"]) << std::endl;
        std::cout << "attack: " << static_cast<int32_t>(row["attack"]) << std::endl;
        std::cout << "description: " << static_cast<std::string>(row["description"]) << std::endl;
    }

    // 测试查找特定数据
    std::cout << "\n查找所有武器:" << std::endl;
    for (size_t i = 0; i < table->getRowCount(); ++i) {
        RowAccessor row(table.get(), i);
        if (row.isValid()) {
            std::string type = static_cast<std::string>(row["type"]);
            if (type == "weapon") {
                std::cout << "  - " << static_cast<std::string>(row["name"])
                    << " (攻击力: " << static_cast<int32_t>(row["attack"]) << ")" << std::endl;
            }
        }
    }
}

void testValueConversion(const DataReader& reader) {
    std::cout << "\n=== 测试值转换功能 ===" << std::endl;

    auto table = reader.getTable("example_config");
    if (!table) return;

    // 测试不同类型的值获取
    const RowData* row = table->getRow(0);
    if (row) {
        // 使用模板方法获取值
        auto id = table->getValue<int32_t>(0, "id", -1);
        auto name = table->getValue<std::string>(0, "name", "unknown");
        auto level = table->getValue<int32_t>(0, "level", 0);
        auto attack = table->getValue<int32_t>(0, "attack", 0);
        auto hp = table->getValue<int32_t>(0, "hp", 0);
        auto description = table->getValue<std::string>(0, "description", "no description");

        std::cout << "使用 getValue 方法:" << std::endl;
        std::cout << "id: " << id << std::endl;
        std::cout << "name: " << name << std::endl;
        std::cout << "level: " << level << std::endl;
        std::cout << "attack: " << attack << std::endl;
        std::cout << "hp (默认值): " << hp << std::endl;
        std::cout << "description: " << description << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string bin_file = "example_config.bin";

    // 如果指定了文件路径
    if (argc > 1) {
        bin_file = argv[1];
    }

    std::cout << "=== 数据读取器测试程序 ===" << std::endl;
    std::cout << "加载文件: " << bin_file << std::endl;

    // 创建数据读取器
    DataReader reader("aes256_gcm", "chen_default_password" ); // 使用AES-256-GCM解密进行测试

    // 加载数据
    if (!reader.loadFromFile(bin_file)) {
        std::cerr << "错误: 无法加载数据文件!" << std::endl;
        std::cout << "\n请确保已经使用以下命令生成了bin文件:" << std::endl;
        std::cout << "python3 xlsx_to_bin.py example_config.xlsx -o " << bin_file << std::endl;
        return 1;
    }

    // 打印基本信息
    printTableInfo(reader);

    // 打印每个表的内容
    auto table_names = reader.getAllTableNames();
    for (const auto& name : table_names) {
        auto table = reader.getTable(name);
        printTableContent(table);
    }

    // 测试字段访问器
    testFieldAccessor(reader);

    // 测试值转换
    testValueConversion(reader);

    std::cout << "\n=== 测试完成 ===" << std::endl;

    return 0;
}