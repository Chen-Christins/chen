#include "Module.hpp"

#include <algorithm>
#include <sstream>

#include "../Application.hpp"
#include "../Config/Config.hpp"
#include "../Log/Log.hpp"
#include "../Socket/Address.hpp"
#include "../Util/Env.hpp"
#include "Library.hpp"

namespace chen {

static Logger::ptr g_logger = LOG_NAME("system");

static ConfigVar<std::string>::ptr g_module_path = Config::Lookup("module.path", std::string("module"), "module path");

/**
 * @brief 获取本机非回环 IPv4 地址（用于监听 0.0.0.0 时向服务发现注册）
 */
static std::string GetLocalIPv4()
{
    std::multimap<std::string, std::pair<Address::ptr, uint32_t>> result;
    if (!Address::GetInterfaceAddresses(result)) {
        ERROR(g_logger) << "get interface addresses fail";
        return "";
    }

    for (auto& [name, value] : result) {
        auto addr = std::dynamic_pointer_cast<IPv4Address>(value.first);
        if (!addr) {
            continue;
        }
        std::string ip = addr->toString();
        ip = ip.substr(0, ip.rfind(':'));
        if (ip.compare(0, 4, "127.") == 0) {
            continue;
        }
        return ip;
    }
    return "";
}

std::string GetModulePath()
{
    return g_module_path->getValue();
}

Module::Module(const std::string& name, const std::string& version, const std::string& filename, uint32_t type)
    : name_(name)
    , version_(version)
    , filename_(filename)
    , id_(name + "/" + version)
    , type_(type)
{
}

Module::~Module()
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
    // 默认实现：下线时把本模块注册的服务实例从服务发现中移除
    unregisterAllServices();
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

bool Module::handleRequest(Message::ptr req, Message::ptr rsp, Stream::ptr stream)
{
    DEBUG(g_logger) << "Module::handleRequest name=" << name_ << " req=" << (req ? req->toString() : "null")
                    << " stream=" << stream;
    return true;
}

bool Module::handleNotify(Message::ptr notify, Stream::ptr stream)
{
    DEBUG(g_logger) << "Module::handleNotify name=" << name_ << " notify=" << (notify ? notify->toString() : "null")
                    << " stream=" << stream;
    return true;
}

void Module::registerService(const std::string& server_type, const std::string& domain, const std::string& service)
{
    Application* app = Application::GetInstance();
    if (!app) {
        WARN(g_logger) << "registerService skipped: application is null, domain=" << domain << " service=" << service;
        return;
    }

    IServiceDiscovery::ptr sd = app->getServiceDiscovery();
    if (!sd) {
        DEBUG(g_logger) << "registerService skipped: no service discovery, domain=" << domain << " service=" << service;
        return;
    }

    std::vector<TcpServer::ptr> servers;
    if (!app->getServer(server_type, servers) || servers.empty()) {
        WARN(g_logger) << "registerService skipped: no server type=" << server_type << " domain=" << domain << " service=" << service;
        return;
    }

    const std::string local_ipv4 = GetLocalIPv4();

    for (auto& server : servers) {
        for (auto& sock : server->getSocks()) {
            auto addr = std::dynamic_pointer_cast<IPv4Address>(sock->getLocalAddress());
            if (!addr) {
                continue;
            }

            std::string ip = addr->toString();
            ip = ip.substr(0, ip.rfind(':'));
            if (ip == "0.0.0.0") {
                ip = local_ipv4;
            }
            if (ip.empty() || ip.compare(0, 4, "127.") == 0) {
                continue;
            }

            sd->registerServer(domain, service, ip + ":" + std::to_string(addr->getPort()), server_type);
            {
                std::lock_guard lock(registered_services_mutex_);
                registered_services_.push_back(
                    RegisteredService{server_type, domain, service, ip + ":" + std::to_string(addr->getPort())});
            }
            INFO(g_logger) << "registerService domain=" << domain << " service=" << service << " addr=" << ip << ":" << addr->getPort()
                           << " type=" << server_type;
        }
    }
}

void Module::unregisterService(const std::string& server_type, const std::string& domain, const std::string& service)
{
    std::vector<RegisteredService> removed;
    {
        std::lock_guard lock(registered_services_mutex_);
        auto it = std::remove_if(
            registered_services_.begin(), registered_services_.end(), [&](const RegisteredService& item) {
                return item.server_type == server_type && item.domain == domain && item.service == service;
        });
        removed.assign(it, registered_services_.end());
        registered_services_.erase(it, registered_services_.end());
    }

    if (removed.empty()) {
        return;
    }

    Application* app = Application::GetInstance();
    IServiceDiscovery::ptr sd = app ? app->getServiceDiscovery() : nullptr;
    if (!sd) {
        return;
    }
    for (auto& item : removed) {
        sd->unregisterServer(item.domain, item.service, item.addr);
        INFO(g_logger) << "unregisterService domain=" << item.domain << " service=" << item.service << " addr=" << item.addr
                       << " type=" << item.server_type;
    }
}

void Module::unregisterAllServices()
{
    std::vector<RegisteredService> removed;
    {
        std::lock_guard lock(registered_services_mutex_);
        removed.swap(registered_services_);
    }

    if (removed.empty()) {
        return;
    }

    Application* app = Application::GetInstance();
    IServiceDiscovery::ptr sd = app ? app->getServiceDiscovery() : nullptr;
    if (!sd) {
        return;
    }
    for (auto& item : removed) {
        sd->unregisterServer(item.domain, item.service, item.addr);
        INFO(g_logger) << "unregisterService(module deactivate) domain=" << item.domain << " service=" << item.service << " addr=" << item.addr
                       << " type=" << item.server_type;
    }
}

RockModule::RockModule(const std::string& name, const std::string& version, const std::string& filename)
    : Module(name, version, filename, ROCK)
{
}

bool RockModule::handleRequest(Message::ptr req, Message::ptr rsp, Stream::ptr stream)
{
    auto rock_req = std::dynamic_pointer_cast<RockRequest>(req);
    auto rock_rsp = std::dynamic_pointer_cast<RockResponse>(rsp);
    auto rock_stream = std::dynamic_pointer_cast<RockStream>(stream);
    if (!rock_req || !rock_rsp || !rock_stream) {
        WARN(g_logger) << "RockModule::handleRequest invalid message type, req=" << (req ? req->getName() : "null")
                       << " rsp=" << (rsp ? rsp->getName() : "null");
        return false;
    }
    return handleRockRequest(rock_req, rock_rsp, rock_stream);
}

bool RockModule::handleNotify(Message::ptr notify, Stream::ptr stream)
{
    auto rock_nty = std::dynamic_pointer_cast<RockNotify>(notify);
    auto rock_stream = std::dynamic_pointer_cast<RockStream>(stream);
    if (!rock_nty || !rock_stream) {
        WARN(g_logger) << "RockModule::handleNotify invalid message type, notify=" << (notify ? notify->getName() : "null");
        return false;
    }
    return handleRockNotify(rock_nty, rock_stream);
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
    std::unique_lock lock(mutex_);
    const auto& id = mod->getId();
    modules_[id] = mod;
    type2_modules_[mod->getType()][id] = mod;
}

void ModuleManager::del(const std::string& name)
{
    Module::ptr module;
    std::unique_lock lock(mutex_);
    auto it = modules_.find(name);
    if (it == modules_.end()) {
        return;
    }
    module = it->second;
    modules_.erase(it);

    auto type_it = type2_modules_.find(module->getType());
    if (type_it != type2_modules_.end()) {
        type_it->second.erase(module->getId());
        if (type_it->second.empty()) {
            type2_modules_.erase(type_it);
        }
    }
    lock.unlock();
    module->onUnload();
}

void ModuleManager::delAll()
{
    std::unique_lock lock(mutex_);
    auto tmp = modules_;
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
    std::shared_lock lock(mutex_);
    auto it = modules_.find(name);
    return it == modules_.end() ? nullptr : it->second;
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
    std::shared_lock lock(mutex_);
    for (auto& i : modules_) {
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
    std::shared_lock lock(mutex_);
    auto it = type2_modules_.find(type);
    if (it == type2_modules_.end()) {
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
