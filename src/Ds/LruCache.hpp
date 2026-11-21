/**
 * @file LruCache.hpp
 * @brief LRU 缓存模板
 * @author Christins
 * @date 2026-10-01
 * @copyright GPL-3.0
 *
 * 提供两个主要模板：
 * - LruCache<K,V>：线程安全的最近最少使用（LRU）缓存，使用双向链表维护访问顺序，
 *   通过 unordered_map 保存键到链表迭代器的映射以实现 O(1) 访问/插入/删除。
 * - HashLruCache<K,V,Hash>：将多个 LruCache 分片（bucket）以降低锁争用的包装器。
 *
 * 缓存支持最大容量（max_size）与弹性大小（elasticity），允许在达到上限前短暂超配。
 * 可选的 CacheStatus 指针用于统计命中/访问/删除/修剪等信息。
 */
#pragma once

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "CacheStatus.hpp"

namespace chen {
namespace ds {

/**
 * @brief 线程安全的 LRU（最近最少使用）缓存实现。
 *
 * 使用 std::list 保存键值对以维护最近使用顺序，使用 std::unordered_map
 * 将键映射到链表迭代器以实现常数时间查找、移动与删除。
 * 内部通过 std::mutex 保护所有公有方法以保证并发安全。
 *
 * @tparam K 键类型，应可哈希并可用于 unordered_map。
 * @tparam V 值类型，以值方式存储在容器中。
 */
template <class K, class V>
class LruCache {
public:
    typedef std::shared_ptr<LruCache> ptr;

    // 双向链表类型，存储键值对
    typedef std::list<std::pair<K, V>> list_type;
    // 哈希表类型，键映射到链表迭代器
    typedef std::unordered_map<K, typename list_type::iterator> map_type;
    // 回调函数类型，用于缓存淘汰时调用
    typedef std::function<void(const K&, const V&)> prune_callback;

    /**
     * @brief 构造函数
     * @param max_size 最大容量
     * @param elasticity 弹性大小
     * @param status 缓存状态指针
     */
    LruCache(size_t max_size = 0, size_t elasticity = 0, CacheStatus* status = nullptr)
        : max_size_(max_size)
        , elasticity_(elasticity)
    {
        status_ = status;

        if (status_ == nullptr) {
            status_ = new CacheStatus;
            status_owner_ = true;
        }
    }

    // 析构函数，用于释放LruCache对象占用的资源
    ~LruCache()
    {
        // 检查m_statusOwner和m_status是否有效
        if (status_owner_ && status_) {
            // 如果m_statusOwner为true，表示当前对象拥有m_status的所有权，需要释放内存
            delete status_;
        }
    }

    /**
     * 设置键值对到缓存中
     * @param k 键值
     * @param v 要设置的值
     */
    void set(const K& k, const V& v)
    {
        // 增加设置操作计数
        status_->incSet();
        // 使用unique_lock对互斥量进行加锁，保证线程安全
        std::unique_lock lock(mutex_);
        // 在缓存中查找键k
        auto it = cache_.find(k);

        // 如果键k已存在于缓存中
        if (it != cache_.end()) {
            // 更新对应的值
            it->second->second = v;
            // 将该键值对移动到链表头部（表示最近使用）
            keys_.splice(keys_.begin(), keys_, it->second);
            return;
        }

        // 如果键k不存在，创建新的键值对并添加到链表头部
        keys_.emplace_front(std::pair(k, v));
        cache_.insert(std::pair(k, keys_.begin()));
        prune();
    }

    /**
     * @brief 从缓存中获取指定键的值
     * @param k 键值
     * @param v 获取的值
     * @return bool 如果成功获取返回true，如果键不存在返回false
     */
    bool get(const K& k, V& v)
    {
        status_->incGet();             //  增加获取次数计数
        std::unique_lock lock(mutex_); //  使用unique_lock获取互斥锁，确保线程安全
        auto it = cache_.find(k);      //  在缓存中查找键k
        if (it == cache_.end()) {      //  如果未找到键，返回false
            return false;
        }
        keys_.splice(keys_.begin(), keys_, it->second);    //  将访问的键值对移动到链表头部，表示最近使用
        v = it->second->second;                            //  获取对应的值
        lock.unlock();                                     //  提前释放锁
        status_->incHit();                                 //  增加命中次数计数
        return true;                                       //  返回true表示命中缓存
    }

