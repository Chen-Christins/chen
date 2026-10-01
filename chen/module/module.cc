#include "module.h"

#include <fstream>
#include <sstream>
#include <unistd.h>

#include "../application.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../util/env.h"
#include "library.h"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_module_path = 
    Config::Lookup("module.path", std::string("module"), "module path");

std::string GetModulePath() {
    return g_module_path->getValue();
}

Module::Module(const std::string& name, const std::string& version, const std::string& filename, uint32_t type)
    :m_name(name)
    ,m_version(version)
    ,m_filename(filename)
    ,m_id(name + "/" + version)
    ,m_type(type) {
}

void Module::onBeforeArgsParse(int argc, char** argv) {
}

void Module::onAfterArgsParse(int argc, char** argv) {
}

bool Module::onLoad() {
    return true;
}

bool Module::onUnload() {
    return true;
}

bool Module::onActivate() {
    return onServerReady();
}

bool Module::onDeactivate() {
    return true;
}

bool Module::onConnect(Stream::ptr stream) {
    return true;
}

bool Module::onDisconnect(Stream::ptr stream) {
    return true;
}

bool Module::onServerReady() {
    return true;
}

bool Module::onServerUp() {
    return true;
}

void Module::onTick() {
}

uint64_t Module::getTickIntervalMs() {
    return 0;
}

std::string Module::statusString() {
    std::vector<TcpServer::ptr> servers;
    Application::GetInstance()->getAllServers(servers);
    std::stringstream ss;
    for (auto& server : servers) {
        ss << server->toString("") << std::endl;
    }
    return ss.str();
}

static void getServersByType(const std::string& type, std::vector<TcpServer::ptr>& out) {
    Application::GetInstance()->getServer(type, out);
}

void Module::getAllHttpServer(std::vector<std::shared_ptr<http::HttpServer>>& servers) {
    std::vector<TcpServer::ptr> tcps;
    getServersByType("http", tcps);
    for (auto& svr : tcps) {
        auto hs = std::dynamic_pointer_cast<http::HttpServer>(svr);
        if (hs) {
            servers.push_back(hs);
        }
    }
}

void Module::getAllWSServer(std::vector<std::shared_ptr<http::WSServer>>& servers) {
    std::vector<TcpServer::ptr> tcps;
    getServersByType("ws", tcps);
    for (auto& svr : tcps) {
        auto ws = std::dynamic_pointer_cast<http::WSServer>(svr);
        if (ws) {
            servers.push_back(ws);
        }
    }
}

void Module::getAllRpcServer(std::vector<std::shared_ptr<rpc::RpcServer>>& servers) {
    std::vector<TcpServer::ptr> tcps;
    getServersByType("rpc", tcps);
    for (auto& svr : tcps) {
        auto rs = std::dynamic_pointer_cast<rpc::RpcServer>(svr);
        if (rs) {
            servers.push_back(rs);
        }
    }
}

void Module::getAllGameServer(std::vector<std::shared_ptr<game::GenericProtocolServer>>& servers) {
    std::vector<TcpServer::ptr> tcps;
    getServersByType("game", tcps);
    for (auto& svr : tcps) {
        auto gs = std::dynamic_pointer_cast<game::GenericProtocolServer>(svr);
        if (gs) {
            servers.push_back(gs);
        }
    }
}

ModuleManager::ModuleManager() {
}

void ModuleManager::add(Module::ptr mod) {
    del(mod->getId());
    std::unique_lock lock(m_mutex);
    m_modules[mod->getId()] = mod;
}

void ModuleManager::del(const std::string& name) {
    Module::ptr module;
    std::unique_lock lock(m_mutex);
    auto it = m_modules.find(name);
    if (it == m_modules.end()) {
        return ;
    }
    module = it->second;
    m_modules.erase(it);
    lock.unlock();
    module->onUnload();
}

void ModuleManager::delAll() {
    std::unique_lock lock(m_mutex);
    auto tmp = m_modules;
    lock.unlock();

    for (auto& i : tmp) {
        del(i.first);
    }
}

void ModuleManager::init() {
    auto path = EnvMgr::GetInstance()->getAbsolutePath(g_module_path->getValue());

    std::vector<std::string> files;
    FSUtil::ListAllFile(files, path, ".so");

    std::sort(files.begin(), files.end());
    for (auto& i : files) {
        initModule(i);
    }
}

Module::ptr ModuleManager::get(const std::string& name) {
    std::shared_lock lock(m_mutex);
    auto it = m_modules.find(name);
    return it == m_modules.end() ? nullptr : it->second;
}

void ModuleManager::onConnect(Stream::ptr stream) {
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto& m : ms) {
        m->onConnect(stream);
    }
}

