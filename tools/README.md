# 游戏策划表导出工具

这个工具集用于将 Excel 策划表（.xlsx）转换为加密的二进制格式，并提供 C++ 接口进行读取。

## 功能特性

- **多种加密方案**：
  - AES-256-GCM（推荐）：业界标准的认证加密
  - ChaCha20-Poly1305：现代流加密方案
  - 自定义加密：多轮混淆 + HMAC认证
  - XOR加密：仅用于向后兼容（不安全）
- **密钥派生**：使用PBKDF2从密码安全派生密钥
- **压缩优化**：使用 zlib 压缩减少文件大小
- **完整性保护**：所有加密方案都包含认证标签
- **类型支持**：支持整数、浮点数、字符串等多种数据类型
- **便捷访问**：提供友好的 C++ 访问接口

## 文件结构

```
tools/
├── scripts/
│   ├── xlsx_to_bin.py          # Python 转换脚本
│   ├── encrypt.py              # 加密算法模块
│   └── test_data.xlsx          # 测试数据文件
├── xlsx/                       # Excel策划表目录
│   ├── example_config.xlsx     # 示例策划表
│   └── table1.xlsx             # 示例表
├── test_data_reader.cc         # C++ 测试程序
└── README.md                   # 说明文档

chen/data/
├── data_reader.h               # C++ 数据读取器头文件
├── data_reader.cpp             # C++ 数据读取器实现
├── data_decryptor.h            # 解密模块头文件
└── data_decryptor.cpp          # 解密模块实现
```

## 使用方法

### 1. 安装依赖

```bash
# 激活虚拟环境（如果存在）
source .venv/bin/activate

# 安装Python依赖
pip install openpyxl cryptography
```

### 2. 转换 xlsx 到 bin

```bash
# 使用AES-256-GCM加密（推荐）
cd tools
python3 scripts/xlsx_to_bin.py xlsx/example_config.xlsx -o data/example_config.bin

# 使用ChaCha20-Poly1305加密
python3 scripts/xlsx_to_bin.py xlsx/example_config.xlsx -o data/example_config.bin -e chacha20_poly1305

# 使用自定义密码
python3 scripts/xlsx_to_bin.py xlsx/example_config.xlsx -o data/example_config.bin -p my_secret_password

# 合并多个文件到一个bin
python3 scripts/xlsx_to_bin.py xlsx/table1.xlsx xlsx/table2.xlsx -o data/merged.bin

# 批量转换每个xlsx为单独的bin文件
python3 scripts/xlsx_to_bin.py xlsx/*.xlsx -o data/

# 使用通配符（shell展开后等同于列出所有文件）
python3 scripts/xlsx_to_bin.py xlsx/table1.xlsx xlsx/table2.xlsx xlsx/table3.xlsx -o data/
```

### 3. C++ 程序中读取数据

```cpp
#include "chen/data/data_reader.h"

using namespace chen::data;

// 创建读取器（指定加密类型和密码）
DataReader reader("aes256_gcm", "chen_default_password");

// 加载数据文件
if (!reader.loadFromFile("data/example_config.bin")) {
    std::cerr << "加载失败!" << std::endl;
    return -1;
}

// 获取表
auto table = reader.getTable("example_config");
if (!table) {
    std::cerr << "表不存在!" << std::endl;
    return -1;
}

// 遍历数据
table->forEachRow([](size_t index, const RowData& row) {
    // 使用访问器获取数据
    RowAccessor accessor(table.get(), index);

    int32_t id = accessor["id"];
    std::string name = accessor["name"];
    int32_t attack = accessor["attack"];

    std::cout << "ID: " << id << ", Name: " << name << ", Attack: " << attack << std::endl;
});

// 或者直接获取值
int32_t id = table->getValue<int32_t>(0, "id", -1);
std::string name = table->getValue<std::string>(0, "name", "unknown");
```

### 4. 编译测试程序

```bash
# 需要链接的库
# -lz (zlib)
# -lcrypto (OpenSSL)

g++ -std=c++17 -I../ test_data_reader.cc ../chen/data/data_reader.cpp ../chen/data/data_decryptor.cpp -o test_data_reader -lz -lcrypto

# 运行测试
./test_data_reader data/example_config.bin
```

## 加密方案对比

