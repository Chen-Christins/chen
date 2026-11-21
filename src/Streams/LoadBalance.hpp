/**
 * @file LoadBalance.hpp
 * @brief load balance
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-26
 * @copyright GPL-3.0
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <shared_mutex>

#include "../Socket/SocketStream.hpp"
#include "../Streams/ServiceDiscovery.hpp"
#include "../Util/Util.hpp"

namespace chen {

class HolderStatsSet;
class HolderStats {
    friend class HolderStatsSet;

public:
    uint32_t getUsedTime() const { return used_time_; }
    uint32_t getTotal() const { return total_; }
    uint32_t getDoing() const { return doing_; }
    uint32_t getTimeouts() const { return timeouts_; }
    uint32_t getOks() const { return oks_; }
    uint32_t getErrs() const { return errs_; }

    uint32_t incUsedTime(uint32_t v) { return Atomic::addFetch(used_time_, v); }
    uint32_t incTotal(uint32_t v) { return Atomic::addFetch(total_, v); }
    uint32_t incDoing(uint32_t v) { return Atomic::addFetch(doing_, v); }
    uint32_t incTimeouts(uint32_t v) { return Atomic::addFetch(timeouts_, v); }
    uint32_t incOks(uint32_t v) { return Atomic::addFetch(oks_, v); }
    uint32_t incErrs(uint32_t v) { return Atomic::addFetch(errs_, v); }

    uint32_t decDoing(uint32_t v) { return Atomic::subFetch(doing_, v); }
    void clear();

    float getWeight(float rate = 1.0f);

    std::string toString();

private:
    uint32_t used_time_ = 0;
    uint32_t total_ = 0;
    uint32_t doing_ = 0;
    uint32_t timeouts_ = 0;
    uint32_t oks_ = 0;
    uint32_t errs_ = 0;
};

class HolderStatsSet {
public:
    HolderStatsSet(uint32_t size = 5);
    HolderStats& get(const uint32_t& now = time(0));

    float getWeight(const uint32_t& now = time(0));

    HolderStats getTotal();

private:
    void init(const uint32_t& now);

private:
    uint32_t last_update_time_ = 0; // seconds
    std::vector<HolderStats> stats_;
};

class LoadBalanceItem {
public:
    typedef std::shared_ptr<LoadBalanceItem> ptr;
    virtual ~LoadBalanceItem();

    SocketStream::ptr getStream() const { return stream_; }
    void setStream(SocketStream::ptr v) { stream_ = v; }

    void setId(uint64_t v) { id_ = v; }
    uint64_t getId() const { return id_; }

    HolderStats& get(const uint32_t& now = time(0));

    template <class T>
    std::shared_ptr<T> getStreamAs()
    {
        return std::dynamic_pointer_cast<T>(stream_);
    }

    virtual int32_t getWeight() { return weight_; }
    void setWeight(int32_t v) { weight_ = v; }

    virtual bool isValid();
    void close();

    /**
     * @brief 记录一次请求失败（用于被动摘除）
     */
    void incFail();

    /**
     * @brief 请求成功后清除失败计数
     */
    void resetFail();

    std::string toString();

protected:
    uint64_t id_ = 0;
    SocketStream::ptr stream_;
    int32_t weight_ = 0;
    HolderStatsSet stats_;
    /// 连续失败次数
    std::atomic<uint32_t> consecutive_fails_{0};
    /// 最近一次失败时间（毫秒）
    std::atomic<uint64_t> last_fail_ms_{0};
};

class ILoadBalance {
public:
    enum Type {
        ROUNDROBIN = 1,
        WEIGHT = 2,
        FAIR = 3
    };

    enum Error {
        NO_SERVICE = -101,
        NO_CONNECTION = -102,
    };
    typedef std::shared_ptr<ILoadBalance> ptr;
    virtual ~ILoadBalance();
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) = 0;
};

class LoadBalance : public ILoadBalance {
public:
    typedef std::shared_ptr<LoadBalance> ptr;
    void add(LoadBalanceItem::ptr v);
    void del(LoadBalanceItem::ptr v);
    void set(const std::vector<LoadBalanceItem::ptr>& vs);

    LoadBalanceItem::ptr getById(uint64_t id);
    void update(const std::unordered_map<uint64_t, LoadBalanceItem::ptr>& adds, std::unordered_map<uint64_t, LoadBalanceItem::ptr>& dels);
    void init();

    std::string statusString(const std::string& prefix);

protected:
    virtual void initNolock() = 0;
    void checkInit();

protected:
    std::shared_mutex mutex_;
    std::unordered_map<uint64_t, LoadBalanceItem::ptr> datas_;
    uint64_t last_init_time_ = 0;
};

class RoundRobinLoadBalance : public LoadBalance {
public:
    typedef std::shared_ptr<RoundRobinLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) override;

protected:
    virtual void initNolock() override;

protected:
    std::vector<LoadBalanceItem::ptr> items_;
};

// class FairLoadBalance;
class FairLoadBalanceItem : public LoadBalanceItem {
    // friend class FairLoadBalance;
public:
    typedef std::shared_ptr<FairLoadBalanceItem> ptr;

    void clear();
    virtual int32_t getWeight();
};

class WeightLoadBalance : public LoadBalance {
public:
    typedef std::shared_ptr<WeightLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = -1) override;

    FairLoadBalanceItem::ptr getAsFair();

protected:
    virtual void initNolock() override;

protected:
    std::vector<LoadBalanceItem::ptr> items_;
};

// class FairLoadBalance : public LoadBalance {
// public:
//     typedef std::shared_ptr<FairLoadBalance> ptr;
//     virtual LoadBalanceItem::ptr get() override;
//     FairLoadBalanceItem::ptr getAsFair();
//
// protected:
//     virtual void initNolock();
// private:
//     int32_t getIdx();
// protected:
//     std::vector<LoadBalanceItem::ptr> items_;
// private:
//     std::vector<int32_t> m_weights;
// };

class SDLoadBalance {
public:
    typedef std::shared_ptr<SDLoadBalance> ptr;
    typedef std::function<SocketStream::ptr(ServiceItemInfo::ptr)> stream_callback;

    SDLoadBalance(IServiceDiscovery::ptr sd);
    virtual ~SDLoadBalance();

    virtual void start();
    virtual void stop();

    stream_callback getCb() const { return cb_; }
    void setCb(stream_callback v) { cb_ = v; }

    LoadBalance::ptr get(const std::string& domain, const std::string& service, bool auto_create = false);

    void initConf(const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& confs);

    std::string statusString();

private:
    void onServiceChange(
        const std::string& domain,
        const std::string& service,
        const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& old_value,
        const std::unordered_map<uint64_t, ServiceItemInfo::ptr>& new_value);

    ILoadBalance::Type getType(const std::string& domain, const std::string& service);
    LoadBalance::ptr createLoadBalance(ILoadBalance::Type type);
    LoadBalanceItem::ptr createLoadBalanceItem(ILoadBalance::Type type);

protected:
    std::shared_mutex mutex_;
    IServiceDiscovery::ptr sd_;
    // domain -> [ service -> [ LoadBalance ] ]
    std::unordered_map<std::string, std::unordered_map<std::string, LoadBalance::ptr>> datas_;
    std::unordered_map<std::string, std::unordered_map<std::string, ILoadBalance::Type>> types_;
    ILoadBalance::Type default_type_ = ILoadBalance::FAIR;
    stream_callback cb_;
};

} // namespace chen
