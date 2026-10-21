# 框架优先（Framework First）

本仓库 / SDK 已经把基础设施封装好了。 **先找框架入口，再考虑标准库，最后才自己写。**

> 本文与模块 README（`chen/<module>/README.md`，SDK 内为 `include/chen/<module>/README.md`）冲突时，以模块 README 为准。

## 核心原则

1. 框架封装 > 标准库 > 第三方库 > 自研
2. 业务模块只写业务逻辑；IO、日志、配置、DB、协议、并发等基础设施交给框架
3. 同一能力不要写第二遍；框架没有的，先补框架，再在业务里调用
4. 新增可复用能力放到 `chen/<module>/`，带 README + 测试，不要散落在业务目录
5. 下游只依赖 SDK 公开头文件与 `libchen.so`，不要复制框架源码

## 30 秒决策流程

1. 查下面的「模块索引」，打开对应 README
2. 搜现成 API：`grep -rn "class .*Util\|class .*Mgr\|typedef Singleton" chen/<module>/`
3. 看框架内同类写法：`grep -rn "StringUtil::\|TimeUtil::\|FSUtil::" chen tests | head`
4. 确认没有合适的封装，再用标准库
5. 标准库也没有，再自己写；如果是通用能力，回补到 `chen/`

## 能力对照表

| 需求                   | 用框架的                                                             | 不要                               |
|------------------------|----------------------------------------------------------------------|------------------------------------|
| 日志                   | `INFO/WARN/ERROR(logger) << ...` + `LOG_NAME`                        | `printf` / `std::cout` / 自建日志  |
| 配置                   | `Config::Lookup<T>` + `addListener`                                  | 自己读 yaml / json / ini           |
| 时间                   | `TimeUtil::GetCurrentMs/Time2Str/Str2Time/IsExpired/...`             | 到处 `std::chrono` / `time()`      |
| 字符串                 | `StringUtil::Split/Join/Trim/Replace/UrlEncode/Base64/Hex`           | 手写 split / trim / base64         |
| 文件                   | `FSUtil::ListAllFile/ReadFileToString/Exists/Mkdir/OpenForWrite`     | 裸 `fopen` / `std::filesystem`     |
| 环境 / 参数            | `Env` / `EnvMgr`                                                     | 自己解析 `argv`                    |
| JSON                   | `JsonUtil::FromString/ToString`                                      | 散落 `Json::Reader`                |
| 类型转换               | `LexicalCast<From,To>`、`TypeUtil::Atoi/Atof/ToString`               | 一律手写 `std::stoi`               |
| 加解密                 | `EncryptorUtil::MD5/SHA1/SHA256/HMAC/AES-GCM`                        | 直接调 OpenSSL                     |
| 压缩                   | `CompressUtil::Gzip/Zlib`                                            | 直接调 zlib                        |
| 随机                   | `RandomUtil::RandInt/RandString/RandElement`、`SnowflakeIdGenerator` | `rand()`                           |
| 字节序                 | `chen::endian`、`htonll` / `ntohll`                                  | 手写移位                           |
| 二进制缓冲             | `ByteArray`（`writeFint32` / `readStringVint` 等）                   | 裸 `memcpy` 拼包                   |
| 协程 / 异步            | `IOManager::GetThis()->schedule(...)`、`addTimer(...)`               | `std::thread` + `sleep_for`        |
| 并发等待               | `FiberSemaphore` / `Semaphore`                                       | fiber 里长时间 `std::mutex`        |
| 单例                   | `Singleton` / `LeakySingleton`                                       | 自己写双重检查锁                   |
| 锁                     | `SpinLock` + `MutexType::Lock`                                       | 裸 pthread mutex                   |
| 地址 / 网络            | `Address::LookupAny`、`Socket::CreateTCP/UDP`                        | `getaddrinfo` / `socket` / `epoll` |
| TCP 服务               | `TcpServer` / `TcpServerFactory`                                     | 自己写 accept 循环                 |
| HTTP / WS              | `HttpServer` + `ServletDispatch`、`WSServer`                         | 自己解析 HTTP                      |
| RPC                    | `RpcServer` / `RpcClient` / `RpcClientPoolMgr` / Hub                 | 自定义跨进程协议                   |
| Redis / MySQL / SQLite | `RedisMgr` / `MySQLMgr` / `SQLite3Mgr` + `*Util`                     | 自己建连接 / 连接池                |
| SQL 构建               | `QueryBuilder`                                                       | 字符串拼 SQL                       |
| 分库分表               | `ShardRouterMgr`                                                     | 自己取模路由                       |
| ORM                    | `chen/orm`（XML 定义 + Dao）                                         | 手写 CRUD                          |
| 事件 / 解耦            | `EventBusMgr` / `EventDispatcherMgr`                                 | 自己维护回调表                     |
| 缓存                   | `LruCache` / `HashLruCache`                                          | 自己写 LRU                         |
| 限流                   | `RateLimiter` / `ConnLimiter`                                        | 自己计数                           |
| 指标                   | `RegistryMgr` 的 `Counter/Gauge/Histogram`                           | 自己统计                           |
| 模块                   | `chen::Module` 生命周期                                              | 自己 `dlopen`                      |
| 配置热加载             | `FileWatcher` + `ConfigVar::addListener`                             | 自建 inotify 轮询                  |
| 路径处理               | `FSUtil::Dirname/Basename/GetCwd`                                    | 手写字符串截取                     |

