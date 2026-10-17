#include "module.h"

#include <algorithm>
#include <sstream>

#include "../application.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../util/env.h"
#include "library.h"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_module_path = Config::Lookup("module.path", std::string("module"), "module path");

std::string GetModulePath()
{
    return g_module_path->getValue();
}

Module::Module(const std::string& name, const std::string& version, const std::string& filename, uint32_t type)
    : m_name(name)
    , m_version(version)
    , m_filename(filename)
    , m_id(name + "/" + version)
    , m_type(type)
{
}

void Module::onBeforeArgsParse(int argc, char** argv)
{
}

void Module::onAfterArgsParse(int argc, char** argv)
{
}

bool Module::onLoad()
{
    return true;
}

bool Module::onUnload()
{
    return true;
}

bool Module::onDeactivate()
{
    return true;
}

bool Module::onConnect(Stream::ptr stream)
{
    return true;
}

bool Module::onDisconnect(Stream::ptr stream)
{
    return true;
}

bool Module::onServerReady()
{
    return true;
}

bool Module::onServerUp()
{
    return true;
}

void Module::onTick()
{
}

uint64_t Module::getTickIntervalMs()
{
    return 0;
}

std::string Module::statusString()
{
    std::vector<TcpServer::ptr> servers;
    Application::GetInstance()->getAllServers(servers);
    std::stringstream ss;
    for (auto& server : servers) {
        ss << server->toString("") << std::endl;
    }
    return ss.str();
}

static void getServersByType(const std::string& type, std::vector<TcpServer::ptr>& out)
{
    Application::GetInstance()->getServer(type, out);
}

void Module::getAllHttpServer(std::vector<std::shared_ptr<http::HttpServer>>& servers)
{
    std::vector<TcpServer::ptr> tcps;
    getServersByType("http", tcps);
    for (auto& svr : tcps) {
        auto hs = std::dynamic_pointer_cast<http::HttpServer>(svr);
        if (hs) {
            servers.push_back(hs);
        }
    }
}

void Module::getAllWSServer(std::vector<std::shared_ptr<http::WSServer>>& servers)
{
    std::vector<TcpServer::ptr> tcps;
    getServersByType("ws", tcps);
    for (auto& svr : tcps) {
        auto ws = std::dynamic_pointer_cast<http::WSServer>(svr);
        if (ws) {
            servers.push_back(ws);
        }
    }
}

void Module::getAllRpcServer(std::vector<std::shared_ptr<rpc::RpcServer>>& servers)
{
    std::vector<TcpServer::ptr> tcps;
    getServersByType("rpc", tcps);
    for (auto& svr : tcps) {
        auto rs = std::dynamic_pointer_cast<rpc::RpcServer>(svr);
        if (rs) {
            servers.push_back(rs);
        }
    }
}

void Module::getAllGameServer(std::vector<std::shared_ptr<game::GenericProtocolServer>>& servers)
{
    std::vector<TcpServer::ptr> tcps;
    getServersByType("game", tcps);
    for (auto& svr : tcps) {
        auto gs = std::dynamic_pointer_cast<game::GenericProtocolServer>(svr);
        if (gs) {
            servers.push_back(gs);
        }
    }
}

ModuleManager::ModuleManager()
{
}

void ModuleManager::add(Module::ptr mod)
{
    del(mod->getId());
    std::unique_lock lock(m_mutex);
    m_modules[mod->getId()] = mod;
}

void ModuleManager::del(const std::string& name)
{
    Module::ptr module;
    std::unique_lock lock(m_mutex);
    auto it = m_modules.find(name);
    if (it == m_modules.end()) {
        return;
    }
    module = it->second;
    m_modules.erase(it);
    lock.unlock();
    module->onUnload();
}

void ModuleManager::delAll()
{
    std::unique_lock lock(m_mutex);
    auto tmp = m_modules;
    lock.unlock();

    for (auto& i : tmp) {
        del(i.first);
    }
}

void ModuleManager::init()
{
    auto path = EnvMgr::GetInstance()->getAbsolutePath(g_module_path->getValue());

    std::vector<std::string> files;
    FSUtil::ListAllFile(files, path, ".so");
    // 跳过隐藏文件（如遗留的热重载临时 .so，避免被误当成模块加载）
    files.erase(
        std::remove_if(
            files.begin(), files.end(),
            [](const std::string& f) {
                auto base = FSUtil::Basename(f);
                return !base.empty() && base[0] == '.';
            }),
        files.end());

    std::sort(files.begin(), files.end());
    for (auto& i : files) {
        initModule(i);
    }
}

Module::ptr ModuleManager::get(const std::string& name)
{
    std::shared_lock lock(m_mutex);
    auto it = m_modules.find(name);
    return it == m_modules.end() ? nullptr : it->second;
}

void ModuleManager::onConnect(Stream::ptr stream)
{
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto& m : ms) {
        m->onConnect(stream);
    }
}

void ModuleManager::onDisconnect(Stream::ptr stream)
{
    std::vector<Module::ptr> ms;
    listAll(ms);

    for (auto& m : ms) {
        m->onDisconnect(stream);
    }
}

void ModuleManager::listAll(std::vector<Module::ptr>& ms)
{
    std::shared_lock lock(m_mutex);
    for (auto& i : m_modules) {
        ms.push_back(i.second);
    }
}

void ModuleManager::initModule(const std::string& path)
{
    Module::ptr m = Library::GetModule(path);
    if (m) {
        add(m);
    }
}

void ModuleManager::listByType(uint32_t type, std::vector<Module::ptr>& ms)
{
    std::shared_lock lock(m_mutex);
    auto it = m_type2Modules.find(type);
    if (it == m_type2Modules.end()) {
        return;
    }
    for (auto& i : it->second) {
        ms.push_back(i.second);
    }
}

void ModuleManager::foreach(uint32_t type, std::function<void(Module::ptr)> cb)
{
    std::vector<Module::ptr> ms;
    listByType(type, ms);

    for (auto& m : ms) {
        cb(m);
    }
}

} // namespace chen
