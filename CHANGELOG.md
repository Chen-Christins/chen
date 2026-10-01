# Changelog

All notable changes to this project will be documented in this file.

## v1.5.2 (2026-10-01)

### 许可证

- **变更** 项目许可证由 Apache 2.0 切换为 GNU GPL v3.0（强 copyleft），更新 `LICENSE`、`README.md`、文件头模板及全部源文件 `@copyright` 声明；第三方代码（Mongrel2 / BSD-3、CRoaring / Apache-2.0）保留其原始版权声明

### RPC — CmdID 与方法名接口合并

- **变更** 调用侧删除 13 个 `*ByCmd*` 接口（`callByCmd(WithTimeout)` / `callRoutedByCmd(WithTimeout)` / `callByCmdAsync(WithTimeout)` / `callRoutedByCmdAsync(WithTimeout)` / `callByCmdFuture(WithTimeout)` / `callRoutedByCmdFuture(WithTimeout)` / `notifyRoutedByCmd`），由现有 `call` / `callRouted` / `callAsync` / `callRoutedAsync` / `callFuture` / `callRoutedFuture` / `notifyRouted` 的 `uint32_t` 重载接替，与 `registerMethod` 的双重重载对称；`cmd` 必须非 0，否则抛 `std::invalid_argument`
- **重构** 组帧去重：新增私有 `buildNameRequest()` / `buildCmdRequest()`，各同步/异步/Future/Notify 接口的请求构造统一收敛
- **重构** `RpcClient` / `RpcServer` 的方法名与 CmdID 两张 handler 表合并为一张 `HandlerMap`（`chen/rpc/handler_map.h`，key 为 `std::variant<uint32_t, std::string>`，方法名 `"16"` 与 cmd `16` 不冲突），分派统一为一次 `find`；`RpcServer` 热重载的 `m_pendingCmdHandlers` 随之移除，`prepareDispatch` / `commitDispatch` 只需处理单张表
- **变更** 协议不变：`cmd=0` 按 body 方法名分派、`cmd!=0` body 直接为参数，线上帧格式与旧行为完全兼容
- **变更** `tests/test_rpc_hub.cc` 调用点迁移，更新 `chen/rpc/README.md`

## v1.5.1 (2026-09-22)

### 配置热加载 — inotify 事件驱动（新增）

- **新增** `chen/watcher/` 模块：`FileWatcher` 基于 Linux inotify，通过 `IOManager` 的 epoll 接入协程事件循环，递归监听配置目录
- **新增** `Config::LoadFromFile(filepath, force)` 单文件加载，`LoadFromConfDir()` 改为复用它
- **优化** 配置热加载从「每 2s 定时轮询全目录」改为「文件变更事件驱动」：`IN_CLOSE_WRITE` / `IN_MOVED_TO` 后立即重载对应文件，无轮询开销、无延迟
- **优化** 文件 mtime 比较改用纳秒精度（`st_mtim.tv_nsec`），修复同一秒内多次修改漏检
- **移除** `server.config_reload_interval_ms` 配置项；`housekeeping_interval_ms` 定时器只负责信号检测
- **变更** SIGHUP / `-s reload` 只重载 `.so` 模块，配置改由 inotify 自动重载
- **修复** `FileWatcher::stop()` 先 `cancelEvent` 再 `close`，归还 pending event 计数，避免 IOManager 停止阻塞
- **新增** `tests/test_file_watcher.cc`，覆盖原地修改 / 原子替换（rename）/ 递归子目录 / 非配置文件忽略 / 删除
- **文档** 新增 `chen/watcher/README.md` 使用指南，更新根 `README.md`、`chen/config/README.md`

### RPC — Future 异步调用

- **新增** `chen/rpc/rpc_future.h`：`RpcFuture<R>` 句柄，提供 `get()`（返回 `RpcResult<R>`，不抛异常）/ `getValue()`（失败抛异常）/ `ready()`（非阻塞查询）
- **新增** 8 个 future 接口，与回调式异步一一对应：`callFuture(WithTimeout)` / `callRoutedFuture(WithTimeout)` / `callByCmdFuture(WithTimeout)` / `callRoutedByCmdFuture(WithTimeout)`
- **优化** eager 发起、`get()` 挂起当前 fiber 等待结果（不阻塞线程），支持并发发起多个请求后按序取结果
- **文档** 更新 `chen/rpc/README.md`；`test_rpc_client` / `test_rpc_hub` 补充 future 用例