    /**
     * 从缓存中获取指定键的值
     * @param k 要获取的键
     * @return 返回与键关联的值，如果键不存在则返回默认构造的值
     */
    V get(const K& k)
    {
        // 增加获取次数计数
        status_->incGet();
        // 使用互斥锁保证线程安全
        std::unique_lock lock(mutex_);
        // 在缓存中查找键
        auto it = cache_.find(k);
        // 如果键不存在，返回默认构造的值
        if (it == cache_.end()) {
            return V();
        }
        // 将访问的键值对移动到链表头部，表示最近使用
        keys_.splice(keys_.begin(), keys_, it->second);
        // 获取值
        auto v = it->second->second;
        // 提前释放锁
        lock.unlock();
        // 增加命中次数计数
        status_->incHit();
        return v;
    }

    /**
     * 从缓存中删除指定键的元素
     * @param k 要删除的键
     * @return 如果成功删除返回true，如果键不存在返回false
     */
    bool del(const K& k)
    {
        status_->incDel();             // 增加删除计数器的值
        std::unique_lock lock(mutex_); // 获取互斥锁，确保线程安全
        auto it = cache_.find(k);      // 在缓存中查找键
        if (it == cache_.end()) {      // 如果键不存在
            return false;
        }
        keys_.erase(it->second); // 从键集合中删除对应的键值
        cache_.erase(it);        // 从缓存中删除该元素
        return true;              // 删除成功返回true
    }

    /**
     * 检查键值k是否存在于缓存中
     * @param k 要检查的键值
     * @return 如果键值存在返回true，否则返回false
     */
    bool exists(const K& k)
    {
        // 使用unique_lock对互斥量进行加锁，确保线程安全
        std::unique_lock lock(mutex_);
        // 在缓存中查找键值k，如果找到返回迭代器，否则返回cache_.end()
        // 通过比较结果是否等于cache_.end()来判断键值是否存在
        return cache_.find(k) != cache_.end();
    }

    /**
     * 获取缓存中元素的数量
     * @return 返回缓存中当前存储的元素数量
     */
    size_t size()
    {
        // 使用std::unique_lock对互斥量m_mutex进行加锁
        // unique_lock提供了自动加锁和解锁的功能，在作用域结束时自动释放锁
        std::unique_lock lock(mutex_);
        // 返回缓存容器的大小
        return cache_.size();
    }

    /**
     * 检查缓存是否为空
     * @return 如果缓存为空返回true，否则返回false
     * 该函数使用互斥锁确保线程安全
     */
    bool empty()
    {
        std::unique_lock lock(mutex_); // 使用unique_lock自动管理互斥锁的生命周期
        return cache_.empty();         // 返回缓存是否为空的状态
    }

    /**
     * 清空缓存的所有内容
     * @return 返回true表示清空操作成功
     */
    bool clear()
    {
        // 使用unique_lock对互斥量m_mutex进行加锁，确保线程安全
        std::unique_lock lock(mutex_);
        // 清空缓存数据容器
        cache_.clear();
        // 清空键值容器
        keys_.clear();
        // 操作成功，返回true
        return true;
    }

    /**
     * 设置最大容量大小的成员函数
     * @param v 要设置的最大容量值，类型为size_t的常量引用
     */
    void setMaxSize(const size_t& v) { max_size_ = v; }
    /**
     * 设置弹性值的成员函数
     * @param v 要设置的弹性值，使用size_t类型的常量引用传递
     */
    void setElasticity(const size_t& v) { elasticity_ = v; }

    /**
     * 获取容器最大容量的函数
     * @return 返回容器的最大容量大小，类型为size_t
     */
    size_t getMaxSize() const { return max_size_; }
    /**
     * @brief 获取弹性值
     * @return 返回当前对象的弹性值，类型为size_t
     */
    size_t getElasticity() const { return elasticity_; }
    /**
     * 获取允许的最大尺寸
     * @return 返回最大允许的尺寸，包括基础尺寸和弹性尺寸
     */
    size_t getMaxAllowedSize() const { return max_size_ + elasticity_; }