## 分领域规则

### 日志 / 配置

- `.cc` 顶部：`static Logger::ptr logger = LOG_NAME("system");`，然后 `ERROR(logger) << ...`
- 头文件里不要定义 static logger（ODR）；日志级别/格式/切分走 YAML `logs` 配置
- 配置一律 `Config::Lookup<T>`，名字必须匹配 `[a-z0-9_.]+`；变更用 `addListener`
- 下游不要用 `node.as<bool>()`（`convert<bool>::decode` 未随 `libchen.so` 导出）：用 `Config::Lookup<bool>` 或
  `LexicalCast<std::string, bool>`，raw node 先 `as<std::string>()`

### 时间 / 字符串 / 文件

- 时间：`TimeUtil::GetCurrentMs/Us/Sec`、`Time2Str`、`Str2Time`、`IsExpired`、`GetDayStart` 等
- 字符串：`StringUtil::Split` 默认跳过空段；`UrlEncode`（空格→`+`）与 `URLEncode`（空格→`%20`）别用混
- 文件：`FSUtil::ListAllFile(files, dir, ".yml")`，第二个参数名是 `subfix`（不是拼写错误）
- 加密：SHA / MD5 返回 hex；HMAC 返回原始字节，需要原始字节时用 `StringUtil::HexDecode`

### 并发 / IO

- 异步任务用 `IOManager::GetThis()->schedule(fn)`，不要 `std::thread`
- 定时器用 `IOManager::GetThis()->addTimer(ms, fn, recurring)`
- fiber 内 `sleep/usleep/nanosleep` 已被 hook，可直接用；不要 `sleep_for` 卡线程
- 阻塞 IO 走 hook 后的 `read/write` 或 `Socket`，不要自己 `epoll_wait`
- 跨 fiber 等待用 `FiberSemaphore`；短临界区可用 `SpinLock`

### 数据库

- 连接一律从 `*Mgr` 拿，不要自己 `connect` / 自己建池
- Redis 用 `RedisUtil::Cmd` 或 `RedisMgr::get`；MySQL 用 `MySQLUtil::Query` 或 `MySQLMgr::get`；SQLite3 同理
- SQL 用 `QueryBuilder`，不要拼接字符串（注入 + 转义问题）
- 分库分表用 `ShardRouterMgr`；表操作优先 ORM Dao
- DB 操作跑在 `FoxThread` 上，不要在 IO 线程同步阻塞

### 服务端 / 协议

- HTTP 用 `HttpServer` + `ServletDispatch::addServlet`；WebSocket 用 `WSServer`
- 跨进程用 `RpcServer` / `RpcClient`：`registerService` 注册，`callRouted` / `notifyRouted` 路由
- 长连接客户端用 `RpcClientPoolMgr`
- 游戏二进制协议用 `GenericProtocolServer` + `IMessageHandler`
- 模块用 `Module` 生命周期：`onLoad → onServerReady → onServerUp → onTick → onDeactivate → onUnload`

### 事件 / 缓存 / 限流 / 指标