void ModuleManager::onDisconnect(Stream::ptr stream) {
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto& m : ms) {
        m->onDisconnect(stream);
    }
}

void ModuleManager::listAll(std::vector<Module::ptr>& ms) {
    std::shared_lock lock(m_mutex);
    for (auto& i : m_modules) {
        ms.push_back(i.second);
    }
}

void ModuleManager::initModule(const std::string& path) {
    Module::ptr m = Library::GetModule(path);
    if (m) {
        add(m);
    }
}

Module::ptr ModuleManager::reloadModule(const std::string& path, Module::ptr* old_out) {
    // 1. 先找出并移除旧模块（不销毁，留给调用方排空后处理）
    Module::ptr old_mod;
    {
        std::unique_lock lock(m_mutex);
        for (auto& [id, mod] : m_modules) {
            if (mod->getFilename() == path) {
                old_mod = mod;
                m_modules.erase(id);
                break;
            }
        }
    }

    // 2. 加载新模块。
    //    ASan 等工具会向 .so 注入 thread_local 变量，导致 glibc 自动标记 DF_1_NODELETE，
    //    dlclose 虽返回 0 但实际不卸载。再次 dlopen 同一路径会返回缓存的旧 handle。
    //    解决方案：先将 .so 复制到唯一临时路径再 dlopen，确保每次拿到最新内容。
    std::string load_path = path;
    std::string tmp_path;
    {
        // 将 .so 复制到临时目录（与模块同目录，避免链接路径差异）
        std::string dir = FSUtil::Dirname(path);
        tmp_path = dir + "/." + std::to_string(GetCurrentMs()) + "_" + std::to_string(getpid()) + ".so";

        std::ifstream src(path, std::ios::binary);
        if (!src) {
            ERROR(g_logger) << "reloadModule: cannot open source path=" << path;
            // 恢复旧模块
            if (old_mod) {
                std::unique_lock lock(m_mutex);
                m_modules[old_mod->getId()] = old_mod;
            }
            return nullptr;
        }
        std::ofstream dst(tmp_path, std::ios::binary | std::ios::trunc);
        if (!dst) {
            ERROR(g_logger) << "reloadModule: cannot create tmp path=" << tmp_path;
            if (old_mod) {
                std::unique_lock lock(m_mutex);
                m_modules[old_mod->getId()] = old_mod;
            }
            return nullptr;
        }
        dst << src.rdbuf();
        src.close();
        dst.close();
        load_path = tmp_path;
    }

    Module::ptr new_mod = Library::GetModule(load_path);
    if (!new_mod) {
        ERROR(g_logger) << "reloadModule: cannot load new module from path=" << load_path;
        FSUtil::Unlink(tmp_path);
        if (old_mod) {
            std::unique_lock lock(m_mutex);
            m_modules[old_mod->getId()] = old_mod;
        }
        return nullptr;
    }
    // 将模块的文件名保持为原始路径（对外不变）
    new_mod->setFilename(path);

    // 3. 调用新模块的 onLoad
    if (!new_mod->onLoad()) {
        ERROR(g_logger) << "reloadModule: new module onLoad failed name="
            << new_mod->getName() << " version=" << new_mod->getVersion()
            << " path=" << new_mod->getFilename();
        FSUtil::Unlink(tmp_path);
        if (old_mod) {
            std::unique_lock lock(m_mutex);
            m_modules[old_mod->getId()] = old_mod;
        }
        return nullptr;
    }

    // 4. 加入模块表
    {
        std::unique_lock lock(m_mutex);
        m_modules[new_mod->getId()] = new_mod;
    }

    // 清理临时文件（dlopen 已 mmap，删除文件不影响已加载的代码）
    if (!tmp_path.empty()) {
        FSUtil::Unlink(tmp_path);
    }

    // 成功后才将旧模块交给调用方排空
    if (old_out) {
        *old_out = old_mod;
    }

    INFO(g_logger) << "reloadModule success name=" << new_mod->getName()
        << " version=" << new_mod->getVersion() << " path=" << new_mod->getFilename();
    return new_mod;
}

void ModuleManager::listByType(uint32_t type, std::vector<Module::ptr>& ms) {
    std::shared_lock lock(m_mutex);
    auto it = m_type2Modules.find(type);
    if(it == m_type2Modules.end()) {
        return;
    }
    for(auto& i : it->second) {
        ms.push_back(i.second);
    }
}

void ModuleManager::foreach(uint32_t type, std::function<void(Module::ptr)> cb) {
    std::vector<Module::ptr> ms;
    listByType(type, ms);

    for (auto& m : ms) {
        cb(m);
    }
}

} // namespace chen
