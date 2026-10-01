# Log — 日志系统

高性能异步日志系统，支持分级、分模块、热重载配置。

> 完整配置指南详见 [docs/log-configuration.md](../../docs/log-configuration.md)

## 日志级别

```
TRACE < DEBUG < INFO < WARN < ERROR < FATAL
```

## 宏接口

```cpp
TRACE(logger) << "trace message";
DEBUG(logger) << "debug message";
INFO(logger) << "info message";
WARN(logger) << "warning";
ERROR(logger) << "error";
FATAL(logger) << "fatal";

LOG_NAME("module_name")   // 创建模块级 logger
```

## 核心类

- **Logger** — 日志记录器，持有 Appender 列表和日志级别
- **LogAppender** — 输出目标（文件、控制台）
- **LogFormatter** — 自定义日志格式
- **LogEvent** — 单条日志事件（时间、线程、协程、文件、行号、内容）
- **LogDispatch** — 日志分发入口

## 日志格式

默认格式包含：时间戳、线程 ID、协程 ID、日志级别、模块名、文件名:行号、消息

## 配置说明

日志通过 `logs` 段配置，每个 logger 可挂多个 appender：

```yaml
logs:
  - name: root
    level: info
    formatter: "%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    appenders:
      - type: FileLogAppender
        file: /path/to/app.log
        level: info
        time_rotate: day          # 可选，按时间切分（none/30min/hour/12hour/day/week/month）
        max_size: 104857600       # 可选，单文件最大字节数，超过即分片（0 表示不限制）
        retention_days: 7         # 可选，保留天数（0 表示不限制）
      - type: StdoutLogAppender
        level: debug
```

### 输出器类型

| type | 说明 |
|------|------|
| `FileLogAppender` | 输出到文件，支持按时间切分、按大小分片、过期清理 |
| `StdoutLogAppender` | 输出到标准输出，按日志级别着色 |

### FileLogAppender 配置项

| 键 | 类型 | 默认 | 说明 |
|----|------|------|------|
| `file` | string | 必填 | 日志文件路径 |
| `level` | string | 继承 logger | 输出级别过滤 |
| `formatter` | string | 内置 | 自定义输出格式 |
| `time_rotate` | string | `none` | 时间切分：`none`/`30min`/`hour`/`12hour`/`day`/`week`/`month` |
| `max_size` | int(字节) | `0` | 单文件最大字节数，超过即分片；`0` 不限制 |
| `retention_days` | int | `0` | 保留天数，`0` 不限制；仅在 `time_rotate` 非 `none` 时生效 |

### 按大小分片（`max_size`）

- 当前文件写入超过 `max_size` 后，下一次写入自动切换到新分片文件
- 单条日志超过 `max_size` 不会被拆分，写入后下一条才触发分片
- 可与 `time_rotate` 并存：时间周期变更时新文件分片序号从 0 重新开始

分片文件命名（序号插入扩展名前）：

| 场景 | 文件命名 |
|------|---------|
| `time_rotate: day` + `max_size` | `app_2024-11-02.log` → `app_2024-11-02_1.log` |
| `time_rotate: none` + `max_size` | `app.log` → `app_1.log` |

> `max_size` 分片文件不按 `retention_days` 清理（清理依赖文件名中的日期）；如需自动清理建议配合 `time_rotate` 使用。

## 配置热重载

`LoggerManager` 监听配置变化，支持运行时调整日志级别和 Appender。

## 线程安全

- 多线程安全（mutex 保护）
- `~Fiber()` 析构打日志时清空 `t_fiber`，避免访问已释放协程