### Application

- **修复** 热重启时的无效模块加载：`-s reload/stop/quit` 与 `--help` 在 `ModuleMgr::init()` 前提前返回，执行 `-s` 命令不再加载 `.so` 模块

## v1.5.0 (2026-09-17)

### 模块热重载 / 优雅关闭 — 重构

- **变更** 模块生命周期 API：`onDrain()` → `onDeactivate()`，移除 `onGracefulUnload()`，新增 `onActivate()`（默认委托 `onServerReady()`）
- **重构** 热重载改为双缓冲 dispatch 原子切换：`TcpServer::prepareDispatch()` / `commitDispatch()` 取代 `clearRegistrations()`
  - `HttpServer` / `WSServer`：新增 `m_pendingDispatch`，`getServletDispatch()` 优先返回 pending
  - `RpcServer`：新增 `m_pendingHandlers` / `m_pendingCmdHandlers`
  - `GenericProtocolServer`：新增 `pendingHandlers_`
- **优化** 热重载流程：dlopen 新模块 → `onServerReady()` → `commitDispatch()` 原子切流量（无 404 窗口）→ 仅新模块 `onServerUp()` → 旧模块 `onDeactivate()` → `dlclose`，旧 `.so` 真正卸载（不再永久保留）
- **优化** 优雅关闭：先 `closeAllClients()` 关闭存量连接再 drain，随后取消 tick → `onDeactivate()` → `onUnload()`；进程退出从 `std::_Exit(0)` 改回 `std::exit(0)`
- **移除** `Socket::m_iom` 及 `setIOManager()` / `getIOManager()`，`cancelRead` / `cancelWrite` / `cancelAccept` / `cancelAll` 统一使用 `IOManager::GetThis()`
- **移除** `IOManager::cancelAllNoTrigger()`、`FdContext::last_fd_ctx`，hook `close()` 中不再调用 `FdCtx::setClose`
- **优化** `module.path` 提取为静态 `ConfigVar` 缓存，热重载时不再重复 `Lookup`
- **新增** EventBus 安全规则：listener 必须在 `.so` dlclose 前清理，统一在 `onServerReady()` 开头调用 `clearAll()`
- **新增** `tests/module_test/lifecycle_module` 生命周期测试模块，验证 `onActivate` / `onDeactivate` / `dlclose` 流程
- **文档** 更新 `docs/module-lifecycle.md`、`docs/module-development.md`、`chen/module/README.md`

### RPC — 异步调用

- **新增** `RpcStatus` 枚举（`OK` / `TIMEOUT` / `SEND_FAILED` / `CONNECTION_CLOSED` / `DECODE_ERROR`），仅表达本地传输层结果
- **新增** `RpcResult<R>` 与 `RpcResult<void>` 特化：`status`（传输层）、`code`（服务端业务码，0=成功）、`error`、`value` 及 `ok()`
- **新增** `RpcCallback<R>` 回调类型
- **新增** 8 个异步接口：`callAsync(WithTimeout)` / `callRoutedAsync(WithTimeout)` / `callByCmdAsync(WithTimeout)` / `callRoutedByCmdAsync(WithTimeout)`
- **优化** 异步回调被调度为独立协程执行，发起后立即返回，不阻塞调用方；异步路径不抛异常，错误统一经 `RpcResult` 表达
- **重构** `ResponseContext` 增加 `callback` / `timer` 字段；新增 `takePending()` / `completeRequest()` 统一超时、响应、断开三路完成，保证只完成一次
- **优化** 异步超时定时器在请求完成时取消，避免定时器堆积；新增 `MAX_PENDING_REQUESTS` 上限保护
- **修复** `recvLoop` 断开时持锁完成回调导致的死锁，改为锁外完成
- **新增** `test_rpc_client` / `test_rpc_hub` 异步调用用例

### 工具

- **新增** `EncryptorUtil::HMAC_SHA1`（原始二进制签名，提供 `std::string` 与 `void*` 两个重载）
- **新增** `StringUtil::Base32Encode` / `Base32Decode`（RFC 4648，大写字母 + `2-7` + `=` padding）

## v1.4.1 (2026-09-08)

### RPC 中心转发（Hub）— 新增模块

