# Protocol XML to C++ Header Converter

这是一个将XML格式的协议定义转换为C++头文件的工具。

## 目录结构

```
protocol/
├── xml/                          # XML协议定义文件目录
│   ├── template.xml              # 基础模板示例
│   ├── template_with_arrays.xml  # 数组类型示例
│   ├── extended_types.xml        # 扩展类型示例
│   ├── advanced_structures.xml   # 高级数据结构示例
│   ├── game_protocol.xml         # 游戏协议示例
│   └── modern_cpp_types.xml      # 现代 C++ 类型示例
├── include/                     # 生成的C++头文件目录
│   └── *.h                     # 自动生成的头文件
├── scripts/                    # 脚本目录
│   └── convert_xml.sh          # XML转换脚本
├── Makefile                    # 构建配置
├── format_code.sh              # 代码格式化脚本
└── README.md                  # 本文档
```

## 使用方法

### 转换所有XML文件
```bash
make
```

### 转换特定XML文件
```bash
make xml_to_header INPUT=xml/your_file.xml OUTPUT=your_file.h
```

### 清理生成的头文件
```bash
make clean
```

### 查看帮助
```bash
make help
```

## XML格式规范

### 基本结构

XML文件必须以`<header>`标签作为根元素：

```xml
<header name="protocol_name" desc="协议描述">
    <!-- 协议内容 -->
</header>
```

- `name`: 协议名称，用于生成头文件名和头文件保护宏
- `desc`: 协议描述，将作为头文件的文档注释

### 结构体定义

使用`<struct>`标签定义C++结构体：

```xml
<struct name="StructName" desc="结构体描述">
    <entry name="field_name" type="field_type" desc="字段描述"/>
    <entry name="another_field" type="another_type" desc="另一个字段"/>
</struct>
```

- `name`: 结构体名称，将转换为`tagStructName`格式（驼峰命名 + tag前缀）
- `desc`: 结构体描述，将作为结构体的文档注释

#### 支持的字段类型

##### 基本类型

| XML类型 | C++类型 | 说明 |
|---------|---------|------|
| `int` | `int32_t` | 32位有符号整数 |
| `uint` | `uint32_t` | 32位无符号整数 |
| `short` | `int16_t` | 16位有符号整数 |
| `ushort` | `uint16_t` | 16位无符号整数 |
| `char`/`byte` | `int8_t` | 8位有符号整数 |
| `ubyte` | `uint8_t` | 8位无符号整数 |
| `bigint` | `int64_t` | 64位有符号整数 |
| `biguint` | `uint64_t` | 64位无符号整数 |
| `bool` | `bool` | 布尔类型 |
| `string`/`stringvar` | `std::string` | 字符串类型 |
| `float` | `float` | 单精度浮点数 |
| `double`/`decimal` | `double` | 双精度浮点数 |
| `varint` | `int32_t` | 变长整数 |
| `timestamp`/`datetime` | `int64_t` | 时间戳 |
| `bytes`/`blob` | `std::vector<uint8_t>` | 字节数组 |
| `CustomStruct` | `tagCustomStruct` | 自定义结构体类型 |

##### 数组类型

| XML属性 | C++类型 | 说明 |
|---------|---------|------|
| `array="true" size="N"` | `std::array<T, N>` | 固定大小数组 |
| `array="true"` (无size) | `std::vector<T>` | 动态数组 |

示例：
```xml
<entry name="ids" type="uint" array="true" size="10" desc="固定大小数组"/>
<entry name="items" type="string" array="true" desc="动态数组"/>
```

##### 高级数据结构 (C++17)

| XML类型 | C++类型 | 说明 |
|---------|---------|------|
| `list<T>` | `std::list<T>` | 双向链表 |
| `map<K, V>` | `std::map<K, V>` | 键值映射 |
| `set<T>` | `std::set<T>` | 集合 |
| `pair<T1, T2>` | `std::pair<T1, T2>` | 键值对 |
| `tuple<T1, T2, ...>` | `std::tuple<T1, T2, ...>` | 元组（支持任意数量元素） |
| `optional<T>` | `std::optional<T>` | 可选值 |
| `variant<T1, T2, ...>` | `std::variant<T1, T2, ...>` | 联合类型 |

