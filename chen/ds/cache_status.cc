/**
 * @file cache_status.cc
 * @brief 缓存状态统计实现
 */
#include "cache_status.h"

namespace chen {
namespace ds {

void CacheStatus::merge(const CacheStatus& oth)
{
    m_get += oth.m_get;
    m_set += oth.m_set;
    m_del += oth.m_del;
    m_timeout += oth.m_timeout;
    m_prune += oth.m_prune;
    m_hit += oth.m_hit;
}

std::string CacheStatus::toString() const
{
    std::stringstream ss;
    ss << "get=" << m_get << " set=" << m_set << " del=" << m_del << " prune=" << m_prune << " timeout=" << m_timeout << " hit=" << m_hit
       << " hit_rate" << (getHitRate() * 100.0) << "%";
    return ss.str();
}

std::ostream& operator<<(std::ostream& os, const CacheStatus& status)
{
    os << status.toString();
    return os;
}

} // namespace ds
} // namespace chen