- **新增** `RpcHubRegistry` 服务注册表：支持 `peer_id`/`func_id`/`bind_id` 多维索引，连接断开自动注销
- **新增** `RpcServer` relay 模式：服务连接后通过 `@register` 注册，带路由字段的请求由 hub 转发到目标并中继回包
- **新增** 路由方式：`DIRECT`（按 peer_id）、`GROUPID`（按 func_id + group_id，支持固定/动态拓扑）、`BROADCAST`（全量 fan-out）、`BIND_ID`（按 func_id + bind_id）
- **新增** CmdID 模式 RPC 调用：`callByCmd`/`callByCmdWithTimeout`、`callRoutedByCmd`/`callRoutedByCmdWithTimeout`、`notifyRoutedByCmd`，body 直接为参数，无方法名字符串开销
- **新增** `RpcClient::registerMethod(name/func, cmd/func)` — 客户端侧本地方法注册，hub 转发 REQUEST 时按方法名/CmdID 分派
- **新增** `FunctionTraits` 提取为 `chen/rpc/function_traits.h`，支持 lambda 和成员函数
- **新增** `chen/util/endian.h` 大端序读写工具函数（`ReadBigEndianUint32`/`WriteBigEndianUint8` 等）
- **优化** `RpcConnection` 协议编解码：头部改用栈 buffer 定长解析，小包（≤4KB）header+body 一次 write，减少 `ByteArray` 分配
- **新增** `test_rpc_hub.cc` 单元测试

### SSL / HTTPS 修复

- **修复** `SSLSocket::init()` SSL_accept 阻塞问题：同步调用在非阻塞 socket 上无法完成握手 → 改为 fiber 友好式循环（`addEvent` + `YieldToHold` + `delEvent`），正确处理 `SSL_ERROR_WANT_READ`/`SSL_ERROR_WANT_WRITE`
- **新增** SSL 握手超时（30 秒）：`addConditionTimer` + `weak_ptr` 模式，防止 fiber 永久挂起
- **新增** `SSLSocket::recv()` MSG_PEEK 支持：`SSL_peek()` 实现不消费数据的预览读取，修复 `detectH2Preface` 导致 HTTP 请求体被截断的问题
- **修复** `loadCertificates()` 使用 `SSLv23_server_method()` 替代 `SSLv23_client_method()`，返回值判断从 `!= -1` 改为 `!= 1`（与 OpenSSL 文档一致）
- **优化** SSL 错误日志级别：`SSL_accept` 失败从 DEBUG 提升为 WARN，`SSL_ERROR_ZERO_RETURN` 使用 DEBUG
- **优化** `SSLSocket::accept()` 错误处理：EBADF（停服预期）降级为 DEBUG，EAGAIN 静默

### TcpServer

- **修复** `startAccept()` accept 返回 errno=0 时仍打 ERROR 日志：TCP accept 成功但 SSL init 失败时 errno 为 0，跳过避免误导性 "errno=0 errstr=Success" 日志
- **优化** `startAccept()` 静默 EAGAIN/EWOULDBLOCK，非阻塞 socket 上无连接是正常行为
- **优化** `TcpServer::toString()` 状态日志格式：新增 `io=` worker 显示、`ssl=` 标志、`listen=[addr:port,...]` 监听地址列表；空名/无 worker 显示 `-` 而非空白
- **新增** `WorkerName()` 辅助函数统一 IOManager 显示名格式

### 其他

- **修复** `Socket::accept()` EAGAIN 静默，非阻塞 socket 上无待处理连接时不再报 ERROR

## v1.4.0 (2026-09-01)

### 限流器 — 新增模块

- **新增** `chen/rate_limiter/` 模块：`RateLimiter` 纯令牌桶算法（`qps` 速率补充、`burst` 突发上限、`qps <= 0` 恒放行）
- **新增** `ConnLimiter` 连接准入控制器，组合令牌桶（速率）与并发计数（容量），提供 `tryAcquire`/`release` 及 `current`/`getRejectedCount` 统计
- **新增** `ConnLimiter::CreateFromArgs(args)` 从 `TcpServerConf::args` 解析 `accept_qps`/`accept_burst`/`max_conn`，未配置任何参数时返回 `nullptr`（不启用）
- **集成** `TcpServer`：`setConf` 时构建限流器，`startAccept` 中 accept 后准入校验，超限直接关闭连接并计数；连接关闭时 `release()` 归还名额
- **新增** `Application::getAllServers()` 获取所有类型 Server；`Module::statusString()` 返回各服务器状态摘要
- **新增** `chen/rate_limiter/README.md` 设计文档、`test_rate_limiter.cc` 单元测试

