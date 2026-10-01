#include "library.h"

#include <dlfcn.h>

#include "../config/config.h"
#include "../log/log.h"
#include "../util/env.h"

namespace chen {

typedef Module* (*create_module)();
typedef void (*destroy_module)(Module*);

static Logger::ptr logger = LOG_NAME("system");

class ModuleCloser {
public:
    /**
     * @brief 构造函数
     * @param handle 动态链接库的句柄
     * @param d 销毁模块的函数指针
     */
    ModuleCloser(void* handle, destroy_module d) : m_handle(handle), m_destroy(d) {}

    /**
     * @brief 重载()运算符
     * @param module 传入模块指针
     */
    void operator()(Module* module) {
        std::string name = module->getName();
        std::string version = module->getVersion();
        std::string path = module->getFilename();

        m_destroy(module);
        int rt = dlclose(m_handle);
        if (rt) {
            ERROR(logger) << "dlclose handle fail handle="
                << m_handle << " name=" << name
                << " version=" << version
                << " path=" << path
                << " error=" << dlerror();
        } else {
            INFO(logger) << "destroy module=" << name
                << " version=" << version
                << " path=" << path
                << " handle=" << m_handle
                << " success";
        }
    }
private:
    // 存储动态链接库的句柄
    void* m_handle;
    // 存储用于销毁模块的函数指针
    destroy_module m_destroy;
};

Module::ptr Library::GetModule(const std::string& path) {
    // 创建动态库句柄
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        ERROR(logger) << "cannot load library path="
            << path << " error=" << dlerror();
        return nullptr;
    }
    // 模块的类型指针
    create_module create = (create_module)dlsym(handle, "CreateModule");
    if (!create) {
        ERROR(logger) << "cannot load symbol CreateModule in "
            << path << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }
    // 销毁动态库的指针
    destroy_module destroy = (destroy_module)dlsym(handle, "DestroyModule");
    if (!destroy) {
        ERROR(logger) << "cannot load symbol DestroyModule in "
            << path << " error=" << dlerror();
        dlclose(handle);
        return nullptr;
    }
    // 加载动态库，获取模块信息
    Module::ptr module(create(), ModuleCloser(handle, destroy));
    module->setFilename(path);
    INFO(logger) << "load module name=" << module->getName()
        << " version=" << module->getVersion()
        << " path=" << module->getFilename()
        << " success";
    Config::LoadFromConfDir(EnvMgr::GetInstance()->getConfigPath(), true);
    return module;
}


}
