/**
 * @file ServerRegistry.cc
 * @brief server 注册表实现
 * @author Christins
 * @date 2026-02-06
 * @copyright GPL-3.0
 */
#include "ServerRegistry.hpp"

#include <cstdlib>
#include <mutex>

#include "Game/GameServer.hpp"
#include "Http/HttpServer.hpp"
#include "Http/WSServer.hpp"
#include "Rock/RockServer.hpp"
#include "Rpc/RpcServer.hpp"
#include "Util/Env.hpp"

namespace chen {

static Logger::ptr logger = LOG_NAME("system");

// 必须在这里声明：Config::LoadFromConfDir() 只认已注册的 ConfigVar，
// 声明晚了 YAML 里的 servers: 段会被丢掉
static ConfigVar<std::vector<TcpServerConf>>::ptr g_servers_conf =
    Config::Lookup("servers", std::vector<TcpServerConf>(), "server conf");

void ServerRegistry::registerBuiltInServers()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        TcpServerFactory::Register("http",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server =
                    std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
                if (conf.negotiate_h2) {
                    server->setNegotiateH2(true);
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("http2",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server =
                    std::make_shared<http::HttpServer>(conf.keepalive != 0, process_worker, io_worker, accept_worker);
                server->setType("http2");
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("ws",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                return TcpServer::ptr(new http::WSServer(process_worker, io_worker, accept_worker));
            });
        TcpServerFactory::Register("rpc",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                auto server = std::make_shared<rpc::RpcServer>(process_worker, io_worker, accept_worker);
                if (auto it = conf.args.find("relay"); it != conf.args.end() && it->second == "1") {
                    server->setRelay(true);
                }
                if (auto it = conf.args.find("relay_timeout"); it != conf.args.end()) {
                    server->setRelayTimeout(strtoul(it->second.c_str(), nullptr, 0));
                }
                return TcpServer::ptr(server);
            });
        TcpServerFactory::Register("game",
            [](const TcpServerConf& conf, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                uint32_t magic = 0xC0DECAFE;
                size_t header_size = 16;
                if (auto it = conf.args.find("magic"); it != conf.args.end()) {
                    magic = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 0));
                }
                if (auto it = conf.args.find("header_size"); it != conf.args.end()) {
                    header_size = strtoul(it->second.c_str(), nullptr, 0);
                }
                auto parser = game::CreateLengthFieldParser(header_size, magic);
                return TcpServer::ptr(
                    new game::GenericProtocolServer(parser, process_worker, io_worker, accept_worker));
            });
        TcpServerFactory::Register("rock",
            [](const TcpServerConf&, IOManager* process_worker, IOManager* io_worker, IOManager* accept_worker) {
                return TcpServer::ptr(new RockServer("rock", process_worker, io_worker, accept_worker));
            });
    });
}

bool ServerRegistry::createFromConf(const std::vector<TcpServerConf>& conf)
{
    registerBuiltInServers();

    for (auto& i : conf) {
        DEBUG(logger) << std::endl << LexicalCast<TcpServerConf, std::string>()(i);

        std::vector<Address::ptr> address;
        for (auto& a : i.address) {
            size_t pos = a.find(":");
            if (pos == std::string::npos) {
                address.push_back(UnixAddress::ptr(std::make_shared<UnixAddress>(a)));
                continue;
            }
            int32_t port = atoi(a.substr(pos + 1).c_str());
            auto addr = IPAddress::Create(a.substr(0, pos).c_str(), port);
            if (addr) {
                address.push_back(addr);
                continue;
            }
            std::vector<std::pair<Address::ptr, uint32_t>> result;
            if (Address::GetInterfaceAddresses(result, a.substr(0, pos))) {
                for (auto& x : result) {
                    auto ipaddr = std::dynamic_pointer_cast<IPAddress>(x.first);
                    if (ipaddr) {
                        ipaddr->setPort(atoi(a.substr(pos + 1).c_str()));
                    }
                    address.push_back(ipaddr);
                }
                continue;
            }

            if (auto aaddr = Address::LookupAny(a)) {
                address.push_back(aaddr);
                continue;
            }
            ERROR(logger) << "invalid address: " << a;
            return false;
        }

        // master 以空 worker 构造，fork 后由 worker 通过 setWorkers() 注入真实线程池
        TcpServer::ptr server = TcpServerFactory::Create(i, nullptr, nullptr, nullptr);
        if (!server) {
            ERROR(logger) << "invalid server type=" << i.type << LexicalCast<TcpServerConf, std::string>()(i);
            return false;
        }
        if (!i.name.empty()) {
            server->setName(i.name);
            EnvMgr::GetInstance()->setEnv("server", i.name);
        }
        if (std::vector<Address::ptr> fails; !server->bind(address, fails, i.ssl)) {
            for (auto& x : fails) {
                ERROR(logger) << "bind address fail:" << *x;
            }
            return false;
        }
        if (i.ssl) {
            if (!server->loadCertificates(i.cert_file, i.key_file)) {
                ERROR(logger) << "loadCertificates fail, cert_file=" << i.cert_file << " key_file=" << i.key_file;
                return false;
            }
        }
        server->setConf(i);
        servers_[i.type].push_back(server);
    }
    return true;
}

bool ServerRegistry::createFromConfigured()
{
    return createFromConf(g_servers_conf->getValue());
}

bool ServerRegistry::getServer(const std::string& type, std::vector<TcpServer::ptr>& svrs)
{
    auto it = servers_.find(type);
    if (it == servers_.end()) {
        return false;
    }
    svrs = it->second;
    return true;
}

void ServerRegistry::getAllServers(std::vector<TcpServer::ptr>& svrs)
{
    for (auto& [type, servers] : servers_) {
        svrs.insert(svrs.end(), servers.begin(), servers.end());
    }
}

void ServerRegistry::forEach(const std::function<void(const std::string&, const TcpServer::ptr&)>& fn) const
{
    for (auto& [type, servers] : servers_) {
        for (auto& server : servers) {
            fn(type, server);
        }
    }
}

} // namespace chen