### 配置系统 — XML 支持

- **新增** XML 配置文件支持（tinyxml2）：`Config::LoadFromXml()` 将 XML 元素树转换为 YAML 节点后复用同一套加载流程，`LexicalCast` 与自定义类型无需改动
- **扩展** `LoadFromConfDir()` 递归加载目录下所有 `.yml` 与 `.xml`
- **映射规则**：无子元素节点 → 标量；同名子元素重复（≥2）或标签为保留标记 `item`/`entry`/`value` → 序列；否则 → map（标签名作 key，属性忽略）
- **新增** `test_config_xml.cc` 单元测试，覆盖标量/序列/map/嵌套 map/对象序列/单元素列表

### HTTP — 大文件上传 / 流式 body

- **新增** 流式请求体支持：`HttpSession::recvRequestHeader()`（只收 header，多读的 body 字节存入 `leftover`）、`readBodyInto()`（全量读入）、`readBodyStreaming()`（分块回调）、`drainBody()`（提前返回时排空兜底）、`sendResponseHeader()`（先发头再分块写 body）
- **新增** `Servlet::isStreamingBody()` 虚方法；`FunctionServlet`/`MethodServlet` 支持 `streamingBody` 标记，`addServlet`/`addHandler`/`setDefaultHandler` 增加相应参数
- **新增** `ServletDispatch::matchForRequest()` — 匹配请求对应 servlet 并抽取 path 参数，但不立即调用 handle
- **修复** `common.cmake` 中 `TEST` 标志仅在 Debug 构建启用，避免 Release 构建误编译测试

### 构建 / CI

- **新增** arm64 架构 SDK 包（`ubuntu-24.04-arm`），发布产物按架构区分 `chen-sdk-<version>-<arch>.zip`
- **新增** 独立 `release` job：打 tag 时聚合多架构产物创建 draft release
- **修复** `package_sdk.sh` 复制可执行文件名 `main` → `server`

## v1.3.6 (2026-08-25)

### ORM / QueryBuilder 增强

- **新增** `Dao::newQuery(alias)` 生成预配置表名的 `QueryBuilder`，支持表别名用于 JOIN 多表查询，无需硬编码表名
- **新增** `QueryBuilder::queryColumn<T>` 单列查询（`int32_t`/`int64_t`/`double`/`std::string`），`queryPairs<K,V>` 两列聚合查询，`queryScalarInt64/Double` 标量查询（`COUNT`/`SUM`/`AVG` 等）
- **新增** `IDB::findColumn(name)` 按列名查找列索引，及按列名取值重载 `getInt32/getInt64/getString/getTime(col)`
- **优化** `QueryByBuilder`/`QueryByBuilderPages` 支持自定义 `select()`：用户显式设置 SELECT（如 `prs.*`）时保留，否则自动覆盖为表全列，JOIN 场景映射正确

### 工具宏增强

- **重命名** `LICKLY/UNLICKLY` → `LIKELY/UNLIKELY`（修正拼写）
- **重命名** `ASSERT2` → `ASSERT_MSG`，新增 `ASSERT_RET`（断言失败直接 return）
- **新增** 终端颜色宏 `COLOR_RED`/`COLOR_GREEN`/`COLOR_BOLD_*`/`COLOR_BG_*` 等 ANSI 转义序列

### XML 转换器增强

- **新增** `xml_converter.hpp` 头文件，将 `xml_converter.cc` 拆分，增强 converter 的类型处理与错误信息输出

### 代码风格

- 全量代码格式化（clang-format），调整 `.claude/skills/cpp-code-style` 规范文档

## v1.3.5 (2026-07-13)

### UdpServer — 新增独立 UDP 服务器模块

- **新增** `chen/udp/` 模块，UDP 服务器与 `TcpServer` 独立平行，不继承关系（`UdpServer`、`UdpServerConf`、`UdpServerFactory`）
- `UdpServer` 基于 `recvfrom` 数据报循环，通过框架 hook 机制实现 fiber 友好的非阻塞 IO
- 子类重写 `handleRecv` 实现自定义协议，完成 request-response 或单向接收
- 配套 `chen/udp/README.md` 设计文档

