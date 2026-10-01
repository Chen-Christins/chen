#include "event_bus.h"

#include <algorithm>

#include "../iomanager/iomanager.h"
#include "../log/log.h"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

void EventBus::doEmitAsync(EventKey event, std::any data) {
    auto iom = IOManager::GetThis();
    if (!iom) {
        ERROR(g_logger) << "emitAsync: IOManager not available, event dropped";
        return;
    }
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
    for (auto& cb : cbs) {
        auto cb_ptr = std::make_shared<EventCallback>(std::move(cb));
        iom->schedule([cb_ptr, data]() {
            (*cb_ptr)(data);
        });
    }
}

uint64_t EventBus::on(EventKey event, EventCallback cb) {
    std::unique_lock lock(m_mutex);
    uint64_t id = m_nextId++;
    m_listeners[std::move(event)].push_back({id, std::move(cb)});
    return id;
}

void EventBus::off(EventKey event, uint64_t id) {
    std::unique_lock lock(m_mutex);
    auto it = m_listeners.find(event);
    if (it == m_listeners.end()) {
        return;
    }
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [id](const auto& entry) {
            return entry.id == id;
        }), vec.end());
}

void EventBus::emitAsync(EventKey event, std::any data) {
    doEmitAsync(std::move(event), std::move(data));
}

void EventBus::clear(EventKey event) {
    std::unique_lock lock(m_mutex);
    m_listeners.erase(event);
}

void EventBus::clearAll() {
    std::unique_lock lock(m_mutex);
    m_listeners.clear();
}

} // namespace chen
