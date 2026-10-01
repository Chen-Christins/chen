/**
 * @file session_data.h
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

#include "../util/singleton.h"

namespace chen::http {

class SessionData {
public:
    typedef std::shared_ptr<SessionData> ptr;
    SessionData(bool auto_gen = false);

    template <class T>
    void setData(const std::string& key, const T& v) {
        std::unique_lock lock(m_mutex);
        m_datas[key] = v;
    }

    template <class T>
    T getData(const std::string& key, const T& def = T{}) {
        std::shared_lock lock(m_mutex);
        auto it = m_datas.find(key);
        if (it == m_datas.end()) {
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
    uint64_t getLastAccessTime() const { return m_lastAccessTime; }
    void setLastAccessTime(uint64_t v) { m_lastAccessTime = v; }

    const std::string& getId() const { return m_id; }
    void setId(const std::string& v) { m_id = v; }

private:
    std::shared_mutex m_mutex;
    std::unordered_map<std::string, boost::any> m_datas;
    uint64_t m_lastAccessTime;
    std::string m_id;
};

class SessionDataManager {
public:
    void add(SessionData::ptr info);
    void del(const std::string& id);
    SessionData::ptr get(const std::string& id);
    void check(int64_t ts = 3600);

private:
    std::shared_mutex m_mutex;
    std::unordered_map<std::string, SessionData::ptr> m_datas;
};

typedef Singleton<SessionDataManager> SessionDataMgr;

} // namespace chen::http