### 分库分表路由

- **新增** `chen/db/shard.h/cc` — 分库分表路由框架
- `ShardKey` 分片键：支持 Int64 / String / Time 三种类型
- `IShardStrategy` 可插拔策略接口，内置 `HashShardStrategy`（取模）和 `DateShardStrategy`（日期）实现
- `ShardRouter` 统一路由入口，注册策略 + 数据库连接后按逻辑表名做分片路由

### 数据结构 — Dispatcher / EventBus

- **新增** `chen/ds/dispatcher.h/cc` — 通用分发器，支持消息类型注册和协程调度
- **新增** `chen/ds/event_bus.h/cc` — 事件总线，基于 Dispatcher 实现发布/订阅模式
- **新增** `chen/ds/README.md` 设计文档

### RPC 死锁修复

- **修复** RPC 客户端 `recvLoop` 断开连接时持有锁调用 `close()` 导致死锁。改为先释放锁再 `close()`

### 日志系统 — 新增 TRACE 级别

- **新增** `TRACE` 日志级别（比 DEBUG 更细粒度），`LogLevel` 枚举值从 `DEBUG=1` 改为 `TRACE=1/DEBUG=2/.../FATAL=6`
- **新增** `Logger::trace()` 方法
- **变更** 默认日志级别从 `DEBUG` 改为 `TRACE`
- **优化** `Config::Lookup` 已存在变量时从 INFO 降为 TRACE，消除高频无意义日志
- **优化** `Scheduler::run` idle fiber term 日志从 INFO 降为 TRACE

### 工具模块增强

- **新增** `EncryptorUtil` 加密工具：`AESEncrypt/AESDecrypt`、`RSAPrivateEncrypt/RSAPublicDecrypt`、`BASE64Encode/BASE64Decode`、`HexEncode/HexDecode`、`CRC32` 等
- **新增** `StringUtil`：`Split`、`Replace`、`Trim`、`UrlEncode/UrlDecode`、`ToUpper/ToLower`、`StartsWith/EndsWith` 等
- **新增** `TimeUtil`：`Time2Str`、`Str2Time`、`IsLeapYear`、`DaysInMonth`、`BeginOfDay/Month/Year`、`EndOfDay/Month/Year`、`TimeUtil::Age` 等

### ORM

- **优化** ORM 表生成：新增 `getListSQLColumns` 接口，`magic_enum` 字段映射增强
- **新增** `QueryBuilder`：`selectForUpdate`、`insertOrUpdate`（INSERT ... ON DUPLICATE KEY UPDATE）、`rawCondition`、子查询支持

### 其他

- **修复** CI 打包脚本 `package-sdk-ubuntu.yml` 路径问题
- **新增** `sync_sdk.sh` SDK 同步脚本
- **新增** 单元测试：`test_encryptor_util.cc`、`test_string_util.cc`、`test_time_util.cc`

## v1.3.4 (2026-06-28)

### RPC 框架重构

- **修复** RPC 服务端静默丢包：方法不存在或 body 反序列化失败时 `continue`，客户端挂到超时 → 改为返回 `code=400/404` 错误响应
- **修复** `m_seq` 线程安全：多协程并发 `call()` 时 `++m_seq` 不在锁保护下，可能导致 sequence 重复、响应互相覆盖 → 改为 `std::atomic<uint32_t>`
- **优化** 消除 `req->body` 重复 parse：handleClient 先反序列化拿到 method，handler lambda 内又解析一次 → handler 签名改为 `(ByteArray::ptr, uint32_t)`，一次 parse 搞定
- **移除** body 中的冗余 `sequence` 字段（Protocol 头部已有），精简 protobuf 定义
- **移除** protobuf RpcRequest/RpcResponse 中间层，body 直接编码为 `[method_vint][args_binary]`，省掉 protobuf 序列化/反序列化开销
- **新增** `MessageType` 枚举替代 `uint8_t type = 0/1/2`魔数
- **新增** `ProtocolHeader` packed 结构体，`BASE_LENGTH` 由 `sizeof(ProtocolHeader)` 自动推导
- **新增** `RpcClient::close()` 中调用 `m_sock->cancelAll()` 确保 recvLoop fiber 被唤醒，修复 close 后程序无法退出的问题
- **新增** 心跳丢失检测：连续 3 次无回包自动断开
- **新增** `FunctionTraits` 支持 lambda 和成员函数
- **新增** `Serializer::EncodeRequest/EncodeResponse/DecodeMethod/DeserializeResult` 等直接 body 编解码 API
- **新增** 序列化防护：`Serialization<T>` 默认 `write/read` 加 `static_assert`，未特化时编译报错
- **新增** `connect()` 前置 `close()` 防止重复调用泄漏旧连接
- **修复** `Protocol::Decode` 未被使用 → 改为 `recvProtocol` 直接构造，`Decode` 留作对称工具

