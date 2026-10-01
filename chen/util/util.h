/**
 * @file util.h
 * @brief 工具函数
 * @author Christins
 * @date 2024-11-03
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <boost/lexical_cast.hpp>

#include "compress_util.h" // IWYU pragma: keep
#include "encryptor_util.h" // IWYU pragma: keep
#include "endian.h"
#include "fs_util.h" // IWYU pragma: keep
#include "json_util.h" // IWYU pragma: keep
#include "random_util.h" // IWYU pragma: keep
#include "string_util.h" // IWYU pragma: keep
#include "time_util.h" // IWYU pragma: keep
#include "type_util.h" // IWYU pragma: keep

namespace chen {
/**
 * @brief 获取线程号
 * @return pid_t 返回pid
 */
pid_t GetThreadId();

/**
 * @brief 获取协程号
 */
uint32_t GetFiberId();

/**
 * @brief 打印堆栈信息
 * @param bt 打印的堆栈信息
 * @param size 
 * @param skip 
 */
void Backtrace(std::vector<std::string>& bt, int size = 64, int skip = 1);

/**
 * @brief 将堆栈信息转换为字符串
 * @param size 
 * @param skip 
 * @param prefix 
 * @return std::string 
 */
std::string BacktraceToString(int size = 64, int skip = 1, const std::string& prefix = "    ");

/**
 * @brief 获取CPU核心数
 * @return int32_t CPU核心数
 */
int32_t GetCPUCount();

inline uint64_t htonll(uint64_t host_64bits) {
    return byteswapOnLittleEndian(host_64bits);
}

inline uint64_t ntohll(uint64_t net_64bits) {
    return byteswapOnLittleEndian(net_64bits);
}

/**
 * @brief 获取列表中是否存在值K，不存在则返回def值
 * @tparam V 查找值的类型
 * @tparam Map 目标列表的类型
 * @tparam K 查找的值的类型
 * @param m 目标列表
 * @param k 查找的值
 * @param def 默认返回值
 * @return V 返回类型的值
 */
template <class V, class Map, class K>
V GetParamValue(const Map& m, const K& k, const V& def = V()) {
    auto it = m.find(k);
    if (it == m.end()) {
        return def;
    }
    try {
        return boost::lexical_cast<V>(it->second);
    } catch (...) {
    }
    return def;
}

/**
 * @brief 获取列表中是否存在值K，不存在则返回false，存在返回true
 * @tparam V 查找值的类型
 * @tparam Map 目标列表的类型
 * @tparam K 查找的值的类型
 * @param m 目标列表
 * @param k 查找的值
 * @param def 默认返回值
 * @return V 返回 true|false
 */
template <class V, class Map, class K>
bool CheckGetParamValue(const Map& m, const K& k, V& v) {
    auto it = m.find(k);
    if (it == m.end()) {
        return false;
    }
    try {
        v = boost::lexical_cast<V>(it->second);
        return true;
    } catch (...) {
    }
    return false;
}

class Atomic {
public:
    template<class T, class S = T>
    static T addFetch(volatile T& t, S v = 1) {
        return __sync_add_and_fetch(&t, (T)v);
    }

    template<class T, class S = T>
    static T subFetch(volatile T& t, S v = 1) {
        return __sync_sub_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T orFetch(volatile T& t, S v) {
        return __sync_or_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T andFetch(volatile T& t, S v) {
        return __sync_and_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T xorFetch(volatile T& t, S v) {
        return __sync_xor_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T nandFetch(volatile T& t, S v) {
        return __sync_nand_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAdd(volatile T& t, S v = 1) {
        return __sync_fetch_and_add(&t, (T)v);
    }

    template<class T, class S>
    static T fetchSub(volatile T& t, S v = 1) {
        return __sync_fetch_and_sub(&t, (T)v);
    }

    template<class T, class S>
    static T fetchOr(volatile T& t, S v) {
        return __sync_fetch_and_or(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAnd(volatile T& t, S v) {
        return __sync_fetch_and_and(&t, (T)v);
    }

    template<class T, class S>
    static T fetchXor(volatile T& t, S v) {
        return __sync_fetch_and_xor(&t, (T)v);
    }

    template<class T, class S>
    static T fetchNand(volatile T& t, S v) {
        return __sync_fetch_and_nand(&t, (T)v);
    }

    template<class T, class S>
    static T compareAndSwap(volatile T& t, S old_val, S new_val) {
        return __sync_val_compare_and_swap(&t, (T)old_val, (T)new_val);
    }

    template<class T, class S>
    static bool compareAndSwapBool(volatile T& t, S old_val, S new_val) {
        return __sync_bool_compare_and_swap(&t, (T)old_val, (T)new_val);
    }
};

template<class T>
void nop(T*) {}

}