    /**
     * 遍历缓存中的所有元素并对每个元素执行指定操作
     * @param f 应用于每个缓存元素的函数对象（函数或lambda表达式）
     * 使用std::unique_lock确保线程安全，在遍历期间保持互斥锁
     */
    template <class F>
    void foreach(F& f)
    {
        // 使用unique_lock锁定互斥锁，确保线程安全
        std::unique_lock lock(mutex_);
        // 使用标准库的for_each算法遍历缓存容器m_cache
        // 对每个元素应用函数对象f
        std::for_each(cache_.begin(), cache_.end(), f);
    }

    /**
     * @brief 设置剪枝回调函数
     * @param cb 剪枝回调函数，用于在剪枝过程中执行自定义操作
     * @note 该函数用于设置成员变量 cb_ 的值，cb_ 是一个函数指针，指向用户定义的剪枝回调函数
     */
    void setPruneCallback(prune_callback cb) { cb_ = cb; }

    /**
     * 将状态信息转换为字符串表示
     * @return 返回包含状态信息和总数的字符串
     */
    std::string toStatusString()
    {
        // 使用字符串流来构建结果字符串
        std::stringstream ss;
        // 如果有状态对象，则调用其toString方法，否则显示"(no status)"
        // 然后添加总数信息
        ss << (status_ ? status_->toString() : "(no status)") << " total=" << size();
        return ss.str();
    }

    /**
     * 获取缓存状态指针
     * @return 返回指向CacheStatus类型对象的指针，表示当前缓存的状态
     */
    CacheStatus* getStatus() const { return status_; }

    /**
     * 设置缓存状态的方法
     * @param v 指向CacheStatus对象的指针
     * @param owner 标记调用者是否为该状态对象的拥有者，默认为false
     */
    void setStatus(CacheStatus* v, bool owner = false)
    {
        // 如果当前状态对象拥有所有权且存在，则先删除它
        if (status_owner_ && status_) {
            delete status_;
        }

        // 设置新的状态对象和所有权标记
        status_ = v;
        status_owner_ = owner;

        // 如果传入的状态指针为空，则创建一个新的CacheStatus对象，并设置拥有者为true
        if (status_ == nullptr) {
            status_ = new CacheStatus;
            status_owner_ = true;
        }
    }

protected:
    /**
     * @brief 修剪缓存，当缓存大小超过最大允许值时移除最旧的条目
     * @return size_t 被移除的条目数量
     */
    size_t prune()
    {
        // 如果最大大小为0或当前缓存大小未超过允许大小，则无需修剪
        if (max_size_ == 0 || cache_.size() < getMaxAllowedSize()) {
            return 0;
        }

        size_t count = 0; // 记录被移除的条目数量
        // 当缓存大小超过最大限制时，持续移除最旧的条目
        while (cache_.size() > max_size_) {
            auto& back = keys_.back(); // 获取键列表中的最后一个元素（最旧的条目）
            // 如果设置了回调函数，则在删除前调用它
            if (cb_) {
                cb_(back.first, back.second);
            }
            cache_.erase(back.first); // 从缓存中移除该条目
            keys_.pop_back();         // 从键列表中移除最后一个元素
            ++count;                   // 增加移除计数
        }
        status_->incPrune(count); // 更新修剪状态信息
        return count;              // 返回被移除的条目总数
    }

private:
    // 互斥锁，用于保证线程安全
    std::mutex mutex_;
    // 缓存数据，使用map_type类型（可能是std::map或std::unordered_map）
    map_type cache_;
    // 键列表，用于记录缓存项的访问顺序，可能是LRU算法实现的一部分
    list_type keys_;
    // 缓存的最大容量限制
    size_t max_size_;
    // 缓存的弹性大小，可能用于在接近最大容量时决定是否淘汰旧项
    size_t elasticity_;
    // 回调函数指针，用于在缓存项被淘汰时执行自定义操作
    prune_callback cb_;
    // 指向缓存状态对象的指针，可能用于监控缓存状态
    CacheStatus* status_ = nullptr;
    // 标记是否拥有m_status对象的所有权，如果为true则负责释放该对象
    bool status_owner_ = false;
};

/**
 * @brief 分片的哈希 LRU 缓存包装器（降低锁竞争）。
 *
 * HashLruCache 将键空间哈希到多个独立的 LruCache 桶中，每个桶有自己的锁，
 * 从而在高并发场景下减少单个锁的争用。整体最大容量与弹性通过将每个桶
 * 的容量累加得到。
 *
 * @tparam K 键类型。
 * @tparam V 值类型。
 * @tparam Hash 哈希函数类型，默认使用 std::hash<K>。
 */
template <class K, class V, class Hash = std::hash<K>>
class HashLruCache {
public:
    typedef std::shared_ptr<HashLruCache> ptr; // 智能指针类型定义，用于管理HashLruCache对象的生命周期
    typedef LruCache<K, V> cache_type;         // 类型别名，表示LRU缓存的具体类型