### WebSocket 修复

- **修复** `WSSendMessage` 原地 XOR 数据：客户端发送时直接在调用者的 `WSFrameMessage` 上 mask，重发/广播会损坏数据 → 拷贝一份再 mask
- **修复** `rand()` 非线程安全 → 改为 `RandomUtil::RandUint(0, UINT32_MAX)`（thread_local mt19937_64）
- **修复** `WSServer::handleClient` 异常安全：`servlet->handle()` 抛异常时 `onClose()` 不执行 → 加 `try {} catch {}` 确保清理
- **修复** CLOSE 帧未处理：收到 opcode=8 时走到 `else` 分支继续读下一帧 → 读取 close body → 回 CLOSE 确认帧 → 关闭连接
- **修复** `Sec-WebSocket-Accept` 计算错误：`EncryptorUtil::SHA1` 返回 hex 字符串，`Base64Encode` 对 hex 做 base64，浏览器验证失败立即断开 → 加 `HexDecode` 还原为原始 20 字节再 base64
- **修复** `handleShake` 日志级别：验证失败路径使用 `INFO` → 改为 `WARN`

### 文件结构调整

- `chen/env.h/cc` → `chen/util/env.h/cc`（参数解析工具）
- `chen/worker.h/cc` → `chen/iomanager/worker.h/cc`（WorkerGroup 与 IOManager 同层）
- `chen/module.h/cc`、`chen/library.h/cc` → `chen/module/module.h/cc`、`chen/module/library.h/cc`
- `chen/mutex.h/cc` → `chen/util/mutex.h/cc`
- `chen/singleton.h` → `chen/util/singleton.h`

### 代码规范

- **全部 76 个头文件的 `#ifndef` 宏守卫替换为 `#pragma once`**
- **22 个缺少 `@file` 注释的头文件补全文件头注释**
- **标准化 include 结构**：全部 `.h/.cc` 文件按「C++ 标准库 → POSIX 系统头 → 第三方库 → 项目内头文件」四级分组排序
- **修复** `uri.rl:80` 中 `std::string(mark, fpc - mark)` 的 `-Wnonnull` 警告

### 文档

- 新增 `chen/README.md` 顶层模块索引，概述所有 24 个子模块
- 新增 `module/README.md`、`streams/README.md`、`util/README.md` 模块文档
- 重写 `rpc/README.md`（去掉已废弃的 protobuf 描述，补充完整使用示例）
- 更新 `bytearray/README.md`、`fiber/README.md`、`schedule/README.md`、`http/README.md`、`iomanager/README.md`、`socket/README.md`、`timer/README.md` — 补充代码示例
- 更新 `thread/README.md`、`fiber/README.md`、`hook/README.md`、`schedule/README.md`、`timer/README.md` — 增加使用引导警告
- `package_sdk.sh` — 打包时包含各模块 README.md

## v1.3.3 (2026-06-15)

### 优雅关闭修复

- **修复** `close()` hook 在错误 IOManager 上取消事件：WebSocket socket 的
  IO 事件注册在 io worker，但 `close()` 在 main 线程调用，`IOManager::GetThis()`
  返回 main IOManager → `cancelAll` 无操作 → io worker 的 `m_penddingEventCount`
  永远 > 0 → idle 循环卡死
- **新增** `FdCtx` 记录 fd 所属的 `IOManager*`，`close()` hook 优先使用
  `ctx->getIOManager()` 而非 `GetThis()`，确保从任意线程 close 都能找到正确
  的 IOManager
- **新增** `IOManager::cancelAllNoTrigger(fd)` — 清理 epoll 事件和计数器，
  不触发回调。`Socket::close()` 调用此方法，避免热重载时触发旧模块代码回调
  导致 heap-use-after-free
