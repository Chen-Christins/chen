/**
 * @file GameRpcService.hpp
 * @brief 游戏RPC服务 - 提供服务器间通讯的游戏逻辑接口
 * @author Christins
 * @date 2025-01-21
 * @copyright GPL-3.0
 */
#pragma once

#include "src/Rpc/RpcServer.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace chen::game {

/**
 * @brief 玩家数据结构
 */
struct PlayerData {
    int32_t player_id; // 玩家ID
    std::string name; // 玩家名称
    int32_t level;    // 玩家等级
    int32_t exp;      // 玩家经验值
    int32_t hp;       // 玩家血量
};

/**
 * @class GameRpcService
 * @brief 游戏RPC服务类 - 处理服务器间的游戏逻辑调用
 */
class GameRpcService : public std::enable_shared_from_this<GameRpcService> {
public:
    typedef std::shared_ptr<GameRpcService> ptr;

    /**
     * @brief 构造函数
     */
    GameRpcService();

    /**
     * @brief 注册RPC方法到RPC服务器
     * @param server RPC服务器实例
     */
    void registerRpcMethods(chen::rpc::RpcServer::ptr server);

    // ========================================
    // RPC方法 - 这些方法会被注册为RPC接口
    // ========================================

    /**
     * @brief 获取玩家信息
     * @param player_id 玩家ID
     * @return PlayerData 玩家数据
     */
    PlayerData get_player_info(int32_t player_id);

    /**
     * @brief 更新玩家等级
     * @param player_id 玩家ID
     * @param new_level 新等级
     * @return bool 是否成功
     */
    bool update_player_level(int32_t player_id, int32_t new_level);

    /**
     * @brief 增加玩家经验值
     * @param player_id 玩家ID
     * @param exp 经验值
     * @return bool 是否成功
     */
    bool add_player_exp(int32_t player_id, int32_t exp);

    /**
     * @brief 获取所有玩家信息
     * @return std::vector<PlayerData> 玩家列表
     */
    std::vector<PlayerData> get_all_players();

    /**
     * @brief 同步玩家数据（用于服务器间数据同步）
     * @param player_data 玩家数据
     * @return bool 是否成功
     */
    bool sync_player_data(const PlayerData& player_data);

    /**
     * @brief 广播消息到其他服务器
     * @param server_id 发送方服务器ID
     * @param message 消息内容
     * @return bool 是否成功
     */
    bool broadcastMessage(const std::string& server_id, const std::string& message);

    /**
     * @brief 获取单例实例
     * @return GameRpcService::ptr
     */
    static GameRpcService::ptr GetInstance();

    /**
     * @brief 设置全局实例
     * @param instance 实例指针
     */
    static void SetInstance(GameRpcService::ptr instance);

private:
    /**
     * @brief 初始化游戏数据
     */
    void initGameData();

private:
    /// 玩家数据存储 (player_id -> PlayerData)
    std::map<int32_t, PlayerData> player_data_;
};

} // namespace chen::game
