#include "CacheStatus.hpp"

namespace chen {
namespace ds {

void CacheStatus::merge(const CacheStatus& oth)
{
    get_ += oth.get_;
    set_ += oth.set_;
    del_ += oth.del_;
    timeout_ += oth.timeout_;
    prune_ += oth.prune_;
    hit_ += oth.hit_;
}

std::string CacheStatus::toString() const
{
    std::stringstream ss;
    ss << "get=" << get_ << " set=" << set_ << " del=" << del_ << " prune=" << prune_ << " timeout=" << timeout_
       << " hit=" << hit_ << " hit_rate" << (getHitRate() * 100.0) << "%";
    return ss.str();
}

std::ostream& operator<<(std::ostream& os, const CacheStatus& status)
{
    os << status.toString();
    return os;
}

} // namespace ds
} // namespace chen
