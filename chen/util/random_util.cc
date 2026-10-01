#include "random_util.h"

#include <array>
#include <cstdlib>
#include <cstring>

#include "time_util.h"

namespace chen {

std::mt19937_64& RandomUtil::Rng() {
    static thread_local std::mt19937_64 rng([]() {
        std::random_device rd;
        std::array<uint64_t, 8> seeds;
        for (auto& s : seeds) {
            s = (static_cast<uint64_t>(rd()) << 32) | rd();
        }
        std::seed_seq seq(seeds.begin(), seeds.end());
        return std::mt19937_64(seq);
    }());
    return rng;
}

void RandomUtil::Seed(uint64_t seed) {
    Rng().seed(seed);
}

void RandomUtil::SeedWithEntropy() {
    std::random_device rd;
    Rng().seed(rd());
}

int32_t RandomUtil::RandInt(int32_t min, int32_t max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<int32_t> dist(min, max);
    return dist(Rng());
}

uint32_t RandomUtil::RandUint(uint32_t min, uint32_t max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<uint32_t> dist(min, max);
    return dist(Rng());
}

int64_t RandomUtil::RandInt64(int64_t min, int64_t max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<int64_t> dist(min, max);
    return dist(Rng());
}

uint64_t RandomUtil::RandUint64(uint64_t min, uint64_t max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_int_distribution<uint64_t> dist(min, max);
    return dist(Rng());
}

double RandomUtil::RandDouble(double min, double max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_real_distribution<double> dist(min, max);
    return dist(Rng());
}

float RandomUtil::RandFloat(float min, float max) {
    if (min > max) {
        std::swap(min, max);
    }
    std::uniform_real_distribution<float> dist(min, max);
    return dist(Rng());
}

bool RandomUtil::RandBool(double prob) {
    return RandDouble() < prob;
}

int32_t RandomUtil::WeightedSelect(const std::vector<int32_t>& weights) {
    if (weights.empty()) {
        return -1;
    }

    int64_t sum = 0;
    for (auto w : weights) {
        if (w > 0) {
            sum += w;
        }
    }
    if (sum == 0) {
        return -1;
    }

    int64_t r = RandInt64(0, sum - 1);
    int64_t cumulative = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= 0) {
            continue;
        }
        cumulative += weights[i];
        if (r < cumulative) {
            return static_cast<int32_t>(i);
        }
    }
    return static_cast<int32_t>(weights.size() - 1);
}

int32_t RandomUtil::WeightedSelect(const std::vector<double>& weights) {
    if (weights.empty()) {
        return -1;
    }

    double sum = 0.0;
    for (auto w : weights) {
        if (w > 0.0) {
            sum += w;
        }
    }
    if (sum <= 0.0) {
        return -1;
    }

    double r = RandDouble(0.0, sum);
    double cumulative = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] <= 0.0) {
            continue;
        }
        cumulative += weights[i];
        if (r < cumulative) {
            return static_cast<int32_t>(i);
        }
    }
    return static_cast<int32_t>(weights.size() - 1);
}

std::vector<int32_t> RandomUtil::WeightedSelectMultiple(const std::vector<int32_t>& weights, size_t count) {
    if (weights.empty() || count == 0) {
        return {};
    }

    std::vector<int32_t> indices;
    std::vector<int32_t> w;
    indices.reserve(weights.size());
    w.reserve(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0) {
            indices.push_back(static_cast<int32_t>(i));
            w.push_back(weights[i]);
        }
    }

    if (indices.empty()) {
        return {};
    }
    if (count >= indices.size()) {
        Shuffle(indices.begin(), indices.end());
        return indices;
    }

    std::vector<int32_t> result;
    result.reserve(count);
    for (size_t k = 0; k < count && !w.empty(); ++k) {
        int32_t pick = WeightedSelect(w);
        if (pick < 0) {
            break;
        }
        result.push_back(indices[pick]);
        size_t last = w.size() - 1;
        indices[pick] = indices[last];
        w[pick] = w[last];
        indices.pop_back();
        w.pop_back();
    }
    return result;
}

