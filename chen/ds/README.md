# DS — 数据结构

通用数据结构与工具类。

## 组件

| 文件 | 说明 |
|------|------|
| `lru_cache.h` | 分段 LRU 缓存（`HashLruCache`） |
| `event_bus.h/cc` | 事件总线（观察者模式） |
| `dispatcher.h/cc` | 条件事件分发器（观察者 + 解释器模式） |
| `bitmap.h/cc` | 位图 |
| `roaring_bitmap.h/cc` | Roaring Bitmap（压缩位图） |

---

## EventBus — 事件总线

线程安全的事件注册分发器。事件 key 支持字符串或整型（`EventKey = std::variant<std::string, int64_t>`），事件数据为 `std::any`。

### 使用

```cpp
#include <chen/ds/event_bus.h>

auto bus = EventBusMgr::GetInstance();

// 整型事件（枚举）
enum class BlogEvent : int64_t { UserCreated = 1, ArticlePublished };
bus->on((int64_t)BlogEvent::ArticlePublished, [](auto& d) {
    auto& json = std::any_cast<const Json::Value&>(d);
});

// 字符串事件
bus->on("user.created", [](auto& d) {
    int64_t userId = std::any_cast<int64_t>(d);
});

// 同步触发（任意类型）
bus->emit((int64_t)BlogEvent::ArticlePublished, Json::Value{{"id", 123}});
bus->emit("user.created", (int64_t)42);

// 异步触发（IOManager 协程池）
bus->emitAsync((int64_t)BlogEvent::ArticlePublished, Json::Value{{"id", 456}});

// 取消
bus->off("user.created", id);
```

### API

| 方法 | 说明 |
|------|------|
| `on(EventKey, cb) -> id` | 注册回调 |
| `off(EventKey, id)` | 取消注册 |
| `emit<T>(EventKey, data)` | 同步触发，data 任意类型 |
| `emitAsync(EventKey, data)` | 异步触发；无 IOManager 时打日志丢弃 |
| `clear(EventKey)` | 清空某事件 |
| `clearAll()` | 清空全部 |

### 关键点

- `emit` 期间回调中修改订阅安全（先拷贝列表再执行）
- `emitAsync` 不降级为同步，无 IOManager 直接丢弃

---

## EventDispatcher — 条件事件分发器

在 EventBus 基础上增加 `ICondition` 解释器。订阅时绑定条件，触发时校验通过才执行回调。

### 使用

```cpp
#include <chen/ds/dispatcher.h>

auto dp = EventDispatcherMgr::GetInstance();

// 无条件订阅
dp->on((int64_t)Event::UserCreated, [](auto& d) { ... });

// 条件订阅：state == 2 才触发
dp->on((int64_t)Event::ArticleStateChanged,
    [](auto& d) { /* 通知粉丝 */ },
    std::make_shared<FieldEquals>("state", (int64_t)2));

// 触发
dp->emit((int64_t)Event::ArticleStateChanged, Json::Value{{"state", 0}});  // 校验失败，跳过
dp->emit((int64_t)Event::ArticleStateChanged, Json::Value{{"state", 2}});  // 校验通过，触发
```

### FieldEquals 内置条件

| 构造 | 条件 |
|------|------|
| `FieldEquals("f", (int64_t)N)` | `data["f"]` 整数值 == N |
| `FieldEquals("f", "val")` | `data["f"]` 字符串 == "val" |
| `FieldEquals("f", true)` | `data["f"]` 布尔 == true |
| `FieldEquals("f", 3.14)` | `data["f"]` 浮点 == 3.14 |

### 自定义解释器

```cpp
class StateTransition : public ICondition {
    bool check(const std::any& data) const override {
        auto& json = std::any_cast<const Json::Value&>(data);
        return json["old"].asInt64() != json["new"].asInt64();
    }
};
```

### API

| 方法 | 说明 |
|------|------|
| `on(EventKey, cb)` | 无条件订阅 |
| `on(EventKey, cb, cond)` | 条件订阅 |
| `off(EventKey, id)` | 取消 |
| `emit<T>(EventKey, data)` | 同步触发，校验后执行 |
| `emitAsync(EventKey, data)` | 异步触发 |
| `clear(EventKey)` / `clearAll()` | 清空 |

### 与 EventBus 区别

| | EventBus | EventDispatcher |
|------|----------|-----------------|
| 订阅 | 无条件 | 可选绑定 ICondition |
| 触发 | 全部回调执行 | 解释器校验通过才执行 |

---

## HashLruCache — 分段 LRU 缓存

```cpp
#include <chen/ds/lru_cache.h>

// 16 段，最大 1000 条，30 秒 TTL
chen::ds::HashLruCache<int64_t, std::string> cache(16, 1000, 30);

cache.set(key, value);
auto v = cache.get(key);          // nullptr if miss or expired
cache.del(key);
bool hit = cache.exists(key);
```
