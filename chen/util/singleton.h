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
    static T* GetInstance()
    {
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
    static std::shared_ptr<T> GetInstance()
    {
        static std::shared_ptr<T> v(new T);
        return v;
    }
};

/**
 * @brief 泄漏单例：进程退出时不析构
 * @details 用于持有进程退出时必须避免析构的对象。例如 ModuleManager
 *          的析构会 dlclose 模块 .so，多进程模型中 worker 退出时
 *          （std::exit 触发静态析构）卸载模块代码有风险，且会使指向
 *          .so 的 EventBus/Config listener 悬空。改为泄漏单例后，
 *          退出时由内核回收地址空间。
 * @tparam T 需要设置单例模式的实例
 */
template <class T, class X = void, int N = 0>
class LeakySingleton {
public:
    static T* GetInstance()
    {
        static T* v = new T;
        return v;
    }
};

} // namespace chen