- 事件：`EventBusMgr`（同步 `emit` / 异步 `emitAsync`）
- 缓存：`LruCache` / `HashLruCache`
- 限流：`RateLimiter` / `ConnLimiter`
- 指标：`RegistryMgr::getCounter/getGauge/getGaugeFunc/getHistogram`

## 反模式自查

业务代码 / 新增代码里看到这些，先停下来找框架替代：

| 信号                                                  | 应该用                                                    |
|-------------------------------------------------------|-----------------------------------------------------------|
| `printf(` / `std::cout`                               | 日志宏                                                    |
| `std::this_thread::sleep_for` / 业务逻辑里的 `sleep(` | `IOManager::addTimer` 或 hook 后的 sleep                  |
| `std::thread` / `pthread_create`                      | `IOManager::schedule`                                     |
| `std::filesystem` / `fopen`                           | `FSUtil`                                                  |
| `socket(` / `epoll_`                                  | `Socket` / `TcpServer`                                    |
| `rand()` / `random_device`                            | `RandomUtil`                                              |
| 散落的 `std::chrono::system_clock::now`               | `TimeUtil`                                                |
| 手写 `split` / `trim` / `urlencode`                   | `StringUtil`                                              |
| 直接用 `hiredis` / `mysql_real_connect`               | `RedisMgr` / `MySQLMgr`                                   |
| `SELECT ...` 字符串拼接                               | `QueryBuilder`                                            |
| `node.as<bool>()`（下游）                             | `Config::Lookup<bool>` / `LexicalCast<std::string, bool>` |
| 每个模块自建连接池 / 自建 LRU                         | `*Mgr` / `LruCache`                                       |

## 什么时候可以自己写

- 框架确实没有这个能力
- 性能场景：用框架的底层原语（`Socket` / `ByteArray` / `IOManager`），而不是绕开整个框架
- 必须直连系统调用：先确认 `chen/hook/` 是否已覆盖
- 依赖已在 `deps/`：通过 `chen/util` 封装后再用，不要直接在业务里引第三方头
- 通用能力：放 `chen/<module>/`，补 README + 测试，再让业务调用

## 下游 SDK 注意事项

- 头文件 `<chen/...>`，库 `lib/libchen.so`；模块 README 在 `include/chen/<module>/README.md`
- 三方库（yaml-cpp / tinyxml2 / jsoncpp / hiredis-vip / protobuf）已静态进 `libchen.so`，不要再链接
- 模块在 `onUnload` 里注销 `registerMethod` / servlet / EventBus，避免热重载后旧 `.so` 回调悬空
- 热重载会换 worker 进程，堆内状态会重置；跨重载数据落 Redis / MySQL，或在 `onDeactivate` flush
- 不要复制框架源码到业务，升级 SDK 会失配

## Review 清单

- [ ] 新增的类 / 工具，框架里是否已有等价实现？
- [ ] 日志、配置、指标是否走框架？
- [ ] 时间 / 文件 / 网络操作是否 fiber 安全？
- [ ] 是否引入了新的第三方依赖？
- [ ] 是否重复实现了 split / trim / time / json / 连接池 / LRU？
- [ ] 下游是否用了 `.as<bool>()`、裸 yaml、裸 hiredis？
- [ ] 新能力是否放在 `chen/<module>/` 并补了 README / 测试？

## 模块索引

> SDK 内把下表的 `chen/` 换成 `include/chen/`。