    HashLruCache(size_t bucket, size_t max_size, size_t elasticity)
        : bucket_(bucket)
    {
        datas_.resize(bucket); //  调整缓存桶的大小

        //  计算每个缓存桶的最大大小和弹性值 使用ceil函数确保向上取整，保证分配足够的空间
        size_t pre_max_size = std::ceil(max_size * 1.0 / bucket);
        size_t pre_elasticity = std::ceil(elasticity * 1.0 / bucket);
        max_size_ = pre_max_size * bucket; //  计算并设置全局的最大大小和弹性值     这里乘以bucket是为了保持总量不变
        elasticity_ = pre_elasticity * bucket;

        for (size_t i = 0; i < bucket; ++i) { //  初始化每个缓存桶     为每个桶分配新的缓存对象
            datas_[i] = new cache_type(pre_max_size, pre_elasticity, &status_);
        }
    }

    // 析构函数，用于释放哈希LRU缓存中所有元素的内存
    ~HashLruCache()
    {
        // 遍历存储数据的容器
        for (size_t i = 0; i < datas_.size(); ++i) {
            // 释放每个元素占用的内存空间
            delete datas_[i];
        }
    }

    /**
     * 设置键值对的函数
     * @param k 键值
     * @param v 与键关联的值
     */
    void set(const K& k, const V& v)
    {
        // 计算键的哈希值，并对桶数量取模，确定数据存储位置
        // 然后调用对应桶的set方法设置键值对
        datas_[hash_(k) % bucket_]->set(k, v);
    }

    bool get(const K& k, V& v) { return datas_[hash_(k) % bucket_]->get(k, v); }

    /**
     * 从容器中获取键对应的值
     * @param k 要查找的键
     * @return 返回键对应的值
     */
    V get(const K& k)
    {
        // 使用哈希函数计算键的位置，然后对桶数量取模得到桶的索引
        // 从对应的桶中获取键对应的值
        return datas_[hash_(k) % bucket_]->get(k);
    }

    /**
     * 删除键为k的元素
     * @param k 要删除的元素的键
     * @return 删除操作是否成功
     */
    bool del(const K& k)
    {
        // 计算键k的哈希值，并对桶数量取模，得到在m_datas中的索引
        // 然后调用对应桶的del方法执行删除操作
        return datas_[hash_(k) % bucket_]->del(k);
    }

    /**
     * 检查键k是否存在于哈希表中
     * @param k 要查找的键
     * @return 如果键存在返回true，否则返回false
     */
    bool exists(const K& k)
    {
        // 通过哈希函数计算键的位置，然后取模确定桶的位置
        // 调用对应桶中的链表/树形结构的exists方法进行查找
        return datas_[hash_(k) % bucket_]->exists(k);
    }

    /**
     * 获取容器中所有元素的总大小
     * @return 返回容器中所有元素大小的总和
     */
    size_t size()
    {
        // 用于存储总大小的变量
        size_t total = 0;
        // 遍历容器中的每个元素
        for (auto& i : datas_) {
            // 累加每个元素的大小
            total += i->size();
        }
        // 返回计算得到的总大小
        return total;
    }

    /**
     * 检查容器是否为空
     * 遍历容器中的所有元素，如果存在任何非空元素则返回false，否则返回true
     * @return 如果容器中所有元素都为空则返回true，否则返回false
     */
    bool empty()
    {
        // 遍历容器m_datas中的每个元素
        for (auto& i : datas_) {
            // 检查当前元素是否为空
            if (!i->empty()) {
                // 如果发现任何非空元素，立即返回false
                return false;
            }
        }
        // 如果所有元素都为空，则返回true
        return true;
    }

    /**
     * 清空容器中的所有数据
     * 遍历容器中的每个元素，并调用其clear()方法进行清空操作
     */
    void clear()
    {
        // 使用基于范围的for循环遍历容器m_datas中的每个元素
        for (auto& i : datas_) {
            // 调用当前元素的clear()方法，清空其内部数据
            i->clear();
        }
    }

