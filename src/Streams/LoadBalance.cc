#include "LoadBalance.hpp"

#include "../Config/Config.hpp"
#include "../IOManager/Worker.hpp"
#include "../Log/Log.hpp"
#include "../Util/RandomUtil.hpp"

namespace chen {

static chen::Logger::ptr logger = LOG_NAME("system");

static ConfigVar<uint32_t>::ptr g_load_balance_fail_threshold =
    Config::Lookup("load_balance.fail_threshold", (uint32_t)3, "consecutive request failures before an instance is marked unhealthy, 0 disables");

static ConfigVar<uint32_t>::ptr g_load_balance_fail_cooldown_ms =
    Config::Lookup("load_balance.fail_cooldown_ms", (uint32_t)5000, "unhealthy instance cooldown in ms");

LoadBalanceItem::~LoadBalanceItem()
{
}

ILoadBalance::~ILoadBalance()
{
}

SDLoadBalance::~SDLoadBalance()
{
}

HolderStats HolderStatsSet::getTotal()
{
    HolderStats rt;
    for (auto& i : stats_) {
#define XX(f) rt.f += i.f
        XX(used_time_);
        XX(total_);
        XX(doing_);
        XX(timeouts_);
        XX(oks_);
        XX(errs_);
#undef XX
    }
    return rt;
}

std::string HolderStats::toString()
{
    std::stringstream ss;
    ss << "[Stat total=" << total_ << " used_time=" << used_time_ << " doing=" << doing_ << " timeouts=" << timeouts_
       << " oks=" << oks_ << " errs=" << errs_ << " oks_rate=" << (total_ ? (oks_ * 100.0 / total_) : 0)
       << " errs_rate=" << (total_ ? (errs_ * 100.0 / total_) : 0)
       << " avg_used=" << (oks_ ? (used_time_ * 1.0 / oks_) : 0) << " weight=" << getWeight(1) << "]";
    return ss.str();
}

void LoadBalanceItem::close()
{
    if (!stream_) {
        return;
    }
    auto stream = stream_;

    // 优先使用配置的 service_io 调度器；未配置时回退当前调度器，避免 close 任务被丢弃
    if (auto sched = WorkerMgr::GetInstance()->get("service_io")) {
        sched->schedule([stream]() {
            stream->close();
        });
        return;
    }
    if (auto sched = Scheduler::GetThis()) {
        sched->schedule([stream]() {
            stream->close();
        });
        return;
    }
    stream->close();
}

bool LoadBalanceItem::isValid()
{
    if (!stream_ || !stream_->isConnected()) {
        return false;
    }

    uint32_t threshold = g_load_balance_fail_threshold->getValue();
    if (threshold == 0) {
        return true;
    }
    if (consecutive_fails_.load() < threshold) {
        return true;
    }

    // 冷却期外放行一次，给实例恢复的机会（half-open）
    uint64_t cooldown = g_load_balance_fail_cooldown_ms->getValue();
    if (TimeUtil::GetCurrentMs() - last_fail_ms_.load() >= cooldown) {
        consecutive_fails_.store(0);
        return true;
    }
    return false;
}

void LoadBalanceItem::incFail()
{
    consecutive_fails_.fetch_add(1);
    last_fail_ms_.store(TimeUtil::GetCurrentMs());
}

void LoadBalanceItem::resetFail()
{
    consecutive_fails_.store(0);
}

std::string LoadBalanceItem::toString()
{
    std::stringstream ss;
    ss << "[Item id=" << id_ << " weight=" << getWeight();
    if (!stream_) {
        ss << " stream=null";
    } else {
        ss << " stream=[" << stream_->getRemoteAddressString() << " is_connected=" << stream_->isConnected() << "]";
    }
    ss << stats_.getTotal().toString() << "]";
    // float w = 0;
    // float w2 = 0;
    // for(uint64_t n = 0; n < 5; ++n) {
    //     if(n) {
    //         ss << ",";w
    //     } else {
    //         ss << stats_.get(time(0) - n).toString();
    //     }
    //     w += stats_.get(time(0) - n).getWeight();
    //     w2 += stats_.get(time(0) - n).getWeight() * (1 - n * 0.1);
    //     ss << stats_.get(time(0) - n).getWeight();
    // }
    // ss << " w=" << w;
    // ss << " w2=" << w2;
    return ss.str();
}

LoadBalanceItem::ptr LoadBalance::getById(uint64_t id)
{
    std::shared_lock lock(mutex_);
    auto it = datas_.find(id);
    return it == datas_.end() ? nullptr : it->second;
}

void LoadBalance::add(LoadBalanceItem::ptr v)
{
    std::unique_lock lock(mutex_);
    datas_[v->getId()] = v;
    initNolock();
}

void LoadBalance::del(LoadBalanceItem::ptr v)
{
    std::unique_lock lock(mutex_);
    datas_.erase(v->getId());
    initNolock();
}

void LoadBalance::update(const std::unordered_map<uint64_t, LoadBalanceItem::ptr>& adds, std::unordered_map<uint64_t, LoadBalanceItem::ptr>& dels)
{
    std::unique_lock lock(mutex_);
    for (auto& i : dels) {
        auto it = datas_.find(i.first);
        if (it != datas_.end()) {
            i.second = it->second;
            datas_.erase(it);
        }
    }
    for (auto& i : adds) {
        datas_[i.first] = i.second;
    }
    initNolock();
}

void LoadBalance::set(const std::vector<LoadBalanceItem::ptr>& vs)
{
    std::unique_lock lock(mutex_);
    datas_.clear();
    for (auto& i : vs) {
        datas_[i->getId()] = i;
    }
    initNolock();
}

void LoadBalance::init()
{
    std::unique_lock lock(mutex_);
    initNolock();
}

std::string LoadBalance::statusString(const std::string& prefix)
{
    std::shared_lock lock(mutex_);
    decltype(datas_) datas = datas_;
    lock.unlock();
    std::stringstream ss;
    ss << prefix << "init_time: " << TimeUtil::Time2StrMs(last_init_time_, "%Y-%m-%d %H:%M:%S") << std::endl;
    for (auto& i : datas) {
        ss << prefix << i.second->toString() << std::endl;
    }
    return ss.str();
}

void LoadBalance::checkInit()
{
    uint64_t ts = TimeUtil::GetCurrentMs();
    if (ts - last_init_time_ > 500) {
        init();
        last_init_time_ = ts;
    }
}

void RoundRobinLoadBalance::initNolock()
{
    decltype(items_) items;
    for (auto& i : datas_) {
        if (i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(items_);
}

LoadBalanceItem::ptr RoundRobinLoadBalance::get(uint64_t v)
{
    checkInit();
    std::shared_lock lock(mutex_);
    if (items_.empty()) {
        return nullptr;
    }
    uint32_t r = (v == (uint64_t)-1 ? rand() : v) % items_.size();
    for (size_t i = 0; i < items_.size(); ++i) {
        auto& h = items_[(r + i) % items_.size()];
        if (h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

FairLoadBalanceItem::ptr WeightLoadBalance::getAsFair()
{
    auto item = get();
    if (item) {
        return std::static_pointer_cast<FairLoadBalanceItem>(item);
    }
    return nullptr;
}

LoadBalanceItem::ptr WeightLoadBalance::get(uint64_t v)
{
    checkInit();
    std::shared_lock lock(mutex_);
    if (items_.empty()) {
        return nullptr;
    }

    // 只在可用实例中按权重做前缀和随机，避免“选中后再找下一个可用项”导致权重失真
    std::vector<std::pair<LoadBalanceItem::ptr, int64_t>> items;
    int64_t total = 0;
    for (auto& item : items_) {
        if (!item->isValid()) {
            continue;
        }
        int64_t weight = item->getWeight();
        if (weight < 1) {
            weight = 1;
        }
        items.emplace_back(item, weight);
        total += weight;
    }
    if (items.empty() || total <= 0) {
        return nullptr;
    }

    uint64_t dis = (v == (uint64_t)-1 ? RandomUtil::RandUint64(0, (uint64_t)total - 1) : v % (uint64_t)total);
    for (auto& [item, weight] : items) {
        if (dis < (uint64_t)weight) {
            return item;
        }
        dis -= (uint64_t)weight;
    }
    return items.back().first;
}

void WeightLoadBalance::initNolock()
{
    decltype(items_) items;
    for (auto& i : datas_) {
        if (i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(items_);
}

void HolderStats::clear()
{
    used_time_ = 0;
    total_ = 0;
    doing_ = 0;
    timeouts_ = 0;
    oks_ = 0;
    errs_ = 0;
}

float HolderStats::getWeight(float rate)
{
    // if(total_ == 0) {
    //     return 0.1;
    // }
    float base = total_ + 20;
    return std::min((oks_ * 1.0 / (used_time_ + 1)) * 2.0, 50.0) * (1 - 4.0 * timeouts_ / base)
        * (1 - 1 * doing_ / base) * (1 - 10.0 * errs_ / base) * rate;
    // return std::min((oks_ * 1.0 / (used_time_ + 1)) * 10.0, 100.0)
    //     * (1 - (2.0 * pow(timeouts_, 1.3) / base))
    //     * (1 - (1.0 * pow(doing_, 1.1) / base))
    //     * (1 - (4.0 * pow(errs_, 1.5) / base)) * rate;
    // return std::min(((oks_ + 1) * 1.0 / (used_time_ + 1)) * 10.0, 100.0)
    //     * std::min((base / (timeouts_ * 3.0 + 1)) / 100.0, 10.0)
    //     * std::min((base / ( doing_ * 1.0 + 1)) / 100.0, 10.0)
    //     * std::min((base / (errs_ * 5.0 + 1)) / 100.0, 10.0);
}

HolderStatsSet::HolderStatsSet(uint32_t size)
{
    stats_.resize(size);
}

void HolderStatsSet::init(const uint32_t& now)
{
    if (last_update_time_ < now) {
        for (uint32_t t = last_update_time_ + 1, i = 0; t <= now && i < stats_.size(); ++t, ++i) {
            stats_[t % stats_.size()].clear();
        }
        last_update_time_ = now;
    }
}

HolderStats& HolderStatsSet::get(const uint32_t& now)
{
    init(now);
    return stats_[now % stats_.size()];
}

float HolderStatsSet::getWeight(const uint32_t& now)
{
    init(now);
    float v = 0;
    for (size_t i = 1; i < stats_.size(); ++i) {
        v += stats_[(now - i) % stats_.size()].getWeight(1 - 0.1 * i);
    }
    return v;
    // return getTotal().getWeight(1.0);
}

int32_t FairLoadBalanceItem::getWeight()
{
    int32_t v = weight_ * stats_.getWeight();
    if (stream_->isConnected()) {
        return v > 1 ? v : 1;
    }
    return 1;
}

HolderStats& LoadBalanceItem::get(const uint32_t& now)
{
    return stats_.get(now);
}

// LoadBalanceItem::ptr FairLoadBalance::get() {
//     std::shared_lock lock(mutex_);
//     int32_t idx = getIdx();
//     if(idx == -1) {
//         return nullptr;
//     }
//
//     //TODO fix weight
//     for(size_t i = 0; i < items_.size(); ++i) {
//         auto& h = items_[(idx + i) % items_.size()];
//         if(h->isValid()) {
//             return h;
//         }
//     }
//     return nullptr;
// }
//
// void FairLoadBalance::initNolock() {
//     decltype(items_) items;
//     for(auto& i : datas_){
//         items.push_back(i.second);
//     }
//     items.swap(items_);
//
//     m_weights.resize(items_.size());
//     int32_t total = 0;
//     for(size_t i = 0; i < items_.size(); ++i) {
//         total += items_[i]->getWeight();
//         m_weights[i] = total;
//     }
// }
//
// int32_t FairLoadBalance::getIdx() {
//     if(m_weights.empty()) {
//         return -1;
//     }
//     int32_t total = *m_weights.rbegin();
//     auto it = std::upper_bound(m_weights.begin()
//                 ,m_weights.end(), rand() % total);
//     return std::distance(it, m_weights.begin());
// }

SDLoadBalance::SDLoadBalance(IServiceDiscovery::ptr sd)
    : sd_(sd)
{
}

LoadBalance::ptr SDLoadBalance::get(const std::string& domain, const std::string& service, bool auto_create)
{
    do {
        std::shared_lock lock(mutex_);
        auto it = datas_.find(domain);
        if (it == datas_.end()) {
            break;
        }
        auto iit = it->second.find(service);
        if (iit == it->second.end()) {
            break;
        }
        return iit->second;
    } while (0);

    if (!auto_create) {
        return nullptr;
    }

    auto type = getType(domain, service);

    auto lb = createLoadBalance(type);
    std::unique_lock lock(mutex_);
    datas_[domain][service] = lb;
    lock.unlock();
    return lb;
}

ILoadBalance::Type SDLoadBalance::getType(const std::string& domain, const std::string& service)
{
    std::shared_lock lock(mutex_);
    auto it = types_.find(domain);
    if (it == types_.end()) {
        return default_type_;
    }
    auto iit = it->second.find(service);
    if (iit == it->second.end()) {
        return default_type_;
    }
    return iit->second;
}

LoadBalance::ptr SDLoadBalance::createLoadBalance(ILoadBalance::Type type)
{
    if (type == ILoadBalance::ROUNDROBIN) {
        return RoundRobinLoadBalance::ptr(new RoundRobinLoadBalance);
    } else if (type == ILoadBalance::WEIGHT) {
        return WeightLoadBalance::ptr(new WeightLoadBalance);
    } else if (type == ILoadBalance::FAIR) {
        return WeightLoadBalance::ptr(new WeightLoadBalance);
    }
    return nullptr;
}

LoadBalanceItem::ptr SDLoadBalance::createLoadBalanceItem(ILoadBalance::Type type)
{
    LoadBalanceItem::ptr item;
    if (type == ILoadBalance::ROUNDROBIN) {
        item.reset(new LoadBalanceItem);
    } else if (type == ILoadBalance::WEIGHT) {
        item.reset(new LoadBalanceItem);
    } else if (type == ILoadBalance::FAIR) {
        item.reset(new FairLoadBalanceItem);
    }
    return item;
}

void SDLoadBalance::onServiceChange(
    const std::string& domain,
    const std::string& service,
    const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value,
    const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value)
{
    INFO(logger) << "onServiceChange domain=" << domain << " service=" << service;
    auto type = getType(domain, service);
    auto lb = get(domain, service, true);
    std::unordered_map<uint64_t, ServiceItemInfo::ptr> add_values;
    std::unordered_map<uint64_t, LoadBalanceItem::ptr> del_infos;

    for (auto& i : old_value) {
        if (new_value.find(i.first) == new_value.end()) {
            del_infos[i.first];
        }
    }
    for (auto& i : new_value) {
        if (old_value.find(i.first) == old_value.end()) {
            add_values.insert(i);
        }
    }

    std::unordered_map<uint64_t, LoadBalanceItem::ptr> add_infos;
    for (auto& i : add_values) {
        auto stream = cb_(i.second);
        if (!stream) {
            ERROR(logger) << "create stream fail, " << i.second->toString();
            continue;
        }

        LoadBalanceItem::ptr lditem = createLoadBalanceItem(type);
        lditem->setId(i.first);
        lditem->setStream(stream);
        int32_t weight = (int32_t)i.second->getWeight();
        lditem->setWeight(weight > 0 ? weight : 1);

        add_infos[i.first] = lditem;
    }

    lb->update(add_infos, del_infos);
    for (auto& i : del_infos) {
        if (i.second) {
            i.second->close();
        }
    }
}

void SDLoadBalance::start()
{
    sd_->setServiceCallback(std::bind(&SDLoadBalance::onServiceChange, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    sd_->start();
}

void SDLoadBalance::stop()
{
    sd_->stop();
}

void SDLoadBalance::initConf(const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& confs)
{
    decltype(types_) types;
    std::unordered_map<std::string, std::unordered_set<std::string>> query_infos;
    for (auto& i : confs) {
        for (auto& n : i.second) {
            ILoadBalance::Type t = ILoadBalance::FAIR;
            if (n.second == "round_robin") {
                t = ILoadBalance::ROUNDROBIN;
            } else if (n.second == "weight") {
                t = ILoadBalance::WEIGHT;
            }
            types[i.first][n.first] = t;
            query_infos[i.first].insert(n.first);
        }
    }
    sd_->setQueryServer(query_infos);
    std::unique_lock lock(mutex_);
    types.swap(types_);
    lock.unlock();
}

std::string SDLoadBalance::statusString()
{
    std::shared_lock lock(mutex_);
    decltype(datas_) datas = datas_;
    lock.unlock();
    std::stringstream ss;
    for (auto& i : datas) {
        ss << i.first << ":" << std::endl;
        for (auto& n : i.second) {
            ss << "\t" << n.first << ":" << std::endl;
            ss << n.second->statusString("\t\t") << std::endl;
        }
    }
    return ss.str();
}

} // namespace chen
