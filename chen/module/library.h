/**
 * @file library.h
 * @brief library模块
 * @author Christins
 * @date 2025-03-30
 * @copyright GPL-3.0
 */
#pragma once

#include "module.h"

namespace chen {

class Library {
public:
    /**
     * @brief 根据动态库获取模块示例
     * @param path 动态库的位置
     * @return Module::ptr 返回模块的类型
     */
    static Module::ptr GetModule(const std::string& path);
};

}