- **新增** `IOManager::idle()` 事件处理循环在 `epoll_ctl` 返回 `EBADF`/`ENOENT`
  时自动清理残留 `FdContext` 并递减 `m_penddingEventCount`，作为安全兜底
- **新增** `TcpServer` 连接追踪机制（`m_activeClients` + `closeAllClients()`），
  类似 nginx 的 `ngx_connection_t` 遍历。`startAccept` 自动注册/注销客户端连接
- **新增** `WorkerMgr::stop()` 调用 `cancelAllTimers()` 清理 `do_io` 条件超时器等
  框架内部残留定时器
- **修复** `Application::main()` 中 IOManager 和 `m_servers` 析构顺序导致的
  进程退出 crash
- **新增** `Fiber::CleanupThread()` — 工作线程退出前释放 `t_fiber` 和
  `t_threadFiber` thread_local 引用，修复 fiber 内存泄露

### 零停机热重载 — nginx 蓝绿部署模式

- **重构** `doHotReload()`: 新模块先加载 → 注册 handler 接手流量 → 旧模块排空
  连接 → 销毁。消除旧版 stop-the-world 中的请求 404 窗口和 WS 强制断开
- **新增** `Module::onDrain()` — 热重载排空阶段，关闭 WS、停定时器
- **新增** `Module::onGracefulUnload()` — 热重载完后释放非 dispatch 资源，
  默认委托 `onUnload()`
- **新增** `TcpServer::clearRegistrations()` 虚方法 — `HttpServer`/`WSServer` 清
  `ServletDispatch`，`RpcServer` 清 RPC handler，`GenericProtocolServer` 清
  Game handler。确保 dlclose 后无残留 `shared_ptr` 指向已卸载代码
- **重构** `ModuleMgr::reloadModule()` 为 load-only 语义（返回 `Module::ptr`，
  通过 `old_out` 输出旧模块），支持先加载后排空。错误路径自动恢复旧模块
- **修复** 热重载后 tick 定时器失效 — `doHotReload()` 在 `onServerUp` 后重新注册
  所有模块的 tick
- **优化** 无模块变更时跳过 dispatch 清空和 `onServerReady`，避免不必要的
  MySQL 重连和 servlet 重注册

### 新增 API

- `Module::onDrain()` — 热重载排空，关 WS / 停定时器
- `Module::onGracefulUnload()` — 热重载释放非 dispatch 资源
- `TcpServer::clearRegistrations()` — 清空 handler/servlet 注册
- `TcpServer::closeAllClients()` — 关闭所有追踪的客户端连接
- `ServletDispatch::clearServlets()` — 清空 servlet 但保留 NotFoundServlet
- `IOManager::cancelAllNoTrigger(fd)` — 清理 epoll 不触发回调
- `TimerManager::cancelAllTimers()` — 清空定时器集合（内部方法）

### 变更

- `ModuleMgr::reloadModule()` 返回类型 `bool` → `Module::ptr`，增加 `old_out` 参数
- 移除 `IOManager::cancelAllPendingEvents()` — 被连接追踪 + `closeAllClients` 替代
- `Socket` 增加 `m_iom` 字段，由 `TcpServer::startAccept` 设置
- `Socket::cancelRead/cancelWrite/cancelAll` 优先使用 `FdCtx` 记录的 IOManager
- `Application` 成员声明顺序调整，确保析构顺序正确

### 文档

- 新增 `docs/module-lifecycle.md` — 热重载和优雅关闭时序文档
- 新增 `docs/module-development.md` — Module 开发指南（方法继承决策）

## v1.3.2 (2026-06-15)

### fd 复用崩溃修复
- `Socket::close()` 调用 `FdMgr::del()` 清理 fd 上下文，防止 fd 复用时 accept 拿到旧 FdCtx
- `FdContext` 新增 `last_fd_ctx` 字段追踪关联的 FdCtx 指针
- `addEvent()` 冲突时对比 FdCtx 地址识别 fd 复用，自动重置残留的 FdContext 并修正 `m_penddingEventCount`
- `hook close()` 在 `t_hook_enable=false` 时也做 FdMgr/IOManager 清理
- `cancelEvent`/`delEvent`/`cancelAll` 对 `ENOENT`/`EBADF` 容错：fd 已不在 epoll 时清理残留内存状态而非报错返回，消除热重载后 WS 重连 ASSERT 崩溃和定时器回调反复报错

