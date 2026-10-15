#pragma once

#include "chen/module/module.h"

namespace module_test {

/**
 * @brief 生命周期测试模块
 * @details 每个回调打印日志 + 实例 ID，用于验证：
 *   1. 启动时 onLoad → onServerReady → onServerUp → onTick 的顺序
 *   2. 优雅关闭时 onDeactivate → onUnload 被调用
 *
 * 编译：在项目根目录 make 后，tests 下 add_library(lifecycle_module SHARED ...)
 *       输出到 bin/module/lifecycle_module.so
 *
 * 使用：
 *   1. cp bin/module/lifecycle_module.so bin/module/
 *   2. bin/main -s -c bin/conf/system.yml
 *   3. 修改代码重新编译，覆盖 bin/module/lifecycle_module.so
 *   4. kill -HUP $(cat bin/chen.pid)
 *   5. 观察新 worker 加载新模块、旧 worker drain 退出的生命周期日志
 */
class LifecycleModule : public chen::Module {
public:
    LifecycleModule();
    ~LifecycleModule() override;

    bool onLoad() override;
    bool onUnload() override;
    bool onDeactivate() override;
    bool onServerReady() override;
    bool onServerUp() override;
    void onTick() override;
    uint64_t getTickIntervalMs() override;

private:
    int m_id;
};

} // namespace module_test
