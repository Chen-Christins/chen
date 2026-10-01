/**
 * @file cache_status.h
 * @brief 缓存状态统计
 */
#pragma once

#include <sstream>

#include "../util/util.h"

namespace chen {
namespace ds {

/**
 * @class CacheStatus
 * @brief 缓存统计信息结构，用于记录访问/设置/删除/超时/修剪和命中次数。
 *
 * 该类使用原子操作（通过项目中的 Atomic 辅助）累加/递减各类计数，
 * 并提供合并与格式化输出的能力，方便上报与监控。
 */
class CacheStatus {
public:
	/**
	 * @brief 默认构造函数，初始化所有计数为 0。
	 */
	CacheStatus() {}

	/**
	 * @brief 增加 get 请求计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incGet(int64_t v = 1) { return Atomic::addFetch(m_get, v); }

	/**
	 * @brief 增加 set 请求计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incSet(int64_t v = 1) { return Atomic::addFetch(m_set, v); }

	/**
	 * @brief 增加 del 请求计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incDel(int64_t v = 1) { return Atomic::addFetch(m_del, v); }

	/**
	 * @brief 增加 timeout（超时）计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incTimeout(int64_t v = 1) { return Atomic::addFetch(m_timeout, v); }

	/**
	 * @brief 增加 prune（修剪/淘汰）计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incPrune(int64_t v = 1) { return Atomic::addFetch(m_prune, v); }

	/**
	 * @brief 增加 hit（命中）计数
	 * @param v 增加的值，默认 1
	 * @return 增加后计数的值
	 */
	int64_t incHit(int64_t v = 1) { return Atomic::addFetch(m_hit, v); }

	/**
	 * @brief 减少 get 请求计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decGet(int64_t v = 1) { return Atomic::subFetch(m_get, v); }

	/**
	 * @brief 减少 set 请求计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decSet(int64_t v = 1) { return Atomic::subFetch(m_set, v); }

	/**
	 * @brief 减少 del 请求计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decDel(int64_t v = 1) { return Atomic::subFetch(m_del, v); }

	/**
	 * @brief 减少 timeout（超时）计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decTimeout(int64_t v = 1) { return Atomic::subFetch(m_timeout, v); }

	/**
	 * @brief 减少 prune（修剪/淘汰）计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decPrune(int64_t v = 1) { return Atomic::subFetch(m_prune, v); }

	/**
	 * @brief 减少 hit（命中）计数
	 * @param v 减少的值，默认 1
	 * @return 减少后计数的值
	 */
	int64_t decHit(int64_t v = 1) { return Atomic::subFetch(m_hit, v); }

	/**
	 * @brief 获取 get 请求计数（只读）
	 * @return 当前 get 计数
	 */
	int64_t getGet() const { return m_get; }

	/**
	 * @brief 获取 set 请求计数（只读）
	 * @return 当前 set 计数
	 */
	int64_t getSet() const { return m_set; }

	/**
	 * @brief 获取 del 请求计数（只读）
	 * @return 当前 del 计数
	 */
	int64_t getDel() const { return m_del; }

	/**
	 * @brief 获取 timeout（超时）计数（只读）
	 * @return 当前 timeout 计数
	 */
	int64_t getTimeout() const { return m_timeout; }

	/**
	 * @brief 获取 prune（修剪/淘汰）计数（只读）
	 * @return 当前 prune 计数
	 */
	int64_t getPrune() const { return m_prune; }

	/**
	 * @brief 获取 hit（命中）计数（只读）
	 * @return 当前 hit 计数
	 */
	int64_t getHit() const { return m_hit; }

	/**
	 * @brief 计算命中率
	 * @return 命中率（0.0 - 1.0），当 get 为 0 时返回 0
	 */
	double getHitRate() const {
		return m_get ? (m_hit * 1.0 / m_get) : 0;
	}

	/**
	 * @brief 合并另一个 CacheStatus 到当前对象中（累加每个计数）
	 * @param oth 要合并的另一个 CacheStatus 实例
	 */
	void merge(const CacheStatus& oth) {
		m_get += oth.m_get;
		m_set += oth.m_set;
		m_del += oth.m_del;
		m_timeout += oth.m_timeout;
		m_prune += oth.m_prune;
		m_hit += oth.m_hit;
	}

	/**
	 * @brief 将当前统计信息格式化为可打印字符串
	 * @return 格式化后的统计字符串（包含各计数与命中率）
	 */
	std::string toString() const {
		std::stringstream ss;
		ss << "get=" << m_get
		   << " set=" << m_set
		   << " del=" << m_del
		   << " prune=" << m_prune
		   << " timeout=" << m_timeout
		   << " hit=" << m_hit
		   << " hit_rate" << (getHitRate() * 100.0) << "%";
		return ss.str();
	}

private:
	/// 请求计数
	int64_t m_get = 0;
	/// 请求计数
	int64_t m_set = 0;
	/// 请求计数
	int64_t m_del = 0;
	/// （超时）计数
	int64_t m_timeout = 0;
	/// （修剪/淘汰）计数
	int64_t m_prune = 0;
	/// （命中）计数
	int64_t m_hit = 0;
};

inline std::ostream& operator<<(std::ostream& os, const CacheStatus& status) {
	os << status.toString();
	return os;
}

} // namespace ds
} // namespace chen
