#include "Dispatcher.hpp"

#include <algorithm>

#include <json/json.h>

#include "../IOManager/IOManager.hpp"
#include "../Log/Log.hpp"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

void EventDispatcher::doEmitAsync(EventKey event, std::any data)
{
    auto iom = IOManager::GetThis();
    if (!iom) {
        ERROR(g_logger) << "emitAsync: IOManager not available, event dropped";
        return;
    }
    std::vector<std::pair<EventCallback, ICondition::ptr>> cbs;
    {
        std::shared_lock lock(mutex_);
        auto it = listeners_.find(event);
        if (it == listeners_.end()) {
            return;
        }
        for (auto& e : it->second) {
            cbs.emplace_back(e.cb, e.cond);
        }
    }
    for (auto& [cb, cond] : cbs) {
        auto cb_ptr = std::make_shared<EventCallback>(cb);
        auto cond_ptr = cond;
        iom->schedule([cb_ptr, cond_ptr, data]() {
            if (!cond_ptr || cond_ptr->check(data)) {
                (*cb_ptr)(data);
            }
        });
    }
}

uint64_t EventDispatcher::on(EventKey event, EventCallback cb)
{
    return on(std::move(event), std::move(cb), nullptr);
}

uint64_t EventDispatcher::on(EventKey event, EventCallback cb, ICondition::ptr cond)
{
    std::unique_lock lock(mutex_);
    uint64_t id = next_id_++;
    listeners_[std::move(event)].push_back({id, std::move(cb), std::move(cond)});
    return id;
}

void EventDispatcher::off(EventKey event, uint64_t id)
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
            [id](const auto& e) {
                return e.id == id;
            }),
        vec.end());
}

void EventDispatcher::emitAsync(EventKey event, std::any data)
{
    doEmitAsync(std::move(event), std::move(data));
}

void EventDispatcher::clear(EventKey event)
{
    std::unique_lock lock(mutex_);
    listeners_.erase(event);
}

void EventDispatcher::clearAll()
{
    std::unique_lock lock(mutex_);
    listeners_.clear();
}

} // namespace chen
