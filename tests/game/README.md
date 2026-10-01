# Game Module — 游戏模块示例

演示如何基于 Chen 框架编写可热重载的游戏服务模块。

## 文件

| 文件 | 说明 |
|------|------|
| `game_module.h/cc` | 模块定义，实现 `onLoad`/`onUnload`/`onServerReady`/`onServerUp` |
| `game_rpc_service.h/cc` | GameRpcService：游戏业务逻辑 + RPC 方法注册/注销 |

## 生命周期

```
onLoad:          创建 GameRpcService, SetInstance
onServerReady:   向 RpcServer 注册方法, 向 GameServer 注册 EchoHandler
onServerUp:      打印就绪日志
onUnload:        注销 RPC 方法, 释放 GameRpcService
```

## RPC 方法

| 方法名 | 功能 |
|--------|------|
| `getPlayerInfo` | 查询玩家信息 |
| `updatePlayerLevel` | 更新玩家等级 |
| `addPlayerExp` | 增加玩家经验 |
| `getAllPlayers` | 获取所有玩家 |
| `syncPlayerData` | 跨服同步玩家数据 |
| `broadcastMessage` | 跨服广播消息 |

## 编译

```bash
cd build && cmake --build . --target game_module -j$(nproc)
```

产物：`bin/module/libgame_module.so`
