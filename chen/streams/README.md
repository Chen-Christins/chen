# Streams — 流式读写扩展

提供负载均衡、服务发现、流压缩等 `Stream` 扩展实现。

## 核心类

| 类 | 说明 |
|------|------|
| `ZlibStream` | Zlib/Deflate/Gzip 压缩解压流，继承 `Stream`，包裹另一个 Stream 透明压缩 |
| `HolderStats` | 连接宿主统计（耗时、成功/失败/超时计数） |
| `HolderStatsSet` | 统计集合管理 |
| `IServiceDiscovery` | 服务发现接口（注册/注销/查询服务实例） |

## 使用

### ZlibStream

对已有 Stream 的数据做透明压缩：

```cpp
// 创建 zlib 压缩流（包裹 tcp 连接）
auto conn = SocketStream::ptr(new SocketStream(sock));
ZlibStream::ptr zs = ZlibStream::Create(ZlibStream::GZIP, conn);
// 读写经过压缩/解压
std::string data;
zs->readFixSize(&data[0], data.size());
```

### 服务发现

```cpp
class MyDiscovery : public IServiceDiscovery {
    void registerServer(const std::string& domain, const std::string& service,
                        const std::string& ip, uint16_t port, const std::string& data) override;
    void queryServer(const std::string& domain, const std::string& service,
                     std::vector<ServiceItemInfo::ptr>& results) override;
};
```

### 负载均衡

`load_balance.h` 结合服务发现，在多个服务实例间分发连接，跟踪每个实例的耗时和成功率。
