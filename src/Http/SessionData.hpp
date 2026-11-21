/**
 * @file SessionData.hpp
 * @brief session数据
 * @author Christins
 * @date 2025-05-11
 * @copyright GPL-3.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <boost/any.hpp>

#include "../Util/Singleton.hpp"

namespace chen::http {

class SessionData {
public:
    typedef std::shared_ptr<SessionData> ptr;
    SessionData(bool auto_gen = false);

    template <class T>
    void setData(const std::string& key, const T& v)
    {
        std::unique_lock lock(mutex_);
        datas_[key] = v;
    }

    template <class T>
    T getData(const std::string& key, const T& def = T{})
    {
        std::shared_lock lock(mutex_);
        auto it = datas_.find(key);
        if (it == datas_.end()) {
            return def;
        }
        boost::any v = it->second;
        lock.unlock();
        try {
            return boost::any_cast<T>(v);
        } catch (...) {
        }
        return def;
    }

    void del(const std::string& key);

    bool has(const std::string& key);
    uint64_t getLastAccessTime() const { return last_access_time_; }
    void setLastAccessTime(uint64_t v) { last_access_time_ = v; }

    const std::string& getId() const { return id_; }
    void setId(const std::string& v) { id_ = v; }

private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, boost::any> datas_;
    uint64_t last_access_time_;
    std::string id_;
};

class SessionDataManager {
public:
    void add(SessionData::ptr info);
    void del(const std::string& id);
    SessionData::ptr get(const std::string& id);
    void check(int64_t ts = 3600);

private:
    std::shared_mutex mutex_;
    std::unordered_map<std::string, SessionData::ptr> datas_;
};

typedef Singleton<SessionDataManager> SessionDataMgr;

} // namespace chen::http