| 加密方案 | 安全性 | 性能 | 推荐场景 |
|---------|--------|------|----------|
| AES-256-GCM | 很高 | 快 | **推荐**，通用场景 |
| ChaCha20-Poly1305 | 很高 | 快 | 软件优化场景 |
| 自定义加密 | 中等 | 中等 | 兼容性要求 |
| XOR | 低 | 最快 | 仅测试/兼容 |

## 数据格式说明

### xlsx 文件格式要求

- 第一行必须为字段名（表头）
- 支持的数据类型：
  - 整数（自动识别为 int32）
  - 浮点数（自动识别为 double）
  - 字符串
  - 空值（空单元格）
- 空行会被忽略

### 二进制文件格式（版本2）

```
[文件结构]
├── 加密数据（根据加密方案变化）
│   ├── 元数据长度 (4字节)
│   ├── 元数据 (JSON格式)
│   │   ├── type: 加密类型
│   │   └── metadata: 加密参数
│   └── 加密的载荷
└── [解密后]
    ├── MD5校验和 (16字节)
    ├── 压缩数据
    │   ├── 文件头 "CHGD" (4字节)
    │   ├── 版本号 (4字节) [当前为2]
    │   ├── 表数量 (4字节)
    │   ├── 保留字段 (4字节)
    │   └── 表数据...
    │       ├── 表大小 (4字节)
    │       ├── 表名长度 (4字节) + 表名
    │       ├── 字段数量 (4字节)
    │       ├── 字段名列表 (每个字段: 长度+名称)
    │       ├── 行数 (4字节)
    │       └── 行数据...
    │           └── 字段值列表 (每个字段: 类型+数据)
```

### 字段类型编码

| 编码 | 类型 | 说明 |
|------|------|------|
| 0 | NULL | 空值 |
| 1 | int8 | 8位整数 |
| 2 | int16 | 16位整数 |
| 3 | int32 | 32位整数 |
| 4 | int64 | 64位整数 |
| 5 | double | 双精度浮点 |
| 6 | string | 短字符串(<255字节) |
| 7 | string | 长字符串(>=255字节) |

## 命令行参数

```bash
xlsx_to_bin.py [输入文件...] -o 输出文件 [选项]

选项:
  -h, --help            显示帮助信息
  -o OUTPUT, --output   输出文件路径
  -v, --verbose         显示详细信息
  -e {none,xor,aes256_gcm,chacha20_poly1305,custom}
                        加密方案 (默认: aes256_gcm)
  -p PASSWORD, --password
                        加密密码 (默认使用默认密码)
```

## 安全建议

1. **使用强加密**：生产环境推荐使用 AES-256-GCM
2. **自定义密码**：不要使用默认密码
3. **密钥管理**：妥善管理加密密码，考虑使用密钥管理系统
4. **版本兼容**：确保Python和C++使用相同的加密方案版本

## 性能优化建议

1. **批量转换**：将多个表合并到一个 bin 文件
2. **内存映射**：大文件可以使用内存映射
3. **缓存机制**：频繁访问的数据可以缓存到内存
4. **异步加载**：可以使用异步加载避免阻塞

## 扩展功能

可以根据需要扩展以下功能：

- 支持更多数据类型（日期、布尔等）
- 支持数据索引功能
- 支持热更新机制
- 支持版本兼容性检查
- 支持数据验证规则
- 支持增量更新

## 示例输出

```
开始转换 2 个xlsx文件...
使用加密方案: aes256_gcm
使用合并模式：所有文件合并到一个bin
读取文件: xlsx/example_config.xlsx -> 表名: example_config
  - 字段数: 8
  - 行数: 7
读取文件: xlsx/table1.xlsx -> 表名: table1
  - 字段数: 3
  - 行数: 2

转换完成! 输出文件: data/merged.bin
包含表数量: 2
原始大小: 2048 字节
压缩后: 512 字节
压缩率: 25.0%
```

## 常见问题

### Q: ChaCha20-Poly1305 提示 nonce 错误？
A: Python cryptography 库和 OpenSSL 的 ChaCha20 实现存在兼容性问题，建议使用 AES-256-GCM。

### Q: 如何切换加密方案？
A: 使用 `-e` 参数指定加密方案，如 `-e aes256_gcm`。

### Q: C++ 端如何指定加密类型？
A: 创建 DataReader 时传入加密类型和密码：
```cpp
DataReader reader("aes256_gcm", "your_password");
```

### Q: 如何验证数据完整性？
A: 所有新加密方案都包含认证标签，会自动验证数据完整性。