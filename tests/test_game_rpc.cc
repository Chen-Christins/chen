#include "game/game_rpc_service.h"
#include "chen/rpc/rpc_client.h"
#include "chen/socket/address.h"
#include "chen/iomanager/iomanager.h"
#include "chen/log/log.h"
#include "chen/rpc/serializer.h"
#include <iostream>

static chen::Logger::ptr logger = LOG_NAME("test_game_rpc");

// PlayerData的序列化特化实现（客户端版本）
template <>
struct chen::rpc::Serialization<chen::game::PlayerData> {
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

void testRpcClient() {
    std::cout << "\n=== 测试游戏RPC客户端 ===\n" << std::endl;

    // 创建RPC客户端
    chen::rpc::RpcClient::ptr client = std::make_shared<chen::rpc::RpcClient>();

    // 连接到RPC服务器
    auto addr = chen::Address::LookupAny("127.0.0.1:9090");
    if (!client->connect(addr, 3000)) {
        std::cerr << "Failed to connect to RPC server" << std::endl;
        return;
    }

    std::cout << "✓ 成功连接到RPC服务器" << std::endl;

    try {
        // 测试获取玩家信息
        std::cout << "\n--- 测试获取玩家信息 ---" << std::endl;
        auto playerInfo = client->call<chen::game::PlayerData>("getPlayerInfo", 1001);
        std::cout << "玩家信息: ID=" << playerInfo.playerId
                  << ", Name=" << playerInfo.name
                  << ", Level=" << playerInfo.level
                  << ", Exp=" << playerInfo.exp << std::endl;

        // 测试增加经验值
        std::cout << "\n--- 测试增加经验值 ---" << std::endl;
        bool result = client->call<bool>("addPlayerExp", 1001, 50);
        std::cout << "增加经验结果: " << (result ? "成功" : "失败") << std::endl;

        // 再次获取玩家信息查看变化
        auto updatedInfo = client->call<chen::game::PlayerData>("getPlayerInfo", 1001);
        std::cout << "更新后经验值: " << updatedInfo.exp << std::endl;

        // 测试获取所有玩家
        std::cout << "\n--- 测试获取所有玩家 ---" << std::endl;
        auto allPlayers = client->call<std::vector<chen::game::PlayerData>>("getAllPlayers");
        std::cout << "服务器中共有 " << allPlayers.size() << " 个玩家:" << std::endl;
        for (const auto& player : allPlayers) {
            std::cout << "  - " << player.name << " (ID:" << player.playerId
                      << ", Lv." << player.level << ")" << std::endl;
        }

        // 测试广播消息
        std::cout << "\n--- 测试广播消息 ---" << std::endl;
        result = client->call<bool>("broadcastMessage", "test_client", "Hello from RPC client!");
        std::cout << "广播消息结果: " << (result ? "成功" : "失败") << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "RPC调用失败: " << e.what() << std::endl;
    }

    client->close();
    std::cout << "\n✓ RPC客户端测试完成" << std::endl;
}

int main(int argc, char** argv) {
    chen::IOManager ioManager(1);
    ioManager.schedule(testRpcClient);
    ioManager.stop();
    return 0;
}