    /**
     * 获取容器最大容量的函数
     * @return 返回容器的最大容量大小，类型为size_t
     */
    size_t getMaxSize() const { return max_size_; }
    /**
     * 获取弹性值
     * @return 返回当前对象的弹性值大小
     */
    size_t getElasticity() const { return elasticity_; }
    /**
     * 获取允许的最大大小
     * @return 返回当前允许的最大大小，包括基础大小和弹性大小
     */
    size_t getMaxAllowedSize() const { return max_size_ + elasticity_; }
    /**
     * 获取当前对象的桶索引值
     * @return 返回桶索引值，类型为size_t
     */
    size_t getBucket() const { return bucket_; }

    /**
     * 设置最大大小函数
     * @param v 期望设置的最大大小值
     * 该函数会根据传入的值和桶数量计算出实际的最大大小，并更新所有数据项的最大大小
     */
    void setMaxSize(const size_t& v)
    {
        // 计算每个桶的最大大小，使用向上取整确保足够的容量
        size_t pre_max_size = std::ceil(v * 1.0 / bucket_);
        // 计算实际的最大大小，确保是桶大小的整数倍
        max_size_ = pre_max_size * bucket_;
        // 遍历所有数据项，更新每个数据项的最大大小
        for (auto& i : datas_) {
            i->setMaxSize(pre_max_size);
        }
    }

    /**
     * 设置弹性值的方法
     * @param v 输入的弹性值参数
     */
    void setElasticity(const size_t& v)
    {
        // 计算每个桶的弹性值，通过将输入值v除以桶的数量m_bucket并向上取整
        size_t pre_elasticity = std::ceil(v * 1.0 / bucket_);
        // 计算总的弹性值，通过将每个桶的弹性值乘以桶的数量
        elasticity_ = pre_elasticity * bucket_;
        // 遍历所有数据项，为每个数据项设置弹性值
        for (auto& i : datas_) {
            i->setElasticity(pre_elasticity);
        }
    }

    /**
     * 遍历容器中的所有元素，并对每个元素执行指定的函数操作
     * @param f 要对每个元素执行的函数对象，该函数对象应该接受一个参数，即当前遍历到的元素
     */
    template <class F>
    void foreach(F& f)
    {
        // 使用范围for循环遍历容器m_datas中的所有元素
        for (auto& i : datas_) {
            // 对每个元素调用其自身的foreach方法，传入函数对象f
            // 这表明容器中的元素可能也是某种容器或具有foreach方法的对象
            i->foreach(f);
        }
    }

    /**
     * 设置修剪回调函数
     * 该函数用于为缓存中的所有数据项设置统一的修剪回调函数
     * @param cb 修剪回调函数，类型为cache_type的prune_callback
     */
    void setPruneCallback(typename cache_type::prune_callback cb)
    {
        // 遍历m_datas容器中的所有元素
        for (auto& i : datas_) {
            // 为每个数据项设置相同的修剪回调函数
            i->setPruneCallback(cb);
        }
    }

    /**
     * 获取缓存状态的指针
     * @return 返回指向CacheStatus类型成员变量m_status的指针
     */
    CacheStatus* getStatus()
    {
        return &status_; // 返回m_status的内存地址
    }

    /**
     * 将状态信息转换为字符串表示
     * @return 返回包含状态信息和总数的字符串
     */
    std::string toStatusString()
    {
        // 使用字符串流来构建结果字符串
        std::stringstream ss;
        // 将状态信息转换为字符串并添加到流中，同时添加总数信息
        ss << status_.toString() << " total=" << size();
        // 返回构建完成的字符串
        return ss.str();
    }

private:
    // 存储缓存数据的指针数组
    std::vector<cache_type*> datas_;
    // 缓存的最大容量
    size_t max_size_;
    // 缓存的桶数量，用于哈希映射
    size_t bucket_;
    // 缓存的弹性大小，表示可以超出最大容量的额外空间
    size_t elasticity_;
    // 哈希函数对象，用于计算缓存项的哈希值
    Hash hash_;
    // 缓存的状态，表示当前缓存的使用情况
    CacheStatus status_;
};

} // namespace ds
} // namespace chen
