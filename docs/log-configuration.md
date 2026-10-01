# Log 日志配置指南

框架日志系统通过系统配置文件（`bin/conf/system.yml`）中的 `logs` 段进行配置，支持热重载。

## 配置结构

```yaml
logs:
  - name: <logger名称>
    level: <日志级别>
    formatter: <日志格式>        # 可选
    appenders:
      - type: <输出器类型>
        # ... 输出器相关配置
```

## 日志级别

从低到高：

| 级别 | 说明 |
|------|------|
| `debug` | 调试信息，开发阶段使用 |
| `info` | 一般信息，生产环境默认级别 |
| `warn` | 警告，不影响运行但需关注 |
| `error` | 错误，功能受损但系统仍在运行 |
| `fatal` | 致命错误，通常会导致退出 |

- 日志器级别：控制该 logger **是否输出**，低于设定级别的日志会被丢弃
- 输出器级别：控制该 appender **是否写入**，进一步过滤

```yaml
logs:
  - name: root
    level: info       # DEBUG 级别的日志不会进入任何 appender
    appenders:
      - type: FileLogAppender
        file: /path/to/root.log
        level: error  # 文件只写 ERROR 及以上
      - type: StdoutLogAppender
        # 不设 level，继承 logger 的 info 级别
```

## 输出器类型

### StdoutLogAppender — 控制台输出

输出到标准输出，支持按日志级别着色（DEBUG 蓝色、INFO 绿色、WARN 黄色、ERROR 红色、FATAL 洋红）。

```yaml
- type: StdoutLogAppender
  level: debug       # 可选，默认为 logger 的级别
  formatter: "..."   # 可选，自定义格式
```

### FileLogAppender — 文件输出

输出到文件，支持按时间切分、按大小分片和过期清理。

```yaml
- type: FileLogAppender
  file: /path/to/app.log          # 必填，日志文件路径
  level: info                     # 可选
  formatter: "..."                # 可选，自定义格式
  time_rotate: day                # 可选，按时间切分（默认 none）
  max_size: 104857600             # 可选，单文件最大字节数（默认 0 不限制）
  retention_days: 7               # 可选，保留天数（默认 0 不限制）
```

#### 时间切分类型（`time_rotate`）

| 值 | 说明 | 文件名示例 |
|----|------|-----------|
| `none` | 不切分（默认） | `app.log` |
| `30min` | 每 30 分钟 | `app_2024-11-02_14-30.log` |
| `hour` | 每小时 | `app_2024-11-02_14.log` |
| `12hour` | 每 12 小时 | `app_2024-11-02_12.log` |
| `day` | 每天 | `app_2024-11-02.log` |
| `week` | 每周（周一） | `app_2024-10-28.log` |
| `month` | 每月 | `app_2024-11.log` |

#### 日志保留天数（`retention_days`）

- **仅在 `time_rotate` 不为 `none` 时生效**
- 每次文件轮转（rotate）时自动清理超过指定天数的旧日志文件
- `0`（默认）表示不限制，永不清理

```yaml
# 示例：按天切分，保留最近 30 天的日志
- type: FileLogAppender
  file: /home/chen/logs/app.log
  time_rotate: day
  retention_days: 30
```

清理逻辑：
1. 文件轮转发生时触发（与 `time_rotate` 同步）
2. 扫描日志文件所在目录，匹配轮转文件的命名模式
3. 从文件名中解析日期（自动剥离分片序号后缀），删除早于 `当前时间 - retention_days` 的文件
4. **不会删除当前正在写入的日志文件**（即使它没有 `time_rotate` 后缀）

#### 按大小分片（`max_size`）

- `max_size` 为**单文件最大字节数**，`0`（默认）表示不限制
- 当前文件写入超过该大小时，下一次写入自动切换到新的分片文件
- 单条日志超过 `max_size` 不会被拆分，写入后下一条才触发分片
- 可与 `time_rotate` 并存：时间周期变更时新文件分片序号从 0 重新开始，同一周期内继续累加分片序号