示例：
```xml
<!-- List/Vector -->
<entry name="skills" type="list&lt;uint&gt;" desc="技能ID列表"/>

<!-- Map -->
<entry name="properties" type="map&lt;string, int&gt;" desc="属性映射"/>

<!-- Set -->
<entry name="tags" type="set&lt;string&gt;" desc="标签集合"/>

<!-- Pair -->
<entry name="coordinate" type="pair&lt;float, float&gt;" desc="坐标点"/>

<!-- Tuple -->
<entry name="position" type="tuple&lt;float, float, float&gt;" desc="3D位置"/>
<entry name="playerInfo" type="tuple&lt;string, uint, PlayerClass&gt;" desc="玩家信息"/>

<!-- Optional -->
<entry name="guildId" type="optional&lt;uint&gt;" desc="可选的公会ID"/>
<entry name="nickname" type="optional&lt;string&gt;" desc="可选的昵称"/>

<!-- Variant -->
<entry name="value" type="variant&lt;int, string, bool&gt;" desc="可以是多种类型的值"/>
<entry name="event" type="variant&lt;string, tuple&lt;uint, uint&gt;, list&lt;string&gt;&gt;" desc="复杂事件数据"/>

<!-- 嵌套组合 -->
<entry name="complexData" type="optional&lt;variant&lt;list&lt;uint&gt;, map&lt;string, tuple&lt;uint, string&gt;&gt;&gt;&gt;" desc="复杂数据结构"/>
```

### 宏定义

使用`<marco>`标签定义宏常量：

```xml
<marco name="macro_name" value="macro_value" type="value_type" desc="宏描述"/>
```

- `name`: 宏名称，将转换为大写格式
- `value`: 宏值
- `type`: 值类型（`string`类型会自动添加引号）
- `desc`: 宏描述（可选）

#### 示例

```xml
<!-- 数字宏 -->
<marco name="max_users" value="1000" type="number" desc="最大用户数"/>

<!-- 字符串宏 -->
<marco name="app_name" value="my_app" type="string" desc="应用名称"/>
```

生成的C++代码：
```cpp
#define MAX_USERS 1000   /* 最大用户数 */
#define APP_NAME "my_app" /* 应用名称 */
```

### 枚举定义

使用`<marcogroup>`标签定义枚举类型：

```xml
<marcogroup name="EnumName" desc="枚举描述">
    <marco name="enum_value_name" value="enum_value" desc="枚举值描述"/>
    <marco name="another_value" value="another_value" desc="另一个枚举值"/>
</marcogroup>
```

- `name`: 枚举名称，将转换为驼峰命名格式
- `desc`: 枚举描述，将作为枚举的文档注释

#### 示例

```xml
<marcogroup name="UserStatus" desc="用户状态码">
    <marco name="user_inactive" value="0" desc="非活跃用户状态"/>
    <marco name="user_active" value="1" desc="活跃用户状态"/>
    <marco name="user_banned" value="2" desc="被封禁用户状态"/>
</marcogroup>
```

生成的C++代码：
```cpp
/**
 * @brief 用户状态码
 */
enum UserStatus {
    USER_INACTIVE = 0, /* 非活跃用户状态 */
    USER_ACTIVE = 1,   /* 活跃用户状态 */
    USER_BANNED = 2,   /* 被封禁用户状态 */
};
```

## 完整示例

下面是一个完整的XML示例文件：