| 模块      | README                        | 典型入口                                                                |
|-----------|-------------------------------|-------------------------------------------------------------------------|
| 日志      | `chen/log/README.md`          | `LOG_NAME`、`INFO/ERROR`                                                |
| 配置      | `chen/config/README.md`       | `Config::Lookup`、`LexicalCast`                                         |
| 工具      | `chen/util/README.md`         | `StringUtil` / `TimeUtil` / `FSUtil` / `JsonUtil` / `EncryptorUtil`     |
| 协程      | `chen/fiber/README.md`        | `Fiber`                                                                 |
| 调度      | `chen/schedule/README.md`     | `Scheduler`                                                             |
| IO 调度   | `chen/iomanager/README.md`    | `IOManager::schedule/addTimer`                                          |
| 定时器    | `chen/timer/README.md`        | `TimerManager`                                                          |
| Hook      | `chen/hook/README.md`         | 阻塞调用 fiber 化                                                       |
| Socket    | `chen/socket/README.md`       | `Socket` / `Address`                                                    |
| TCP       | `chen/tcp/README.md`          | `TcpServer`                                                             |
| UDP       | `chen/udp/README.md`          | `UdpServer`                                                             |
| HTTP      | `chen/http/README.md`         | `HttpServer` / `WSServer`                                               |
| RPC       | `chen/rpc/README.md`          | `RpcServer` / `RpcClient` / Hub                                         |
| 游戏协议  | `chen/game/README.md`         | `GenericProtocolServer`                                                 |
| Rock 协议 | `chen/rock/README.md`         | `RockServer`                                                            |
| 数据库    | `chen/db/README.md`           | `RedisMgr` / `MySQLMgr` / `SQLite3Mgr` / `QueryBuilder` / `ShardRouter` |
| ORM       | `chen/orm/README.md`          | XML + Dao                                                               |
| 数据结构  | `chen/ds/README.md`           | `EventBus` / `EventDispatcher` / `LruCache`                             |
| 指标      | `chen/metrics/README.md`      | `RegistryMgr`                                                           |
| 限流      | `chen/rate_limiter/README.md` | `RateLimiter` / `ConnLimiter`                                           |
| 模块      | `chen/module/README.md`       | `Module` 生命周期                                                       |
| 文件监听  | `chen/watcher/README.md`      | `FileWatcher`                                                           |
| 二进制    | `chen/bytearray/README.md`    | `ByteArray`                                                             |
| 流        | `chen/streams/README.md`      | `Stream`                                                                |
| 线程      | `chen/thread/README.md`       | `Thread`（框架内部用，业务不要直接用）                                  |

## 正反例

### 例 1：时间 + 文件 + 字符串

```cpp
// ❌ 自己造
std::ifstream ifs(path);
std::string line;
while (std::getline(ifs, line)) {
    auto pos = line.find(',');
    // ...
}
auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

// ✅ 框架封装
std::string content = FSUtil::ReadFileToString(path);
auto rows = StringUtil::Split(content, '\n');
for (auto& row : rows) {
    auto cols = StringUtil::Split(row, ',');
}
uint64_t now_ms = TimeUtil::GetCurrentMs();
```

### 例 2：配置 + 日志

```cpp
// ❌ 自己读 yaml + printf
YAML::Node root = YAML::LoadFile("conf/a.yml");
int port = root["server"]["port"].as<int>();
printf("port=%d\n", port);

// ✅ 框架封装
static ConfigVar<int>::ptr g_port = Config::Lookup("server.port", 8080, "server port");
static Logger::ptr logger = LOG_NAME("system");
int port = g_port->getValue();
INFO(logger) << "port=" << port;
```

### 例 3：Redis

```cpp
// ❌ 自己 connect / 自己拼命令
redisContext* ctx = redisConnect("127.0.0.1", 6379);
redisReply* r = (redisReply*)redisCommand(ctx, "GET key");

// ✅ 框架封装
auto rds = RedisMgr::GetInstance()->get("blog");
auto reply = rds->cmd("get %s", "key");
// 或
RedisUtil::Cmd("blog", "set %s %s", "key", "value");
```

### 例 4：fiber 里等待

```cpp
// ❌ 卡住整个 IO 线程
std::this_thread::sleep_for(std::chrono::seconds(1));

// ✅ 定时器
IOManager::GetThis()->addTimer(1000, []() {
    doNext();
}, false);

// ✅ 或直接在 fiber 里 sleep(1)（hook 已接管）
```

### 例 5：SQL

```cpp
// ❌ 拼接
std::string sql = "SELECT * FROM user WHERE id=" + std::to_string(id);
conn->query(sql);

// ✅ QueryBuilder
auto qb = QueryBuilder::Create("user")->where("id", "=", id);
auto data = qb->executeQuery(conn);
```

### 例 6：事件

```cpp
// ❌ 自己维护 vector<callback>
// ✅
EventBusMgr::GetInstance()->on("user.created", [](auto& d) {
    // ...
});
EventBusMgr::GetInstance()->emit("user.created", (int64_t)uid);
```
