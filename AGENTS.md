# AGENTS.md

给 AI 编码助手的入口说明。开始写代码前请先阅读。

## 必读

1. [`docs/framework-first.md`](docs/framework-first.md) — **优先使用框架封装，不要重复造轮子**
2. 对应模块的 README：`chen/<module>/README.md`（log / config / db / http / rpc / orm ...）
3. [`CLAUDE.md`](CLAUDE.md) — 构建命令、架构、代码风格

## 硬性要求

- 先查框架 API（`grep -rn "class .*Util\|class .*Mgr\|typedef Singleton" chen/`），再考虑标准库 / 自研
- 日志、配置、时间、字符串、文件、DB、网络、协程、指标一律走框架封装
- 新增通用能力放 `chen/<module>/`，补 README 与测试
- 代码风格遵循 `.clang-format` 与 `CLAUDE.md`
- 不要用 `node.as<bool>()`（yaml-cpp 的 `convert<bool>` 未随 `libchen.so` 导出）

## 详细文档

- 日志配置：`docs/log-configuration.md`
- 数据库配置：`docs/db-configuration.md`
- 模块开发 / 生命周期：`docs/module-development.md`、`docs/module-lifecycle.md`
- 版本计划：`docs/roadmap.md`