```xml
<header name="protocol_userinfo" desc="用户信息协议定义">
    <!-- 宏定义 -->
    <marco name="max_user_count" value="1000" type="number" desc="允许的最大用户数"/>
    <marco name="default_user_name" value="guest" type="string" desc="默认用户名"/>

    <!-- 枚举定义 -->
    <marcogroup name="UserStatus" desc="用户状态码">
        <marco name="user_inactive" value="0" desc="非活跃用户状态"/>
        <marco name="user_active" value="1" desc="活跃用户状态"/>
        <marco name="user_banned" value="2" desc="被封禁用户状态"/>
    </marcogroup>

    <!-- 结构体定义 -->
    <struct name="UserInfo" desc="用户基本信息结构">
        <entry name="name" type="string" desc="用户姓名"/>
        <entry name="email" type="string" desc="用户邮箱"/>
        <entry name="age" type="int" desc="用户年龄"/>
        <entry name="score" type="uint" desc="用户积分"/>
        <entry name="status" type="UserStatus" desc="用户状态"/>
    </struct>

    <struct name="Address" desc="地址信息结构">
        <entry name="street" type="string" desc="街道地址"/>
        <entry name="city" type="string" desc="城市名称"/>
        <entry name="zipCode" type="string" desc="邮政编码"/>
        <entry name="country" type="string" desc="国家名称"/>
    </struct>

    <struct name="Person" desc="人员信息结构">
        <entry name="id" type="uint" desc="人员唯一标识"/>
        <entry name="name" type="string" desc="人员全名"/>
        <entry name="age" type="int" desc="人员年龄"/>
        <entry name="address" type="Address" desc="人员地址信息"/>
        <entry name="score" type="bigint" desc="人员评分"/>
    </struct>
</header>
```

## 生成的C++头文件

上述XML示例将生成如下的C++头文件：

```cpp
/**
 * @brief 用户信息协议定义
 */
#ifndef __PROTOCOL_USERINFO_H__
#define __PROTOCOL_USERINFO_H__

#include <cstdint>
#include <string>

#define MAX_USER_COUNT 1000   /* 允许的最大用户数 */
#define DEFAULT_USER_NAME "guest" /* 默认用户名 */

/**
 * @brief 用户状态码
 */
enum UserStatus {
    USER_INACTIVE = 0, /* 非活跃用户状态 */
    USER_ACTIVE = 1,   /* 活跃用户状态 */
    USER_BANNED = 2,   /* 被封禁用户状态 */
};

/**
 * @brief 用户基本信息结构
 */
struct tagUserInfo {
    std::string name;    /* 用户姓名 */
    std::string email;   /* 用户邮箱 */
    int32_t age;         /* 用户年龄 */
    uint32_t score;      /* 用户积分 */
    UserStatus status;   /* 用户状态 */
};

/**
 * @brief 地址信息结构
 */
struct tagAddress {
    std::string street;  /* 街道地址 */
    std::string city;    /* 城市名称 */
    std::string zipCode; /* 邮政编码 */
    std::string country; /* 国家名称 */
};

/**
 * @brief 人员信息结构
 */
struct tagPerson {
    uint32_t id;         /* 人员唯一标识 */
    std::string name;    /* 人员全名 */
    int32_t age;         /* 人员年龄 */
    tagAddress address;  /* 人员地址信息 */
    int64_t score;       /* 人员评分 */
};

#endif // __PROTOCOL_USERINFO_H__
```

## 新增功能特性

### 1. 扩展的数据类型支持
- **扩展基本类型**：新增 short、ushort、char、byte、ubyte、bool、float、double 等类型
- **数组支持**：支持固定大小数组（std::array）和动态数组（std::vector）
- **高级数据结构**：支持 list、map、set、pair、tuple、optional、variant 等 C++17 类型

### 2. 智能头文件包含
脚本会根据实际使用的类型自动包含必要的头文件，避免不必要的依赖：
- 使用 `std::array` 时包含 `<array>`
- 使用 `std::vector` 时包含 `<vector>`
- 使用 `std::list` 时包含 `<list>`
- 使用 `std::map` 时包含 `<map>`
- 使用 `std::set` 时包含 `<set>`
- 使用 `std::tuple` 时包含 `<tuple>`
- 使用 `std::optional` 时包含 `<optional>`（C++17）
- 使用 `std::variant` 时包含 `<variant>`（C++17）
- 使用 `std::pair` 时包含 `<utility>`

### 3. 嵌套模板支持
支持任意深度的嵌套模板类型：
```xml
<entry name="complex" type="map&lt;string, list&lt;tuple&lt;uint, optional&lt;string&gt;&gt;&gt;&gt;" desc="复杂嵌套类型"/>
```

### 4. HTML实体解码
自动处理XML中的HTML实体编码：
- `&lt;` → `<`
- `&gt;` → `>`
- `&amp;` → `&`

