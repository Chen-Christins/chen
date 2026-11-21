#include "GameRpcService.hpp"
#include "src/Log/Log.hpp"
#include "src/Rpc/RpcServer.hpp"
#include "src/Rpc/Serializer.hpp"

namespace chen {
// PlayerData的序列化特化实现
template <>
struct rpc::Serialization<chen::game::PlayerData> {
    static void write(ByteArray::ptr ba, const chen::game::PlayerData& player)
    {
        ba->writeFint32(player.player_id);
        ba->writeStringVint(player.name);
        ba->writeFint32(player.level);
        ba->writeFint32(player.exp);
        ba->writeFint32(player.hp);
    }

    static void read(ByteArray::ptr ba, chen::game::PlayerData& player)
    {
        player.player_id = ba->readFint32();
        player.name = ba->readStringVint();
        player.level = ba->readFint32();
        player.exp = ba->readFint32();
        player.hp = ba->readFint32();
    }
}; // namespace chen::rpc

namespace game {

static Logger::ptr logger = LOG_NAME("game_rpc");

// 全局游戏服务实例
GameRpcService::ptr g_game_service;

// 全局RPC函数实现
PlayerData get_player_info(int32_t player_id)
{
    return g_game_service->get_player_info(player_id);
}

bool update_player_level(int32_t player_id, int32_t new_level)
{
    return g_game_service->update_player_level(player_id, new_level);
}

bool add_player_exp(int32_t player_id, int32_t exp)
{
    return g_game_service->add_player_exp(player_id, exp);
}

std::vector<PlayerData> get_all_players()
{
    return g_game_service->get_all_players();
}

bool sync_player_data(const PlayerData& player_data)
{
    return g_game_service->sync_player_data(player_data);
}

bool broadcastMessage(const std::string& server_id, const std::string& message)
{
    return g_game_service->broadcastMessage(server_id, message);
}

GameRpcService::GameRpcService()
{
    // 初始化游戏服务数据
    initGameData();
}

void GameRpcService::initGameData()
{
    // 初始化一些测试数据
    player_data_[1001] = PlayerData{1001, "Alice", 1, 100, 50};
    player_data_[1002] = PlayerData{1002, "Bob", 1, 80, 30};
    player_data_[1003] = PlayerData{1003, "Carol", 1, 120, 60};

    INFO(logger) << "Game RPC Service initialized with " << player_data_.size() << " players";
}

void GameRpcService::registerRpcMethods(rpc::RpcServer::ptr server)
{
    INFO(logger) << "Registering RPC methods for game service";

    // 注册玩家相关RPC方法
    server->registerMethod("getPlayerInfo", chen::game::get_player_info);
    server->registerMethod("updatePlayerLevel", chen::game::update_player_level);
    server->registerMethod("addPlayerExp", chen::game::add_player_exp);
    server->registerMethod("getAllPlayers", chen::game::get_all_players);

    // 注册跨服务器通讯方法
    server->registerMethod("syncPlayerData", chen::game::sync_player_data);
    server->registerMethod("broadcastMessage", chen::game::broadcastMessage);

    INFO(logger) << "All RPC methods registered successfully";
}

// RPC方法实现
PlayerData GameRpcService::get_player_info(int32_t player_id)
{
    auto it = player_data_.find(player_id);
    if (it != player_data_.end()) {
        INFO(logger) << "Get player info: " << it->second.name << " (ID: " << player_id << ")";
        return it->second;
    }

    WARN(logger) << "Player not found: " << player_id;
    // 返回空的玩家数据
    return PlayerData{player_id, "", 0, 0, 0};
}

bool GameRpcService::update_player_level(int32_t player_id, int32_t new_level)
{
    auto it = player_data_.find(player_id);
    if (it != player_data_.end()) {
        int32_t old_level = it->second.level;
        it->second.level = new_level;

        INFO(logger) << "Player " << it->second.name << " level updated: " << old_level << " -> " << new_level;
        return true;
    }

    WARN(logger) << "Failed to update level for player: " << player_id;
    return false;
}

bool GameRpcService::add_player_exp(int32_t player_id, int32_t exp)
{
    auto it = player_data_.find(player_id);
    if (it != player_data_.end()) {
        it->second.exp += exp;
        INFO(logger) << "Player " << it->second.name << " gained " << exp << " exp, total: " << it->second.exp;
        return true;
    }

    WARN(logger) << "Failed to add exp for player: " << player_id;
    return false;
}

std::vector<PlayerData> GameRpcService::get_all_players()
{
    std::vector<PlayerData> players;
    for (const auto& pair : player_data_) {
        players.push_back(pair.second);
    }

    INFO(logger) << "Returning info for " << players.size() << " players";
    return players;
}

bool GameRpcService::sync_player_data(const PlayerData& player_data)
{
    player_data_[player_data.player_id] = player_data;
    INFO(logger) << "Synced player data: " << player_data.name << " (Level: " << player_data.level
                 << ", Exp: " << player_data.exp << ")";
    return true;
}

bool GameRpcService::broadcastMessage(const std::string& server_id, const std::string& message)
{
    INFO(logger) << "Broadcast message from " << server_id << ": " << message;

    // 这里可以实现向其他游戏服务器广播消息的逻辑
    // 比如调用其他服务器的RPC接口

    return true;
}

// 获取服务实例
GameRpcService::ptr GameRpcService::GetInstance()
{
    return g_game_service;
}

// 设置服务实例
void GameRpcService::SetInstance(GameRpcService::ptr instance)
{
    g_game_service = instance;
}

} // namespace game
} // namespace chen