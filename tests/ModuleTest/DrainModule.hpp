/**
 * @file DrainModule.hpp
 * @brief drain/热重载集成测试模块
 * @author Christins
 * @date 2026-11-27
 * @copyright GPL-3.0
 * @details 只在 tests/integration/ 下使用：提供 /drain/slow（默认 sleep 800ms）与
 *          /drain/fast 两个 servlet，用于验证「热重载时在途请求不被打断」。
 */
#pragma once

#include <string>

#include "src/Module/Module.hpp"

namespace module_test {

class DrainModule : public chen::Module {
public:
    DrainModule();

    bool onLoad() override;

    bool onServerReady() override;

    std::string statusString() override;
};

} // namespace module_test
