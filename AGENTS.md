# AGENTS.md

给 AI 编码助手的入口说明。开始写代码前请先阅读。

## 必读

**动手前先确认 AI 自己跑在什么系统上**（`uname -s`）。本项目只在 Linux（x86_64 / arm64）上构建与运行，见 README 与 CI（
`.github/workflows/build.yml`）。

- **Linux 环境**：可以自行执行构建与验证。
- **非 Linux 环境（macOS / Windows 等）**：**禁止**执行任何编译和运行操作，包括但不限于
  `make`、`make build`、直接调用 `cmake` / `ninja`、`make dist`、启动 `bin/server`（含 `-s` / `-d`）。
  - 非 Linux 上的编译结果没有参考价值，还会污染 `build/`、`dist/`。
  - 正确做法：改完代码后，把需要在 Linux 开发机上执行的命令、预期输出、需要重点观察的日志/报错整理给用户，由用户在 Linux
    开发机上验证，**等用户回报结果后再继续**。
  - 允许：静态检查（读代码、grep、`git diff`）、以及不涉及编译的依赖/代码生成步骤（`./deps.py sync`、`make deps`、`make orm`
    ）。若这些步骤本身因缺工具跑不通，直接告知用户，不要强行绕开。
  - 不得把“本地没报错”当作验证通过；最终以用户 Linux 侧编译结果为准。
  - 用户回报编译错误时，依据日志修改代码，再重新给出验证命令。

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
