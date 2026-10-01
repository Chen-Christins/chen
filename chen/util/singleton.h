/**
 * @file singleton.h
 * @brief 单例模式模板
 * @author Christins
 * @date 2024-11-03
 */
#pragma once

#include <memory>

namespace chen {

/**
 * @brief 单例模式
 * @tparam T 需要设置单例模式的实例
 */
template <class T, class X = void, int N = 0>
class Singleton {
public:
    static T* GetInstance() {
        static T v;
        return &v;
    }
};

/**
 * @brief 单例模式的智能指针
 * @tparam T 需要设置单例模式的实例
 */
template <class T, class X = void, int N = 0>
class SingletonPtr {
public:
    static std::shared_ptr<T> GetInstance() {
        static std::shared_ptr<T> v(new T);
        return v;
    }
};

}
