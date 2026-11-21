#include "SessionData.hpp"

#include <sstream>
#include <vector>

#include "../Util/EncryptorUtil.hpp"

namespace chen::http {

SessionData::SessionData(bool auto_gen)
    : last_access_time_(time(0))
{
    if (auto_gen) {
        std::stringstream ss;
        ss << "chen::TimeUtil::GetCurrentUs()" << "|" << rand() << "|" << rand() << "|" << rand();
        id_ = EncryptorUtil::MD5(ss.str());
    }
}

void SessionData::del(const std::string& key)
{
    std::unique_lock lock(mutex_);
    datas_.erase(key);
}

bool SessionData::has(const std::string& key)
{
    std::shared_lock lock(mutex_);
    auto it = datas_.find(key);
    return it != datas_.end();
}

void SessionDataManager::add(SessionData::ptr info)
{
    std::unique_lock lock(mutex_);
    datas_[info->getId()] = info;
}

void SessionDataManager::del(const std::string& id)
{
    std::unique_lock lock(mutex_);
    datas_.erase(id);
}

SessionData::ptr SessionDataManager::get(const std::string& id)
{
    std::shared_lock lock(mutex_);
    auto it = datas_.find(id);
    if (it != datas_.end()) {
        it->second->setLastAccessTime(time(0));
        return it->second;
    }
    return nullptr;
}

void SessionDataManager::check(int64_t ts)
{
    uint64_t now = time(0) - ts;
    std::vector<std::string> keys;
    std::shared_lock lock(mutex_);
    for (auto& i : datas_) {
        if (i.second->getLastAccessTime() < now) {
            keys.push_back(i.first);
        }
    }
    lock.unlock();
    for (auto& i : keys) {
        del(i);
    }
}

} // namespace chen::http
