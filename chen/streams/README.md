# Streams — 流式读写扩展

提供负载均衡、服务发现、流压缩等 `Stream` 扩展实现。

## 核心类

| 类                       | 说明                                                                    |
|--------------------------|-------------------------------------------------------------------------|
| `ZlibStream`             | Zlib/Deflate/Gzip 压缩解压流，继承 `Stream`，包裹另一个 Stream 透明压缩 |
| `HolderStats`            | 连接宿主统计（耗时、成功/失败/超时计数）                                |
| `HolderStatsSet`         | 统计集合管理                                                            |
| `IServiceDiscovery`      | 服务发现接口（注册/注销/查询服务实例）                                  |
| `StaticServiceDiscovery` | 静态配置型服务发现，实例列表来自 yml                                    |
| `SDLoadBalance`          | 基于服务发现的负载均衡基类                                              |
| `RockSDLoadBalance`      | Rock 协议负载均衡，见 `chen/rock/README.md`                             |

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

`IServiceDiscovery` 是抽象基类，具体实现负责把实例变更通过 `service_callback`
推给订阅者。框架内置 `StaticServiceDiscovery`，实例列表直接来自配置：

```yaml
service_discovery:
  domains:
    astartes:
      rock:
        - name: "rock-1"
          address: "127.0.0.1:9001"
          weight: 100
        - name: "rock-2"
          address: "127.0.0.1:9002"
          weight: 300
```

```cpp
auto sd = std::make_shared<chen::StaticServiceDiscovery>();
sd->setServiceCallback([](const std::string& domain,
                          const std::string& service,
                          const auto& old_value,
                          const auto& new_value) {
    // new_value: unordered_map<uint64_t, ServiceItemInfo::ptr>
});
sd->start();

// 运行时增量更新
sd->addServer("astartes", "rock", "127.0.0.1:9003", "rock-3", 50);
sd->delServer("astartes", "rock", "127.0.0.1:9001");
sd->stop();
```

`service_discovery.domains` 注册了 `ConfigVar` 监听，修改 yml 触发配置热加载后会自动 `setConf()`
并重新发布实例，不需要重启。`name` 是实例别名（会写入 `ServiceItemInfo::m_data`，仅用于标识/日志），
`weight` 会传递到 `LoadBalanceItem`，`FAIR`/`WEIGHT` 策略按权重选择。

`Application` 在启动 worker 时会自动创建 `StaticServiceDiscovery`（配置非空时），
在模块 `onServerReady` 之后 `start()`，在模块 `onUnload` 之后 `stop()`。
模块可通过 `Application::GetInstance()->getServiceDiscovery()` 取用。

### 负载均衡

`load_balance.h` 结合服务发现，在多个服务实例间分发连接，跟踪每个实例的耗时和成功率。
