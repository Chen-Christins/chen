/**
 * @file noncopyable.h
 * @brief 禁用拷贝
 * @author Christins
 * @date 2024-11-03
 */
#pragma once

namespace chen {

/**
 * @brief 禁用拷贝base类，对象无法拷贝,赋值
 */
class Noncopyable {
protected:
    /**
     * @brief 默认构造函数
     */
    Noncopyable() = default;    
    
    /**
     * @brief 默认析构函数
     */
    virtual ~Noncopyable() = default;    
    
    /**
     * @brief 拷贝构造函数禁用
     */
    Noncopyable(const Noncopyable&) = delete;
    
    /**
     * @brief 赋值函数禁用
     */
    Noncopyable& operator=(const Noncopyable&) = delete;
};

}
