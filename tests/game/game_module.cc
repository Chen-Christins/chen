#include "game_module.h"
#include "game_rpc_service.h"
#include "chen/application.h"
#include "chen/tcp/tcp_server.h"
#include "chen/rpc/rpc_server.h"
#include "chen/log/log.h"
#include "chen/game/game_server.h"
#include <arpa/inet.h>

namespace chen {
namespace game {

static Logger::ptr logger = LOG_NAME("game_module");

class EchoHandler : public chen::game::IMessageHandler {
public:
    explicit EchoHandler(chen::game::GenericProtocolServer::ptr server)
        : m_server(std::move(server)) {}

    bool handle(uint32_t connId, uint32_t cmdId, const std::vector<uint8_t>& header, const std::vector<uint8_t>& body) override {
        auto srv = m_server.lock();
        if (!srv) {
            return false;
        }
        // 默认头: [magic][cmd][seq][len]
        if (header.size() < 16) {
            return false;
        }
        uint32_t seq = ntohl(*reinterpret_cast<const uint32_t*>(header.data() + 8));
        INFO(logger) << "game echo cmd=" << cmdId << " seq=" << seq
                     << " body_len=" << body.size() << " conn=" << connId;
        return srv->sendMessage(connId, cmdId, seq, body);
    }

    std::string getName() const override { return "EchoHandler"; }

private:
    std::weak_ptr<chen::game::GenericProtocolServer> m_server;
};

GameModule::GameModule()
    : Module("game_module", "1.0.0", "game_module") {
}

bool GameModule::onLoad() {
    INFO(logger) << "Game module loading...";
    m_gameService = std::make_shared<GameRpcService>();
    // 设置全局实例
    GameRpcService::SetInstance(m_gameService);
    return true;
}

bool GameModule::onUnload() {
    INFO(logger) << "Game module unloading...";
    // 先注销 RPC 方法（销毁 m_handlers 中的旧 lambda），再 dlclose
    // 否则 lambda 内部的 destroy 函数指针指向已卸载 .so 会 SEGV
    if (m_rpcServer) {
        m_rpcServer->unregisterMethod("getPlayerInfo");
        m_rpcServer->unregisterMethod("updatePlayerLevel");
        m_rpcServer->unregisterMethod("addPlayerExp");
        m_rpcServer->unregisterMethod("getAllPlayers");
        m_rpcServer->unregisterMethod("syncPlayerData");
        m_rpcServer->unregisterMethod("broadcastMessage");
        m_rpcServer.reset();
    }
    m_gameService.reset();
    return true;
}

bool GameModule::onServerReady() {
    INFO(logger) << "Game module server ready";

    // 获取RPC服务器实例
    std::vector<TcpServer::ptr> rpc_servers;
    if (Application::GetInstance()->getServer("rpc", rpc_servers)) {
        for (auto& server : rpc_servers) {
            auto rpc_server = std::dynamic_pointer_cast<rpc::RpcServer>(server);
            if (rpc_server) {
                INFO(logger) << "Registering RPC methods on server: " << server->getName();
                m_gameService->registerRpcMethods(rpc_server);
                // 保存引用，供 onUnload 时注销方法
                m_rpcServer = rpc_server;
            }
        }
    } else {
        WARN(logger) << "No RPC server found, game RPC methods not registered";
    }

    // 为 game 协议服务器注册一个默认 echo 处理器 (cmdId=1)
    std::vector<TcpServer::ptr> game_servers;
    if (Application::GetInstance()->getServer("game", game_servers)) {
        for (auto& server : game_servers) {
            auto game_server = std::dynamic_pointer_cast<game::GenericProtocolServer>(server);
            if (game_server) {
                INFO(logger) << "Registering default game handler on server: " << server->getName();
                game_server->registerHandler(1, std::make_shared<EchoHandler>(game_server));
            }
        }
    } else {
        WARN(logger) << "No game server found, default handler not registered";
    }

    return true;
}

bool GameModule::onServerUp() {
    INFO(logger) << "Game module server up - Game RPC Service is ready";
    return true;
}

} // namespace game
} // namespace chen

extern "C" {
// 模块入口点
chen::Module* CreateModule() {
    chen::Module* module = new chen::game::GameModule;
    INFO(chen::game::logger) << "CreateModule " << module;
    return module;
}

void DestroyModule(chen::Module* module) {
    INFO(chen::game::logger) << "DestroyModule " << module;
    delete module;
}

}