# Util 工具模块

## 快速使用

```cpp
#include "util/util.h"

using namespace chen;

// ── 时间 ──
uint64_t now_ms = GetCurrentMs();
std::string ts = Time2Str(now_ms, "%Y-%m-%d %H:%M:%S");  // 2026-06-28 12:00:00
bool expired = IsExpired(start_ms, 30000);                 // 30s TTL

// ── 加密 ──
std::string md5 = EncryptorUtil::MD5("data");              // hex 32 字符
std::string sha1 = EncryptorUtil::SHA1("data");            // hex 40 字符
// 若需要原始 20 字节（例如 WebSocket 握手）：
std::string raw = StringUtil::HexDecode(EncryptorUtil::SHA1("data"));

// ── 随机数 ──
uint32_t r = RandomUtil::RandUint(1, 100);                 // [1, 100] 闭区间
std::string token = RandomUtil::RandString(32);             // 随机英数串
auto* elem = RandomUtil::RandElement(vec);                  // 随机取元素

// ── 字符串 ──
std::string enc = StringUtil::UrlEncode("hello world");     // hello+world
std::string dec = StringUtil::UrlDecode(enc);
std::vector<std::string> parts = StringUtil::Split("a,b,c", ',');
std::string b64 = StringUtil::Base64Encode("data");
std::string raw = StringUtil::HexDecode("1a2b3c");

// ── 堆栈回溯（调试用） ──
std::string bt = BacktraceToString(10, 2, "    ");

// ── 原子操作 ──
int val = 0;
Atomic::addFetch(val, 1);                                   // 线程安全自增
```

## 模块清单

| 文件 | 用途 | 注意 |
|------|------|------|
| `util.h` | 聚合头文件 + 线程/fiber ID + 堆栈回溯 + `Atomic`（`__sync_*` 旧版原子操作） | `Atomic` 使用 GCC `__sync_*` 而非 `std::atomic`，始终全序一致性 |
| `noncopyable.h` | 不可拷贝基类 | 析构函数是 `virtual` 的，会引入 vtable 开销 |
| `macro.h` | `ASSERT`/`ASSERT_MSG` 断言 + `LIKELY`/`UNLIKELY` 分支预测 | 拼写故意写错以避免宏名冲突 |
| `endian.h` | 字节序转换（`byteswap`、`byteswapOnLittleEndian`） | `byteswapOnLittleEndian` 在小端机上转成大端（网络字节序） |

### 加密 / 哈希

| 文件 | 用途 | 注意 |
|------|------|------|
| `encryptor_util.h` | SHA1/256/512、MD5、AES-GCM、HMAC、bcrypt、MurmurHash3、XXHash | **SHA/MD5 系列返回 hex 字符串**，不是原始字节。<br>**HMAC 系列返回原始二进制**，与 SHA 不一致。 |
| `random_util.h` | 随机数、随机字符串/字节、shuffle、加权选择、Snowflake ID 生成器 | `RandInt(a, b)` **两端闭合** `[a, b]`；内部用 `thread_local mt19937_64` |

### 字符串

| 文件 | 用途 | 注意 |
|------|------|------|
| `string_util.h` | Format、UrlEncode/Decode、Trim、Split/Join、Base64、HexEncode/HexDecode | **有两套 URL 编解码：** `UrlEncode`(space→`+`) 和 `URLEncode`(space→`%20`)，容易搞混 |

### 时间

| 文件 | 用途 | 注意 |
|------|------|------|
| `time_util.h` | 高精度时钟、时间格式化/解析(ISO 8601)、HTTP 时间、日/周/月边界、TTL 检查 | `Str2Time` 自动检测 ISO 8601 / 日期 / 时间格式；还提供 `chen` 命名空间的内联快捷函数 |

### 文件系统

| 文件 | 用途 | 注意 |
|------|------|------|
| `fs_util.h` | 文件列表、PID 检查、目录创建、路径分割、文件读写 | `ListAllFile` 第二个参数叫 `subfix`（拼写如此，非 typo） |

### 序列化 / 类型

| 文件 | 用途 | 注意 |
|------|------|------|
| `string_util.h` | Format、UrlEncode、Base64、Hex | — |
| `json_util.h` | JsonCpp 的 `Value ↔ string` 包装 | 极简，无文件 I/O |
| `type_util.h` | `Atoi`/`Atof`/`ToString`/`ToChar` | `Atoi`/`Atof` 不返回错误标志，转换失败抛异常 |
| `compress_util.h` | Gzip / Zlib 压缩解压 | — |
| `mutex.h` | 互斥锁封装 | 从 `chen/mutex.h` 移入，框架基础设施 |
| `singleton.h` | 单例模板 | 从 `chen/singleton.h` 移入 |
| `env.h` | 命令行参数解析、环境变量 | 从 `chen/env.h` 移入 |

## 常见坑

### SHA1 返回 hex，HMAC 返回 raw

```cpp
// SHA 系列返回 hex 字符串：
EncryptorUtil::SHA1("data")  // → "a35491d5f0fa..." (40 字符)

// 如果你需要原始 20 字节，需要 HexDecode：
StringUtil::HexDecode(EncryptorUtil::SHA1("data"))
// → 20 原始字节

// HMAC 系列直接返回原始二进制（不一致！）：
EncryptorUtil::HMAC_SHA256("data", "key")
// → 32 原始字节
```

这个差异在 WebSocket 握手（`Sec-WebSocket-Accept` 计算）中踩过坑。

### 两套 URL 编码

- `UrlEncode(space_as_plus=true)` → `+` 表示空格（form 编码）
- `URLEncode()` → `%20` 表示空格（纯 RFC 3986）

名字只差一个大写字母，容易用错。

### `RandInt` 两端闭合

```cpp
RandomUtil::RandInt(0, 100)  // 返回 [0, 100] 闭区间
```
大多数随机库是 `[min, max)` 半开区间，这里是闭合的。
