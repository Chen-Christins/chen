#pragma once

#include "chen/module/module.h"

namespace module_test {

/**
 * @brief 生命周期测试模块
 * @details 每个回调打印日志 + 实例 ID，用于验证：
 *   1. 初始启动时 onLoad → onServerReady → onServerUp → onTick 的顺序
 *   2. 热重载时新模块 onActivate → onServerReady → onServerUp
 *   3. 旧模块 onDeactivate 被调用
 *   4. 旧模块 shared_ptr 引用归零后 DestroyModule → dlclose 触发
 *
 * 编译：在项目根目录 make 后，tests 下 add_library(lifecycle_module SHARED ...)
 *       输出到 bin/module/lifecycle_module.so
 *
 * 使用：
 *   1. cp bin/module/lifecycle_module.so bin/module/
 *   2. bin/main -s -c bin/conf/system.yml
 *   3. 修改代码重新编译，覆盖 bin/module/lifecycle_module.so
 *   4. kill -HUP $(cat bin/chen.pid)
 *   5. 观察日志中 id=1(旧) 和 id=2(新) 的生命周期
 */
class LifecycleModule : public chen::Module {
public:
    LifecycleModule();
    ~LifecycleModule() override;

    bool onLoad() override;
    bool onUnload() override;
    bool onActivate() override;
    bool onDeactivate() override;
    bool onServerReady() override;
    bool onServerUp() override;
    void onTick() override;
    uint64_t getTickIntervalMs() override;

private:
    int m_id;
};

} // namespace module_test
