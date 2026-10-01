#include "game_rpc_service.h"
#include "chen/log/log.h"
#include "chen/rpc/rpc_server.h"
#include "chen/rpc/serializer.h"

namespace chen {
// PlayerData的序列化特化实现
template <>
struct rpc::Serialization<chen::game::PlayerData> {
    static void write(ByteArray::ptr ba, const chen::game::PlayerData& player) {
        ba->writeFint32(player.playerId);
        ba->writeStringVint(player.name);
        ba->writeFint32(player.level);
        ba->writeFint32(player.exp);
        ba->writeFint32(player.hp);
    }

    static void read(ByteArray::ptr ba, chen::game::PlayerData& player) {
        player.playerId = ba->readFint32();
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
PlayerData getPlayerInfo(int32_t playerId) {
    return g_game_service->getPlayerInfo(playerId);
}

bool updatePlayerLevel(int32_t playerId, int32_t newLevel) {
    return g_game_service->updatePlayerLevel(playerId, newLevel);
}

bool addPlayerExp(int32_t playerId, int32_t exp) {
    return g_game_service->addPlayerExp(playerId, exp);
}

std::vector<PlayerData> getAllPlayers() {
    return g_game_service->getAllPlayers();
}

bool syncPlayerData(const PlayerData& playerData) {
    return g_game_service->syncPlayerData(playerData);
}

bool broadcastMessage(const std::string& serverId, const std::string& message) {
    return g_game_service->broadcastMessage(serverId, message);
}

GameRpcService::GameRpcService() {
    // 初始化游戏服务数据
    initGameData();
}

void GameRpcService::initGameData() {
    // 初始化一些测试数据
    m_playerData[1001] = PlayerData{1001, "Alice", 1, 100, 50};
    m_playerData[1002] = PlayerData{1002, "Bob", 1, 80, 30};
    m_playerData[1003] = PlayerData{1003, "Carol", 1, 120, 60};

    INFO(logger) << "Game RPC Service initialized with " << m_playerData.size() << " players";
}

void GameRpcService::registerRpcMethods(rpc::RpcServer::ptr server) {
    INFO(logger) << "Registering RPC methods for game service";

    // 注册玩家相关RPC方法
    server->registerMethod("getPlayerInfo", chen::game::getPlayerInfo);
    server->registerMethod("updatePlayerLevel", chen::game::updatePlayerLevel);
    server->registerMethod("addPlayerExp", chen::game::addPlayerExp);
    server->registerMethod("getAllPlayers", chen::game::getAllPlayers);

    // 注册跨服务器通讯方法
    server->registerMethod("syncPlayerData", chen::game::syncPlayerData);
    server->registerMethod("broadcastMessage", chen::game::broadcastMessage);

    INFO(logger) << "All RPC methods registered successfully";
}

// RPC方法实现
PlayerData GameRpcService::getPlayerInfo(int32_t playerId) {
    auto it = m_playerData.find(playerId);
    if (it != m_playerData.end()) {
        INFO(logger) << "Get player info: " << it->second.name << " (ID: " << playerId << ")";
        return it->second;
    }

    WARN(logger) << "Player not found: " << playerId;
    // 返回空的玩家数据
    return PlayerData{playerId, "", 0, 0, 0};
}

bool GameRpcService::updatePlayerLevel(int32_t playerId, int32_t newLevel) {
    auto it = m_playerData.find(playerId);
    if (it != m_playerData.end()) {
        int32_t oldLevel = it->second.level;
        it->second.level = newLevel;

        INFO(logger) << "Player " << it->second.name << " level updated: "
                    << oldLevel << " -> " << newLevel;
        return true;
    }

    WARN(logger) << "Failed to update level for player: " << playerId;
    return false;
}

bool GameRpcService::addPlayerExp(int32_t playerId, int32_t exp) {
    auto it = m_playerData.find(playerId);
    if (it != m_playerData.end()) {
        it->second.exp += exp;
        INFO(logger) << "Player " << it->second.name << " gained " << exp << " exp, total: " << it->second.exp;
        return true;
    }

    WARN(logger) << "Failed to add exp for player: " << playerId;
    return false;
}

std::vector<PlayerData> GameRpcService::getAllPlayers() {
    std::vector<PlayerData> players;
    for (const auto& pair : m_playerData) {
        players.push_back(pair.second);
    }

    INFO(logger) << "Returning info for " << players.size() << " players";
    return players;
}

bool GameRpcService::syncPlayerData(const PlayerData& playerData) {
    m_playerData[playerData.playerId] = playerData;
    INFO(logger) << "Synced player data: " << playerData.name
                << " (Level: " << playerData.level << ", Exp: " << playerData.exp << ")";
    return true;
}

bool GameRpcService::broadcastMessage(const std::string& serverId, const std::string& message) {
    INFO(logger) << "Broadcast message from " << serverId << ": " << message;

    // 这里可以实现向其他游戏服务器广播消息的逻辑
    // 比如调用其他服务器的RPC接口

    return true;
}

// 获取服务实例
GameRpcService::ptr GameRpcService::GetInstance() {
    return g_game_service;
}

// 设置服务实例
void GameRpcService::SetInstance(GameRpcService::ptr instance) {
    g_game_service = instance;
}

} // namespace game
} // namespace chen