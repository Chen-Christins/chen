# 测试

本文说明 chen 的测试分层、运行方式和编写约定。

## 目标

框架同时叠加了协作式协程、epoll、hook、fork 多进程四种机制，正确性无法只靠人工验证。测试门禁的最低要求是：

- **每次改动都能一条命令跑完**：`make test`
- **不依赖网络、数据库、外部配置**：默认用例必须可重复、可在 CI 冷启动
- **失败信息可定位**：输出用例名、断言表达式、文件名和行号

## 分层与标签

CTest 用标签区分用例：`make test` 跑 `unit|smoke`，集成用例用 `make test-integration`。

| 标签          | 内容                                                     | 运行时机      | 依赖                 |
|---------------|----------------------------------------------------------|---------------|----------------------|
| `unit`        | 纯逻辑：序列化、字符串、配置、时间、指标计算等           | 每次构建 / CI | 无                   |
| `smoke`       | 链接/启动冒烟：`server -v`、`libchen` 的 SONAME 符号链接 | 每次构建 / CI | 无                   |
| `integration` | 起真实 server、发请求、热重载 / 优雅关闭                 | CI / 手工     | 端口、临时目录、curl |

## 目录结构

```
tests/
├── TestHarness.hpp        # 断言 / 注册 / 过滤 / 统计（header-only）
├── Unit/                  # 单元测试（标签 unit）
│   ├── TestByteArray.cpp
│   ├── TestStringUtil.cpp
│   ├── TestConfig.cpp
│   ├── TestTimeUtil.cpp
│   ├── TestUri.cpp
│   ├── TestHttpParser.cpp
│   ├── TestEncryptorUtil.cpp
│   ├── TestHttp.cpp
│   ├── TestConfigXml.cpp
│   ├── TestMetrics.cpp
│   ├── TestRateLimiter.cpp
│   ├── TestFuture.cpp
│   ├── TestSSESession.cpp
│   ├── TestTcpDrain.cpp
│   └── TestOrmGen.cpp
├── ModuleTest/
│   └── DrainModule.cc     # 集成测试模块（/drain/{slow,fast,stream,sse}）
├── integration/
│   ├── drain_reload.sh    # 热重载 drain 集成测试（标签 integration）
│   └── module/            # libdrain_test_module.so 输出目录（gitignore）
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

### 集成测试：热重载 drain

```bash
make test-integration
# 等价于：构建 server + drain_test_module，然后 ctest -L integration
```

[`drain_reload.sh`](../tests/integration/drain_reload.sh) 会：

1. 用临时目录 + 随机端口起一个真实 server（master/worker 模型）；
2. 发一个 `/drain/slow?ms=800` 的慢请求；
3. 在途时给 master 发 `SIGHUP`，触发 fork 新 worker、旧 worker drain；
4. 断言慢请求仍拿到 `200 + slow-ok`、reload 后新请求正常、日志里没有 drain 超时；
5. 发 `SIGTERM`，断言 master 在 10s 内退出且日志出现 `graceful shutdown complete`。

同一脚本注册了两个 CTest 用例：

| 用例名                        | 协议                          | 额外断言                                                        |
|-------------------------------|-------------------------------|-----------------------------------------------------------------|
| `integration_drain_reload`    | HTTP/1.1                      | 响应带 `Connection: close`                                      |
| `integration_h2_drain_reload` | HTTP/2（h2c prior knowledge） | 日志出现 `H2 session drained`（证明走 GOAWAY 而不是被超时强杀） |

H2 用例用 `CHEN_IT_PROTO=http2` 选择协议；如果本机 curl 不带 HTTP/2 支持，脚本以 77 退出，
CTest 记为 **Skipped** 而不是失败。WS/Rock/RPC/Game 是长连接协议，仍按「立即关闭」处理。

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
- 单元测试不启动监听端口、不连接数据库；这类场景放 `integration`。

## CI

[`.github/workflows/test.yml`](../.github/workflows/test.yml) 在 push / PR 时执行：

1. 安装系统依赖 + `python3 deps.py sync`
2. `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCHEN_BUILD_TESTS=ON`
3. 定向构建：`chen` + 全部单测（`test_unit_*`）+ `server` + `lifecycle_module` + `drain_test_module`
4. `ctest -L "unit|smoke" --output-on-failure`
5. `ctest -L integration --output-on-failure`

失败时上传 `build/Testing/Temporary/LastTest.log`。

发版流程（`package-sdk-ubuntu.yml`）与测试流程分离：测试失败不会阻塞发包，但合并前必须通过。

## 历史用例迁移状态

`tests/` 下早期的 `Test*.cc` 是"手工跑 + 肉眼看输出"的程序。可离线、有明确断言的已迁到
`tests/Unit/` 并接入 CTest；原程序保留，方便单独调试。

已迁移：

| 单元测试                | 来源                                                                           |
|-------------------------|--------------------------------------------------------------------------------|
| `test_unit_bytearray`   | `TestByteArray.cc`                                                             |
| `test_unit_stringutil`  | `TestStringUtil.cc`                                                            |
| `test_unit_config`      | `TestConfig.cc`                                                                |
| `test_unit_timeutil`    | `TestTimeUtil.cc`                                                              |
| `test_unit_uri`         | `TestUri.cc`                                                                   |
| `test_unit_httpparser`  | `TestHttpParser.cc`                                                            |
| `test_unit_encryptor`   | `TestEncryptorUtil.cc`（哈希/HMAC/AES-GCM/PBKDF2；RSA、bcrypt 留在手工程序）   |
| `test_unit_http`        | `TestHttp.cc`（原程序只 dump，迁移时补了断言）                                 |
| `test_unit_configxml`   | `TestConfigXml.cc`                                                             |
| `test_unit_metrics`     | `TestMetrics.cc`                                                               |
| `test_unit_ratelimiter` | `TestRateLimiter.cc`                                                           |
| `test_unit_future`      | `TestFuture.cc`（线程路径 + 自建 IOManager 的 fiber 路径）                     |
| `test_unit_ssesession`  | 新增：用假 HttpSession 抓取 SSE 编码与响应头（无迁移来源）                     |
| `test_unit_tcpdrain`    | 新增：drain 簿记逻辑（在途计数、空闲判定、`closeIdleClients()`，无 IOManager） |
| `test_unit_ormgen`      | `TestOrmGen.cc`（额外编译 `src/Orm/*.cc`）                                     |

有意不迁移（保留为手工程序）：

- **网络 / 进程 / 守护**：`TestSocket`、`TestTcpServer`、`TestHttpServer`、`TestH2Server`、`TestHttpClient`、
  `TestHttpConnection`、`TestWSClient`、`TestWSServer`、`TestSSEServer`、`SsePushExample`、`TestRpcClient`、
  `TestRpcServer`、`TestRpcHub`、`TestRock`、`TestGameClient`、`TestGameRpc`、`TestModule`、
  `TestApplication`、`TestDaemon`、`TestFileWatcher` —— 需要端口、模块热加载或 fork/daemon。
- **数据库**：`TestMySQL`、`TestPostgreSQL` —— 需要真实实例；对应能力由集成测试或手工验证。
- **需要外部数据**：`TestDataReader` —— 依赖 `xlsx_to_bin.py` 生成的加密 `.bin`，仓库里没有示例数据。
- **时间/压测基准**：`TestThread`、`TestFiber`、`TestFiberBench`、`TestSchedule`、`TestMassiveSchedule`、
  `TestIOManager`、`TestHook`、`TestMetricsBench`、`TestLogSizeRotate`、`TestLogReloadFormatter` —— 观测吞吐/调度
  行为或依赖真实文件轮转，不适合当门禁用例。
- **会污染进程**：`TestEnv` —— 修改进程标题（`argv[0]`/`PR_SET_NAME`），放进单测套件会影响同进程其它用例。

## 已知限制

- **ASAN 与协程**：`boost::context` 切换栈需要 `__sanitizer_start_switch_fiber` 注解，当前未接入；对
  `libchen` 整体开 ASAN 后运行涉及 fiber 切换的用例可能出现误报。除 `test_unit_future`（自建
  `IOManager` 跑 fiber 路径）外，其余单测都是纯逻辑，可以安全地用 `-DENABLE_ASAN=ON` 单独跑。
- **drain 的协议覆盖**：HTTP/1.1 有请求边界，能等到在途请求结束；HTTP/2 在 drain 时发送 GOAWAY，
  等 `streams_` 清空后关闭会话；WS/Rock/RPC/Game 是长连接、没有请求边界，drain 期间仍然立即关闭。
- **非 Linux 不构建**：见 `AGENTS.md`。macOS/Windows 上只做静态检查，测试结果以 Linux 开发机/CI 为准。
- 历史测试程序没有断言框架、部分会阻塞在 `accept`，因此不注册进 CTest；可离线断言的部分已按
  “历史用例迁移状态”迁到 `tests/Unit/`，其余保留为手工/联调程序。
