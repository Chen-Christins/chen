# Metrics — 指标注册与 Prometheus 导出

轻量、线程安全的进程级指标注册表。指标对象注册后生命周期与进程一致，适合在服务启动阶段创建、在请求热路径上更新。

## 指标类型

| 类型        | 用途             | 示例                      |
|-------------|------------------|---------------------------|
| `Counter`   | 单调递增计数     | 请求总数、错误数          |
| `Gauge`     | 可增可减的瞬时值 | 活跃连接数、队列长度      |
| `GaugeFunc` | 采集时回调取值   | fiber 数、fd 数、定时器数 |
| `Histogram` | 固定桶分布       | 请求耗时（秒）            |

## 使用

```cpp
#include <chen/metrics/metrics.h>

using namespace chen::metrics;

auto* registry = RegistryMgr::GetInstance();

// Counter：带标签
auto* requests = registry->getCounter(
    "chen_http_requests_total", "HTTP requests total",
    {{"server", "game_http"}, {"method", "GET"}, {"status", "200"}});
requests->inc();

// Gauge
auto* active = registry->getGauge(
    "chen_http_active_requests", "Active HTTP requests",
    {{"server", "game_http"}});
active->inc();
active->dec();

// GaugeFunc：采集时回调，避免热路径维护计数
registry->getGaugeFunc("chen_fibers", "Current fiber count", {},
    []() { return (int64_t)chen::Fiber::GetFiberCount(); });

// Histogram：耗时（秒）
auto* latency = registry->getHistogram(
    "chen_http_request_duration_seconds", "HTTP request latency",
    {{"server", "game_http"}, {"route", "/api/user"}});
latency->observe(0.0012);
```

## Prometheus 输出

```cpp
std::string text = RegistryMgr::GetInstance()->RenderPrometheus();
```

输出为标准 Prometheus 文本格式：

```
# HELP chen_http_requests_total HTTP requests total
# TYPE chen_http_requests_total counter
chen_http_requests_total{method="GET",server="game_http",status="200"} 1
# HELP chen_http_request_duration_seconds HTTP request latency
# TYPE chen_http_request_duration_seconds histogram
chen_http_request_duration_seconds_bucket{route="/api/user",le="0.001"} 0
chen_http_request_duration_seconds_bucket{route="/api/user",le="0.002"} 1
...
chen_http_request_duration_seconds_bucket{route="/api/user",le="+Inf"} 1
chen_http_request_duration_seconds_sum{route="/api/user"} 0.0012
chen_http_request_duration_seconds_count{route="/api/user"} 1
```

- 标签按 key 排序，输出稳定
- 指标名/标签名中的非法字符会替换为 `_`
- 标签值中的 `\`、`"`、换行会被转义
- 同一 `name + labels` 重复注册会返回已有对象；类型不一致时返回 `nullptr` 并输出错误

## 渲染格式

| 格式 | 接口 | Content-Type |
|------|------|--------------|
| Prometheus | `RenderPrometheus()` / `Render(RenderFormat::PROMETHEUS)` | `text/plain; version=0.0.4; charset=utf-8` |
| OpenMetrics | `RenderOpenMetrics()` / `Render(RenderFormat::OPENMETRICS)` | `application/openmetrics-text; version=1.0.0; charset=utf-8` |
| JSON | `RenderJson()` / `Render(RenderFormat::JSON)` | `application/json; charset=utf-8` |

OpenMetrics 输出在 Prometheus 文本基础上追加 `# EOF`。JSON 输出结构：

```json
{
  "metrics": [
    {
      "name": "chen_http_requests_total",
      "help": "HTTP requests total",
      "type": "counter",
      "labels": {"method": "GET", "status": "200"},
      "value": 12
    },
    {
      "name": "chen_http_request_duration_seconds",
      "help": "HTTP request latency",
      "type": "histogram",
      "labels": {"route": "/api/user"},
      "buckets": [
        {"le": 0.001, "cumulative": 0},
        {"le": 0.002, "cumulative": 1}
      ],
      "count": 1,
      "sum": 0.0012
    }
  ]
}
```

JSON 中 `+Inf` 桶省略，由 `count` 隐含。

## 业务侧接入 /metrics

框架暂不内置采集端口，业务模块可自行把 `/metrics` 挂到指定 HTTP Server。推荐使用 `chen/http/metrics_servlet.h` 提供的辅助函数：

```cpp
#include <chen/http/metrics_servlet.h>

bool MyModule::onServerReady() {
    std::vector<chen::http::HttpServer::ptr> servers;
    getAllHttpServer(servers);
    for (auto& server : servers) {
        // 默认 Prometheus 格式
        chen::http::RegisterMetricsServlet(server, "/metrics");
        // 或：OpenMetrics / JSON
        // chen::http::RegisterMetricsServlet(server, "/metrics/openmetrics",
        //     chen::metrics::RenderFormat::OPENMETRICS);
        // chen::http::RegisterMetricsServlet(server, "/metrics/json",
        //     chen::metrics::RenderFormat::JSON);
    }
    return true;
}
```

注意事项：

- registry 是 **进程级（worker 级）** 的；多 worker 时各 worker 指标独立，需要分别采集，或后续由 master/Admin 聚合。
- 不同模块共享同一 registry，指标名建议加业务前缀（如 `game_`），避免同名冲突。
- `/metrics` 建议只挂在内网/管理端口，或增加鉴权，不要直接暴露公网。
- `GaugeFunc` 回调在渲染时执行，必须是快速且线程安全的操作。
- 指标对象的生命周期与进程一致，目前没有注销接口；热重载由新 worker 重新注册。

## 性能基准

`tests/test_metrics_bench.cc` 提供多线程微基准，建议用 Release 构建：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
tests/bin/test_metrics_bench 2000000 8   # 每线程 200 万次，8 线程
```

覆盖：

- 单线程 / 多线程共享 `Counter::inc`
- 单线程 / 多线程共享 `Histogram::observe`（固定命中同一桶，模拟热点争用）
- `Registry::getCounter` 查找成本（含 mutex、label 排序与 key 构造）

输出包含 `elapsed_ms`、`Mops/s`、`ns/op`。Debug 构建是 `-O0`，数据没有参考价值。

## 注意事项

- `Counter` / `Histogram` 采用 **每线程分片**：热路径只更新本线程的 shard，`value()` / 渲染时聚合，避免跨核 cacheline 争用
- 分片数默认不小于 16，按 CPU 数向上取 2 的幂，最多 64；线程退出后其分片数据保留到进程结束
- 分片会增加内存：`Counter` 约 `分片数 × 64B`，`Histogram` 约 `分片数 × stride × 8B`（stride 按 64B 对齐）
- 所有指标计数使用 `std::atomic` + `memory_order_relaxed`，不引入全屏障
- `Histogram` 内部只记录命中的单个桶（1 次桶原子操作，另加 count/sum），渲染时再计算累计 `le` 值
- `GaugeFunc` 回调在 `/metrics` 渲染时执行，不要在回调里做耗时操作
- 指标对象不要手动释放，registry 统一持有到进程退出
