/**
 * @file random_util.h
 * @brief 随机数工具类，提供各种类型的随机数生成、随机选择和随机字符串生成等功能
 * @author Christins (chen.christins@icloud.com)
 * @date 2026-05-26
 * @copyright GPL-3.0
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace chen {

class RandomUtil {
public:
    /**
     * @brief 使用指定种子初始化线程局部随机引擎
     * @param seed 种子值
     */
    static void Seed(uint64_t seed);

    /**
     * @brief 使用 random_device 熵源重新播种线程局部引擎
     */
    static void SeedWithEntropy();

    /**
     * @brief 生成 [min, max] 范围内的随机整数
     * @param min 最小值（含）
     * @param max 最大值（含）
     * @return 随机整数
     */
    static int32_t RandInt(int32_t min, int32_t max);
    static uint32_t RandUint(uint32_t min, uint32_t max);
    static int64_t RandInt64(int64_t min, int64_t max);
    static uint64_t RandUint64(uint64_t min, uint64_t max);

    /**
     * @brief 生成 [min, max) 范围内的随机浮点数
     * @param min 最小值（含），默认 0.0
     * @param max 最大值（不含），默认 1.0
     * @return 随机浮点数
     */
    static double RandDouble(double min = 0.0, double max = 1.0);

    /**
     * @brief 生成 [min, max) 范围内的随机单精度浮点数
     * @param min 最小值（含），默认 0.0
     * @param max 最大值（不含），默认 1.0
     * @return 随机浮点数
     */
    static float RandFloat(float min = 0.0f, float max = 1.0f);

    /**
     * @brief 按给定概率返回 true
     * @param prob 返回 true 的概率，取值范围 [0, 1]，默认 0.5
     */
    static bool RandBool(double prob = 0.5);

    /**
     * @brief 按权重随机选择一个元素的索引（int32_t 权重版本）
     * @param weights 权重列表
     * @return 选中元素的索引，权重全为非正数或列表为空时返回 -1
     */
    static int32_t WeightedSelect(const std::vector<int32_t>& weights);

    /**
     * @brief 按权重随机选择一个元素的索引（double 权重版本）
     * @param weights 权重列表
     * @return 选中元素的索引，权重全为非正数或列表为空时返回 -1
     */
    static int32_t WeightedSelect(const std::vector<double>& weights);

    /**
     * @brief 按权重不放回地随机选择 k 个不重复元素的索引（int32_t 权重版本）
     * @param weights 权重列表
     * @param count 需要选择的元素数量
     * @return 选中元素的索引列表，count >= 正权重元素数时返回全部（打乱顺序）
     */
    static std::vector<int32_t> WeightedSelectMultiple(const std::vector<int32_t>& weights, size_t count);

    /**
     * @brief 按权重不放回地随机选择 k 个不重复元素的索引（double 权重版本）
     * @param weights 权重列表
     * @param count 需要选择的元素数量
     * @return 选中元素的索引列表，count >= 正权重元素数时返回全部（打乱顺序）
     */
    static std::vector<int32_t> WeightedSelectMultiple(const std::vector<double>& weights, size_t count);

    /**
     * @brief 从标准容器中随机选取一个元素
     * @tparam Container 容器类型
     * @param c 容器引用
     * @return 指向随机元素的指针，容器为空时返回 nullptr
     */
    template <class Container>
    static const typename Container::value_type* RandElement(const Container& c) {
        if (c.empty()) {
            return nullptr;
        }
        auto it = c.begin();
        std::advance(it, static_cast<size_t>(RandInt64(0, static_cast<int64_t>(c.size()) - 1)));
        return &(*it);
    }

    /**
     * @brief 生成指定长度的随机字母数字字符串 [0-9a-zA-Z]
     * @param length 字符串长度
     * @return 随机字符串
     */
    static std::string RandString(size_t length);

    /**
     * @brief 从指定字符集生成随机字符串
     * @param length 字符串长度
     * @param charset 候选字符集
     * @return 随机字符串，charset 为空或 length 为 0 时返回空串
     */
    static std::string RandString(size_t length, const std::string& charset);

    /**
     * @brief 生成指定长度的随机字节串
     * @param count 字节数
     * @return 随机字节串
     */
    static std::string RandBytes(size_t count);

    /**
     * @brief 原地打乱区间 [first, last) 内元素的顺序
     * @tparam RandomIt 随机访问迭代器类型
     * @param first 起始迭代器
     * @param last 结束迭代器
     */
    template <class RandomIt>
    static void Shuffle(RandomIt first, RandomIt last) {
        std::shuffle(first, last, Rng());
    }

    /**
     * @brief 从 [0, n) 范围内不放回地随机抽取 k 个整数
     * @param n 范围上限（不含）
     * @param k 抽取数量
     * @return 包含 k 个不同随机整数的 vector
     */
    static std::vector<size_t> RandSample(size_t n, size_t k);

private:
    static std::mt19937_64& Rng();
};

/**
 * @brief 雪花算法分布式唯一 ID 生成器
 */
class SnowflakeIdGenerator {
public:
    /**
     * @brief 生成下一个唯一 ID
     * @return uint64_t 基于时间戳 + 节点 ID + 序列号的唯一 ID
     */
    uint64_t next();

private:
    static uint16_t resolveNodeId();

private:
    std::mutex mutex_;

    static constexpr uint8_t kNodeIdBits = 10;
    static constexpr uint8_t kSequenceBits = 12;
    static constexpr uint16_t kMaxNodeId = (1U << kNodeIdBits) - 1U;
    static constexpr uint16_t kSequenceMask = (1U << kSequenceBits) - 1U;
    static constexpr uint8_t kNodeIdShift = kSequenceBits;
    static constexpr uint8_t kTimestampShift = kNodeIdBits + kSequenceBits;
    static constexpr uint64_t kCustomEpochMs = 1735689600000ULL;

    uint64_t last_ms_ = 0;
    uint16_t sequence_ = 0;
    const uint16_t node_id_ = resolveNodeId();
};

} // namespace chen
