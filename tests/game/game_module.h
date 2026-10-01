/**
 * @file game_module.h
 * @brief 游戏模块 - 启动游戏RPC服务
 * @author Christins
 * @date 2025-01-21
 */
#ifndef __GAME_MODULE_H__
#define __GAME_MODULE_H__

#include "chen/module/module.h"
#include "chen/rpc/rpc_server.h"
#include "game_rpc_service.h"

namespace chen {
namespace game {

/**
 * @class GameModule
 * @brief 游戏模块 - 负责初始化和启动游戏RPC服务
 */
class GameModule : public Module {
public:
    /**
     * @brief 构造函数
     */
    GameModule();

    /**
     * @brief 模块加载时调用
     */
    bool onLoad() override;

    /**
     * @brief 模块卸载时调用
     */
    bool onUnload() override;

    /**
     * @brief 服务器准备就绪时调用
     */
    bool onServerReady() override;

    /**
     * @brief 服务器启动完成时调用
     */
    bool onServerUp() override;

private:
    /// 游戏RPC服务实例
    GameRpcService::ptr m_gameService;
    /// RPC 服务器引用（用于 onUnload 时注销方法）
    rpc::RpcServer::ptr m_rpcServer;
};

} // namespace game
} // namespace chen

#endif // __GAME_MODULE_H__