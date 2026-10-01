# ByteArray — 字节数组

高性能二进制数据读写缓冲区，支持网络字节序和变长编码。

## 使用示例

```cpp
ByteArray ba;
ba.writeFint32(42);                  // 写入 4 字节
ba.writeStringVint("hello");         // 写入 varint 长度前缀 + 字符串
ba.writeDouble(3.14);
ba.setPosition(0);                   // 倒回起始位置

int32_t  v = ba.readFint32();        // → 42
std::string s = ba.readStringVint(); // → "hello"
double   d = ba.readDouble();        // → 3.14
```

## 核心类

- **ByteArray** — 可动态扩容的字节缓冲区

## 设计

```
[read position]  ← 已读区域 →  [write position]  ← 可写区域 →  [capacity]
```

使用 `std::vector<char>` 或自定义内存池作为底层存储。

## 写入方法

| 方法 | 说明 |
|------|------|
| `writeFint8/16/32/64(v)` | 写入定长整数（网络字节序） |
| `writeStringF16/F32(s)` | 写入字符串 + 定长长度前缀 |
| `writeStringVint(s)` | 写入字符串 + 变长长度前缀 |
| `writeInt32/64(v)` | 写入有符号整数（ZigZag 编码） |
| `writeFloat/Double(v)` | 写入浮点数 |
| `writeBytes(data, len)` | 写入原始字节 |

## 读取方法

对称的 `read*` 方法，从当前位置读取并推进 read position。

## 变长编码

`writeInt32(42)` 使用 Varint 编码，小数值占用更少字节。

## 使用场景

- RPC 协议序列化/反序列化
- Game 协议帧编解码
- 网络数据包的组装与解析
