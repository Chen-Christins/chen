/**
 * @file dispatcher.h
 * @brief 条件事件分发器，触发时由解释器（ICondition）校验数据，满足条件才执行回调
 * @author Christins
 * @date 2026-07-02
 * @copyright GPL-3.0
 */
#pragma once

#include <any>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "event_bus.h"
#include "../util/singleton.h"

namespace chen {

/**
 * @brief 条件解释器接口
 *
 * 订阅时绑定，触发时由 Dispatcher 调用 check() 校验。
 * 返回 true 才执行回调。
 */
class ICondition {
public:
    typedef std::shared_ptr<ICondition> ptr;

    virtual ~ICondition() = default;

    /**
     * @brief 校验事件数据是否满足条件
     * @param data 事件数据（std::any，实现类通过 std::any_cast 提取字段）
     * @return true 满足条件，回调应被执行
     */
    virtual bool check(const std::any& data) const = 0;
};

/**
 * @brief 条件事件分发器（观察者 + 解释器模式）
 *
 * 事件数据为 std::any，可传递任意类型。
 * 线程安全：shared_mutex。
 */
class EventDispatcher {
public:
    typedef std::shared_ptr<EventDispatcher> ptr;

    EventDispatcher() = default;

    /**
     * @brief 无条件订阅
     */
    uint64_t on(EventKey event, EventCallback cb);

    /**
     * @brief 条件订阅
     * @param cond 条件解释器（为 nullptr 即无条件）
     */
    uint64_t on(EventKey event, EventCallback cb, ICondition::ptr cond);

    /**
     * @brief 取消订阅
     */
    void off(EventKey event, uint64_t id);

    /**
     * @brief 同步触发（解释器校验通过才执行回调）
     */
    template <typename T>
    void emit(EventKey event, T&& data) {
        doEmit(std::move(event), std::forward<T>(data));
    }

    /**
     * @brief 异步触发
     */
    void emitAsync(EventKey event, std::any data);

    /**
     * @brief 异步触发（模板版本，支持任意类型）
     */
    template <typename T>
    void emitAsync(EventKey event, T&& data) {
        emitAsync(std::move(event), std::any(std::forward<T>(data)));
    }

    /**
     * @brief 清除指定事件的所有订阅
     */
    void clear(EventKey event);

    /**
     * @brief 清除所有事件的所有订阅
     */
    void clearAll();

private:
    template <typename T>
    void doEmit(EventKey event, T&& data) {
        std::vector<std::pair<EventCallback, ICondition::ptr>> cbs;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_listeners.find(event);
            if (it == m_listeners.end()) return;
            for (auto& e : it->second) {
                cbs.emplace_back(e.cb, e.cond);
            }
        }
        std::any payload = std::forward<T>(data);
        for (auto& [cb, cond] : cbs) {
            if (!cond || cond->check(payload)) {
                cb(payload);
            }
        }
    }

    void doEmitAsync(EventKey event, std::any data);

private:
    struct Entry {
        uint64_t id;
        EventCallback cb;
        ICondition::ptr cond;
    };
    std::shared_mutex m_mutex;
    std::unordered_map<EventKey, std::vector<Entry>> m_listeners;
    uint64_t m_nextId = 1;
};

typedef chen::Singleton<EventDispatcher> EventDispatcherMgr;

} // namespace chen