分片文件命名（分片序号插入扩展名前）：

| 场景 | 文件命名 |
|------|---------|
| `time_rotate: day` + `max_size` | `app_2024-11-02.log` → `app_2024-11-02_1.log` → `app_2024-11-02_2.log` |
| `time_rotate: none` + `max_size` | `app.log` → `app_1.log` → `app_2.log` |

```yaml
# 示例：单个文件 100MB，超过即分片
- type: FileLogAppender
  file: /home/chen/logs/app.log
  max_size: 104857600   # 100MB
```

> 注意：`max_size` 分片文件不会按 `retention_days` 清理（清理依赖文件名中的日期）；如需自动清理，建议配合 `time_rotate` 使用。

## 日志格式（`formatter`）

使用 `%` 转义符定义输出格式：

| 格式符 | 说明 | 示例输出 |
|--------|------|----------|
| `%d{...}` | 日期时间，`{...}` 内为 strftime 格式 | `2024-11-02 14:30:05.123456` |
| `%t` | 线程 ID | `12345` |
| `%N` | 线程名称 | `io_0` |
| `%F` | 协程 ID | `1` |
| `%p` | 日志级别 | `INFO` |
| `%c` | Logger 名称 | `root` |
| `%f` | 源文件名 | `log.cc` |
| `%l` | 行号 | `482` |
| `%m` | 日志消息体 | `server started` |
| `%n` | 换行符 | |
| `%T` | Tab 字符 | |
| `%r` | 进程启动到当前的秒数 | `1234` |

特殊格式：
- `%f` 在 `%d{...}` 中表示**微秒**（6 位补齐），与 strftime 的 `%f` 不同

默认格式：
```
%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n
```

输出示例：
```
2024-11-02 14:30:05.123456	12345	io_0	1	[INFO]	[root]	server.cc:42	server started
```

## 完整配置示例

```yaml
logs:
  # 根日志器 — 汇总所有模块的日志
  - name: root
    level: info
    formatter: "%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    appenders:
      - type: FileLogAppender
        file: /home/chen/logs/root.log
        time_rotate: day
        retention_days: 30     # 保留 30 天
      - type: StdoutLogAppender

  # 系统日志 — 框架内部状态、配置变更等
  - name: system
    level: debug
    appenders:
      - type: FileLogAppender
        file: /home/chen/logs/system.log
        time_rotate: day
        retention_days: 7
      - type: StdoutLogAppender

  # 访问日志 — HTTP 请求记录
  - name: access
    level: info
    appenders:
      - type: FileLogAppender
        file: /home/chen/logs/access.log
        time_rotate: hour        # 每小时切分
        max_size: 104857600      # 单文件超过 100MB 再分片
        retention_days: 3        # 只保留 3 天
      # 访问日志通常量大，不输出到控制台

  # HTTP 网关日志
  - name: http_gateway
    level: info
    appenders:
      - type: FileLogAppender
        file: /home/chen/logs/http_gateway.log
        time_rotate: day
        retention_days: 14
      - type: StdoutLogAppender
```

## 代码中使用

```cpp
// 使用根 logger（最常用）
INFO(LOG_ROOT()) << "server started on port " << port;
ERROR(LOG_ROOT()) << "failed to connect: " << err;

// 使用命名 logger（与配置中的 name 对应）
DEBUG(LOG_NAME("system")) << "config reloaded";
INFO(LOG_NAME("access")) << "GET /api/v1/users 200 5ms";
WARN(LOG_NAME("http_gateway")) << "upstream timeout";

// 格式化日志（C++20 std::format 风格）
INFO(LOG_ROOT()) << std::format("{} connections active", count);
```

## 热重载

日志配置支持热重载——修改 `system.yml` 中 `logs` 段后发送 `SIGUSR2` 信号（或调用配置 reload），新配置即时生效：

- 新增 logger → 自动创建
- 修改 level / formatter → 即时更新
- 修改 appender → 重建输出器（旧文件正常关闭）
- 删除 logger → level 置为最大值（停止输出）
