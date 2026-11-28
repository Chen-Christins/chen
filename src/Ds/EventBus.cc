#include "EventBus.hpp"

#include <algorithm>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

void EventBus::doEmitAsync(EventKey event, std::any data)
{
    auto iom = IOManager::GetThis();
    if (!iom) {
        ERROR(g_logger) << "emitAsync: IOManager not available, event dropped";
        return;
    }
    std::vector<EventCallback> cbs;
    {
        std::shared_lock lock(mutex_);
        auto it = listeners_.find(event);
        if (it == listeners_.end()) {
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

uint64_t EventBus::on(EventKey event, EventCallback cb)
{
    std::unique_lock lock(mutex_);
    uint64_t id = next_id_++;
    listeners_[std::move(event)].push_back({id, std::move(cb)});
    return id;
}

void EventBus::off(EventKey event, uint64_t id)
{
    std::unique_lock lock(mutex_);
    auto it = listeners_.find(event);
    if (it == listeners_.end()) {
        return;
    }
    auto& vec = it->second;
    vec.erase(
        std::remove_if(
            vec.begin(), vec.end(),
            [id](const auto& entry) {
                return entry.id == id;
            }),
        vec.end());
}

void EventBus::emitAsync(EventKey event, std::any data)
{
    doEmitAsync(std::move(event), std::move(data));
}

void EventBus::clear(EventKey event)
{
    std::unique_lock lock(mutex_);
    listeners_.erase(event);
}

void EventBus::clearAll()
{
    std::unique_lock lock(mutex_);
    listeners_.clear();
}

} // namespace chen