## v1.3.1 (2026-06-14)

### 热重载修复
- Tick 定时器改用 `weak_ptr<Module>`，修复热重载时定时器持有旧模块 `shared_ptr` 导致 `refcount=2` WARN，模块无法及时释放
- ASan 注入 TLS 导致 glibc 标记 `DF_1_NODELETE`，`dlclose` 返回 0 但实际不卸载。改为复制 `.so` 到临时唯一路径再 `dlopen`，彻底解决 ASan 下热重载不生效问题
- `ServletDispatch` 新增 `clear()` 方法，清空所有注册的 HTTP/WS servlet，`onUnload` 中可复用释放引用

### 数据库 / Redis 清理
- `FoxRedis::~FoxRedis()` 调用 `redisAsyncFree` 释放 `redisAsyncContext`，修复进程退出时 hiredis 内存泄漏
- `RedisManager` 新增 `freeAll()` 方法，遍历释放所有 Redis 连接
- `FoxThreadManager::stop()` 末尾 `m_threads.clear()`，触发 `~FoxThread()` 释放 pipe、event、event_base
- 优雅关闭 Phase 2 在 `FoxThreadMgr::stop()` 之前调用 `RedisMgr::freeAll()`，确保 `redisAsyncFree` 在 libevent 事件循环存活时安全执行

## v1.3.0 (2026-06-14)

### 模块热重载
- 热重载时调用 `onServerReady`/`onServerUp`，确保模块重新注册 RPC 方法和协议 handler
- 修复 `dlopen` 缓存问题：旧模块先 `dlclose` 再 `dlopen` 新模块，确保加载最新代码
- 旧模块 `onUnload` 前注销 RPC 方法，避免旧 `.so` 卸载后 lambda 内部函数指针悬垂导致 SEGV
- `RpcServer` 新增 `unregisterMethod` 方法

### 优雅关闭
- 重构为异步两阶段：Phase 1 停止 accept → drain 排空 → Phase 2 停 tick/卸载模块/停 Worker
- 移除 `usleep()` 阻塞调用，改用 IOManager timer 驱动，避免阻塞主调度线程
- `accept()` 返回 EBADF 在停服时降级为 DEBUG 日志（预期行为）
- 关闭时 skip `dlclose`，仅调 `onUnload` 做业务清理，避免 TLS 析构函数引用已卸载 `.so`
- 停 tick IOManager 优先于卸载模块，防止残留回调引用已卸载代码

### Fiber / Scheduler 稳定性
- `Fiber::~Fiber()` 析构时清空 `t_fiber`，避免日志框架通过 `GetFiberId()` 访问已释放协程
- `Scheduler::run()` while 循环退出后重置 `t_fiber` 为线程主协程，防止局部 idle fiber 析构时悬垂指针

### 构建与工具
- CMake 新增 `ENABLE_ASAN` 选项，支持 AddressSanitizer 内存错误检测
- 所有核心模块补充 README 文档
- 添加 Apache 2.0 LICENSE 文件

## v1.2.1 (2026-05-31)

- 新增 C++ 代码风格指南，统一项目编码规范 (#8)
- 实现 Rock 协议服务端，支持负载均衡与相关工具类 (#9)
- 重构数据库迁移逻辑，调整 include 顺序并改进 column 处理 (#10)
- 新增 FdCtx::setClose 方法，增强 hook 逻辑中的关闭处理 (#11)
- 新增加密哈希（MD5/SHA1/SHA256/SHA512）与 HMAC 函数 (#12)
- 新增 lexical_cast 对 bool 和 string 类型的特化转换支持 (#13)

## v1.2.0 (2026-05-24)

- 新增 HTTP/2 协议支持，包含连接管理与路由功能 (#5)
- 新增 SSE 服务端组件，完善服务端推送能力 (#4)
- 新增 Table 类的 SQLite3 和 MySQL 数据库迁移功能
- CMake 构建配置增加 MySQL、Redis 和 nghttp2 依赖项 (#6)
- 重构 SDK 打包流水线，修复三方依赖处理逻辑 (#7)

## v1.0.0 (2026-05-23)

- 首个 SDK 正式发布，支持 HTTP/2、SSE、WebSocket、RPC 协议
- 支持 MySQL、Redis、SQLite3 数据库
- 基于协程的异步 I/O 框架
