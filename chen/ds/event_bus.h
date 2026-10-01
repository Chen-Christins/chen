/**
 * @file event_bus.h
 * @brief 事件注册分发器（观察者模式），支持同步/异步触发
 * @author Christins
 * @date 2026-07-02
 * @copyright GPL-3.0
 */
#pragma once

#include <any>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "../util/singleton.h"

namespace chen {

/**
 * @brief 事件回调类型
 * @param data 事件数据（std::any，回调内通过 std::any_cast<T> 取回具体类型）
 */
using EventCallback = std::function<void(const std::any& data)>;

/**
 * @brief 事件 key，字符串或整型（枚举值）
 */
using EventKey = std::variant<std::string, int64_t>;

/**
 * @brief 事件总线（观察者模式）
 *
 * @code
 * auto bus = EventBusMgr::GetInstance();
 *
 * // 整型事件
 * bus->on((int64_t)Event::ArticlePublished, [](auto& d) {
 *     auto& json = std::any_cast<const Json::Value&>(d);
 * });
 *
 * // 字符串事件
 * bus->on("user.created", [](auto& d) { ... });
 *
 * bus->emit((int64_t)Event::ArticlePublished, Json::Value());
 * bus->emit("user.created", 42);
 * @endcode
 *
 * 线程安全：shared_mutex。
 */
class EventBus {
public:
    typedef std::shared_ptr<EventBus> ptr;

    EventBus() = default;

    /**
     * @brief 注册回调
     * @param event 事件 key（std::string 或 int64_t）
     * @param cb 回调函数
     * @return 回调 ID，用于 off()
     */
    uint64_t on(EventKey event, EventCallback cb);

    /**
     * @brief 取消注册
     */
    void off(EventKey event, uint64_t id);

    /**
     * @brief 同步触发事件
     * @tparam T 事件数据类型（自动推导，存入 std::any）
     */
    template <typename T>
    void emit(EventKey event, T&& data) {
        doEmit(std::move(event), std::forward<T>(data));
    }

    /**
     * @brief 异步触发事件（通过 IOManager 投递到协程池）
     */
    void emitAsync(EventKey event, std::any data);

    template <typename T>
    void emitAsync(EventKey event, T&& data) {
        emitAsync(std::move(event), std::any(std::forward<T>(data)));
    }

    /**
     * @brief 清空某事件的所有监听器
     */
    void clear(EventKey event);

    /**
     * @brief 清空全部监听器
     */
    void clearAll();

private:
    struct ListenerEntry {
        uint64_t id;
        EventCallback cb;
    };

    template <typename T>
    void doEmit(EventKey event, T&& data) {
        std::vector<EventCallback> cbs;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_listeners.find(event);
            if (it == m_listeners.end()) {
                return;
            }
            for (auto& entry : it->second) {
                cbs.push_back(entry.cb);
            }
        }
        std::any payload = std::forward<T>(data);
        for (auto& cb : cbs) {
            cb(payload);
        }
    }

    void doEmitAsync(EventKey event, std::any data);

private:
    std::shared_mutex m_mutex;
    std::unordered_map<EventKey, std::vector<ListenerEntry>> m_listeners;
    uint64_t m_nextId = 1;
};

typedef chen::Singleton<EventBus> EventBusMgr;

} // namespace chen
