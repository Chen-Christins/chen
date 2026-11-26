# 测试

本文说明 chen 的测试分层、运行方式和编写约定。

## 目标

框架同时叠加了协作式协程、epoll、hook、fork 多进程四种机制，正确性无法只靠人工验证。测试门禁的最低要求是：

- **每次改动都能一条命令跑完**：`make test`
- **不依赖网络、数据库、外部配置**：默认用例必须可重复、可在 CI 冷启动
- **失败信息可定位**：输出用例名、断言表达式、文件名和行号

## 分层与标签

CTest 用标签区分用例，CI 只跑 `unit|smoke`，联调用例按需手工执行。

| 标签          | 内容                                             | 运行时机            | 依赖           |
|---------------|--------------------------------------------------|---------------------|----------------|
| `unit`        | 纯逻辑：序列化、字符串、配置、时间、指标计算等   | 每次构建 / CI       | 无             |
| `smoke`       | 进程能链接、能启动的冒烟检查（如 `server -v`）   | 每次构建 / CI       | 无             |
| `integration` | 起真实 server、发请求、reload/关闭（待补，见下） | Linux 开发机 / 夜间 | 端口、临时目录 |

> `integration` 依赖「真正的 drain」修复合入后才有意义：当前 `closeAllClients()` 会在
> `drain_timeout_ms` 之前关闭连接，“在途请求不中断”的断言必然失败。等该修复合入后再补。

## 目录结构

```
tests/
├── TestHarness.hpp        # 断言 / 注册 / 过滤 / 统计（header-only）
├── Unit/                  # 单元测试（标签 unit）
│   ├── TestByteArray.cpp
│   ├── TestStringUtil.cpp
│   ├── TestConfig.cpp
│   └── TestTimeUtil.cpp
├── *.cc                   # 历史测试程序（手工执行，部分需要端口/数据库）
└── bin/                   # 测试可执行文件输出目录（ctest 用目标路径，无需关心）
```

## 运行

```bash
# 1) Debug 构建（默认会打开 CHEN_BUILD_TESTS）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

# 2) 跑单元 + 冒烟
ctest --test-dir build --output-on-failure -L "unit|smoke"

# 或使用 Makefile 包装
make test
```

Release 构建默认不编译测试，需要显式打开：

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCHEN_BUILD_TESTS=ON
cmake --build build-release -j"$(nproc)"
ctest --test-dir build-release --output-on-failure -L unit
```

只跑某个用例：

```bash
ctest --test-dir build -R test_unit_bytearray --output-on-failure
tests/bin/test_unit_bytearray --filter=varint
tests/bin/test_unit_bytearray --list
```

## 编写新用例

单元测试统一放在 `tests/Unit/`，一个主题一个可执行文件，文件名 `Test<主题>.cpp`。

```cpp
#include "../TestHarness.hpp"
#include "src/ByteArray/ByteArray.hpp"

CHEN_TEST(bytearray, fixed_int_roundtrip)
{
    chen::ByteArray ba;
    ba.writeFint32(42);
    ba.setPosition(0);
    CHEN_CHECK_EQ(ba.readFint32(), 42);
    CHEN_CHECK_EQ(ba.getReadSize(), 0u);
}

CHEN_TEST_MAIN()
```

注册到 CMake（`tests/CMakeLists.txt`）：

```cmake
chen_add_unit_test(test_unit_bytearray Unit/TestByteArray.cpp)
```

`chen_add_unit_test` 会自动加 `unit` 标签、60s 超时和稳定的工作目录。

## 断言

| 宏                               | 说明                                      |
|----------------------------------|-------------------------------------------|
| `CHEN_CHECK(cond)`               | 条件为真                                  |
| `CHEN_CHECK_EQ(lhs, rhs)`        | 相等，失败时打印两侧取值                  |
| `CHEN_CHECK_NE(lhs, rhs)`        | 不等                                      |
| `CHEN_CHECK_CMP(lhs, rhs, op)`   | 大小比较，`op` 传 `<` / `<=` / `>` / `>=` |
| `CHEN_CHECK_THROW(expr, type)`   | 期望抛出 `type`                           |
| `CHEN_CHECK_NO_THROW(expr)`      | 期望不抛异常                              |
| `CHEN_CHECK_NEAR(lhs, rhs, eps)` | 浮点近似                                  |

断言失败会抛 `chen::test::Failure`，只终止当前用例，不影响同进程其他用例。需要中间状态时优先把一个用例拆成多个 `CHEN_TEST`
，而不是依赖执行顺序。

> ⚠️ 断言是宏，参数按**顶层逗号**切分。`LexicalCast<std::string, int>()("42")` 这类含模板逗号的表达式
> 会被当成 3 个参数，编译报 `passed 3 arguments, but takes just 2`。用类型别名（推荐）或额外括号解决：
>
> ```cpp
> using StringToInt = chen::LexicalCast<std::string, int>;
> CHEN_CHECK_EQ(StringToInt()("42"), 42);
> // 或
> CHEN_CHECK_EQ((chen::LexicalCast<std::string, int>()("42")), 42);
> ```

## 隔离约定

- 用例之间**不能共享可变状态**：全局单例（`Config`、`LoggerMgr`、`RegistryMgr` 等）里的改动要在用例内归位。
- 配置项名用小写 `unit_test.<模块>.<字段>`，避免和真实配置冲突。
- 时间相关断言只使用显式时区接口（如 `Time2StrSec(ts, fmt, 0)`），不要依赖 `TZ`。
- 不写死绝对路径；确实需要文件时使用 `FSUtil` 配合临时目录，并在用例结束时清理。
- 不启动监听端口、不连接数据库；这类用例标 `integration` 并默认不注册。

## CI

[`.github/workflows/test.yml`](../.github/workflows/test.yml) 在 push / PR 时执行：

1. 安装系统依赖 + `python3 deps.py sync`
2. `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCHEN_BUILD_TESTS=ON`
3. `cmake --build build -j$(nproc)`
4. `ctest --test-dir build -L "unit|smoke" --output-on-failure`

失败时上传 `build/Testing/Temporary/LastTest.log`。

发版流程（`package-sdk-ubuntu.yml`）与测试流程分离：测试失败不会阻塞发包，但合并前必须通过。

## 已知限制

- **ASAN 与协程**：`boost::context` 切换栈需要 `__sanitizer_start_switch_fiber` 注解，当前未接入；对
  `libchen` 整体开 ASAN 后运行涉及 fiber 切换的用例可能出现误报。纯逻辑单测（`unit`）不开 fiber，
  可以安全地用 `-DENABLE_ASAN=ON` 单独跑。
- **非 Linux 不构建**：见 `AGENTS.md`。macOS/Windows 上只做静态检查，测试结果以 Linux 开发机/CI 为准。
- 历史测试程序没有断言框架、部分会阻塞在 `accept`，因此不注册进 CTest；后续逐步迁移到 `tests/Unit/`。
