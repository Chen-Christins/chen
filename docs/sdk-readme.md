# Chen SDK @VERSION@

C++20 协程服务器框架 Chen 的 SDK 包。

## 目录结构

| 路径         | 说明                                |
|--------------|-------------------------------------|
| `include/`   | 框架头文件（保持 `chen/...` 结构）  |
| `lib/`       | `libchen.so`                        |
| `bin/`       | 可执行程序                          |
| `protocol/`  | 协议定义与转换工具                  |
| `resources/` | 协议资源（与 `protocol/` 内容一致） |
| `tools/`     | 工具脚本与二进制                    |

## 使用

- 编译器：支持 C++20
- 头文件目录：`include/`
- 链接库：`lib/libchen.so`

## 模块开发

模块入口的声明（`CreateModule` / `DestroyModule`）、生命周期
（`onLoad` → `onServerReady` → `onServerUp` → `onTick` → `onDeactivate` → `onUnload`）
与热重载约定见 `include/chen/module/README.md`。

各模块的使用方法见对应模块目录下的 `README.md`，例如：

- `include/chen/log/README.md` — 日志
- `include/chen/config/README.md` — 配置
- `include/chen/http/README.md` — HTTP / WebSocket
- `include/chen/rpc/README.md` — RPC
- `include/chen/orm/README.md` — ORM

> SDK 打包时会把 `chen/**/README.md` 复制到 `include/` 下对应的模块目录。

## 系统依赖

`libchen.so` 已静态链接 yaml-cpp / tinyxml2 / jsoncpp / hiredis-vip / protobuf，
部署时只需安装：

- libsqlite3
- libevent
- libmysqlclient
- libnghttp2
- libboost-context
- openssl

> 三方依赖不随 SDK 分发。下游工程在自己的 `dependencies.json` 中声明
> `deps-<arch>.zip` 并下载到本地 `deps/`，用于编译模块和程序。

## YAML 使用约束

`YAML::convert<bool>::decode` 定义在 yaml-cpp 的 `convert.cpp`（头文件里只有声明），
未随 `libchen.so` 导出。因此不要在业务代码里直接调用：

```cpp
bool v = node["x"].as<bool>();   // 模块加载时会报 undefined symbol
```

请改用：

```cpp
// 方式一：配置系统
auto enable = chen::Config::Lookup<bool>("server.enable", false, "enable");

// 方式二：与 ConfigVar<bool> 相同的转换规则（true/false/1/0，大小写兼容）
bool v = chen::LexicalCast<std::string, bool>()(node["x"].as<std::string>());
```