### 5. 临时文件自动清理
脚本使用 trap 机制确保所有临时文件在退出时被自动清理，无论是正常退出还是错误退出。

### 6. 批量格式化优化
Makefile 优化了格式化流程，在所有文件转换完成后统一进行格式化，提高效率。

## 注意事项

1. **命名规范**：
   - 结构体名使用驼峰命名法，自动添加`tag`前缀
   - 宏名自动转换为大写格式
   - 枚举名使用驼峰命名法
   - 枚举值自动转换为大写格式

2. **处理顺序**：
   XML文件中的元素将按以下顺序生成到C++头文件中：
   1. 独立宏定义（`<marco>`）
   2. 枚举定义（`<marcogroup>`）
   3. 结构体定义（`<struct>`）

3. **代码格式化**：
   生成的头文件会自动使用`clang-format`进行格式化，确保代码风格一致。

4. **依赖关系**：
   - 确保所有引用的自定义结构体都在同一个XML文件中定义
   - 枚举类型可以在结构体字段中直接使用

5. **错误处理**：
   - 如果XML格式不正确，转换脚本会给出相应的错误提示
   - 建议在转换前验证XML文件的格式正确性

6. **C++17要求**：
   使用 `optional` 和 `variant` 类型需要编译器支持 C++17 标准。

7. **XML实体编码**：
   在XML中使用 `<` 和 `>` 符号时，请使用HTML实体编码 `&lt;` 和 `&gt;`，脚本会自动解码。

8. **模板类型嵌套**：
   支持任意深度的模板类型嵌套，但请确保XML中的括号匹配正确。

## 现代 C++ 类型完整示例

查看 `xml/modern_cpp_types.xml` 文件，它展示了所有现代 C++ 类型的使用方法，包括：

- 基本类型转换
- List/Vector 动态数组
- Tuple 多元组（3个及以上元素）
- Optional 可选字段
- Variant 联合类型
- 深层嵌套的复合类型

生成的 `include/modern_cpp_types.h` 展示了对应的 C++17 代码结构。

## 统一头文件功能

### 动态命名的统一头文件

为了方便业务层使用，系统会自动生成一个统一的头文件。该文件名基于当前文件夹名称动态生成：
- 在 `protocol/` 文件夹中会生成 `include/protocol_all.h`
- 在 `resource/` 文件夹中会生成 `include/resource_all.h`
- 在其他文件夹中会生成 `include/{文件夹名}_all.h`

这样可以在不同项目中复用而不会产生命名冲突。

#### 特性
- **自动生成**: 每次执行 `make`、`make convert_only` 或 `make xml_to_header` 时自动更新
- **自动包含**: 自动包含 `include/` 目录下所有 `.h` 文件（除了自身）
- **防重复包含**: 使用标准头文件保护宏
- **文档化**: 包含完整的文档说明

#### 业务层使用
业务层只需要包含一个头文件：

```cpp
// 在protocol文件夹中的项目
#include "protocol/include/protocol_all.h"

// 在resource文件夹中的项目
#include "resource/include/resource_all.h"

// 在其他文件夹中的项目
#include "{文件夹名}/include/{文件夹名}_all.h"

// 现在可以使用所有XML中定义的协议结构
int main() {
    // 使用协议结构...
    return 0;
}
```

#### 单独生成统一头文件
```bash
make generate_unified_header
```

#### 示例文件
参考 `example_usage.cpp` 查看业务层如何使用统一头文件。

## 更新历史

- **v1.0**: 基础功能实现，支持基本类型、结构体、枚举、宏定义
- **v1.1**: 添加数组类型支持（固定数组和动态数组）
- **v1.2**: 扩展基本类型支持（short、ushort、char、byte、ubyte、bool、float、double）
- **v1.3**: 添加高级数据结构（map、set、pair）
- **v1.4**: 实现现代 C++ 类型（list、tuple、optional、variant）
- **v1.5**: 优化临时文件管理和批量格式化
- **v1.6**: 修复嵌套模板解析问题，支持任意深度嵌套
- **v1.7**: 新增统一头文件功能，简化业务层使用