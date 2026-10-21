# AGENTS.md

本 SDK 面向 AI 辅助开发。开始写代码前请先阅读：

## 必读

1. `docs/framework-first.md` — **优先使用框架封装，不要重复造轮子**
2. `include/chen/<module>/README.md` — 各模块 API（log / config / http / rpc / db / orm ...）
3. `include/` 下的公开头文件

## 硬性要求

- 只 include `<chen/...>`，链接 `lib/libchen.so`
- 三方库（yaml-cpp / tinyxml2 / jsoncpp / hiredis-vip / protobuf）已静态进 `libchen.so`，不要再链接
- 不要用 `node.as<bool>()`：用 `Config::Lookup<bool>` 或 `LexicalCast<std::string, bool>`
- 模块在 `onUnload` 里注销 `registerMethod` / servlet / EventBus
- 热重载会换 worker 进程，跨重载数据落 Redis / MySQL，或在 `onDeactivate` flush
- 代码风格：根目录 `.clang-format`，C++20