std::vector<int32_t> RandomUtil::WeightedSelectMultiple(const std::vector<double>& weights, size_t count) {
    if (weights.empty() || count == 0) {
        return {};
    }

    std::vector<int32_t> indices;
    std::vector<double> w;
    indices.reserve(weights.size());
    w.reserve(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0.0) {
            indices.push_back(static_cast<int32_t>(i));
            w.push_back(weights[i]);
        }
    }

    if (indices.empty()) {
        return {};
    }
    if (count >= indices.size()) {
        Shuffle(indices.begin(), indices.end());
        return indices;
    }

    std::vector<int32_t> result;
    result.reserve(count);
    for (size_t k = 0; k < count && !w.empty(); ++k) {
        int32_t pick = WeightedSelect(w);
        if (pick < 0) {
            break;
        }
        result.push_back(indices[pick]);
        size_t last = w.size() - 1;
        indices[pick] = indices[last];
        w[pick] = w[last];
        indices.pop_back();
        w.pop_back();
    }
    return result;
}

std::string RandomUtil::RandString(size_t length) {
    static const char kAlphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    return RandString(length, std::string(kAlphanum, sizeof(kAlphanum) - 1));
}

std::string RandomUtil::RandString(size_t length, const std::string& charset) {
    if (charset.empty() || length == 0) {
        return "";
    }

    std::string result;
    result.reserve(length);
    std::uniform_int_distribution<size_t> dist(0, charset.size() - 1);
    auto& rng = Rng();
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}

std::string RandomUtil::RandBytes(size_t count) {
    if (count == 0) {
        return "";
    }

    std::string result(count, '\0');
    std::uniform_int_distribution<int> dist(0, 255);
    auto& rng = Rng();
    for (size_t i = 0; i < count; ++i) {
        result[i] = static_cast<char>(dist(rng));
    }
    return result;
}

std::vector<size_t> RandomUtil::RandSample(size_t n, size_t k) {
    if (k == 0 || n == 0) {
        return {};
    }
    if (k >= n) {
        std::vector<size_t> result(n);
        std::iota(result.begin(), result.end(), size_t{0});
        Shuffle(result.begin(), result.end());
        return result;
    }

    std::vector<size_t> pool(n);
    std::iota(pool.begin(), pool.end(), size_t{0});
    std::vector<size_t> result;
    result.reserve(k);
    auto& rng = Rng();
    for (size_t i = 0; i < k; ++i) {
        std::uniform_int_distribution<size_t> dist(i, n - 1);
        size_t j = dist(rng);
        std::swap(pool[i], pool[j]);
        result.push_back(pool[i]);
    }
    return result;
}

uint64_t SnowflakeIdGenerator::next() {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t now_ms = GetCurrentMs();
    if (now_ms < last_ms_) {
        now_ms = last_ms_;
    }

    if (now_ms == last_ms_) {
        sequence_ = (sequence_ + 1U) & kSequenceMask;
        if (sequence_ == 0U) {
            now_ms = last_ms_ + 1U;
        }
    } else {
        sequence_ = 0U;
    }

    last_ms_ = now_ms;

    const uint64_t ts_part = (now_ms - kCustomEpochMs) << kTimestampShift;
    const uint64_t node_part = static_cast<uint64_t>(node_id_) << kNodeIdShift;
    const uint64_t seq_part = static_cast<uint64_t>(sequence_);

    return ts_part | node_part | seq_part;
}

uint16_t SnowflakeIdGenerator::resolveNodeId() {
    const char* env = std::getenv("S1_NODE_ID");
    if (!env || *env == '\0') {
        return 0U;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(env, &end, 10);
    if (end == env || *end != '\0') {
        return 0U;
    }
    if (value > kMaxNodeId) {
        return static_cast<uint16_t>(value % (kMaxNodeId + 1U));
    }
    return static_cast<uint16_t>(value);
}

} // namespace chen
