# DASALL

DASALL 是一个面向桌面、边缘和嵌入式 Linux 场景的跨平台 C++ Agent OS。它以 contracts 冻结共享语义，以 profiles 驱动编译与运行治理，以 runtime 作为唯一全局主控，把 cognition、llm、memory、knowledge、tools 等子系统收敛为可长期运行、可观测、可恢复、可裁剪的工程化 Agent 底座。

Contract-first, profile-driven, long-running C++ Agent OS for local-first and hybrid deployments.

> 当前仓库处于持续实现与生产级缺口收敛阶段。大量架构骨架、Focused tests、部署与打包脚本已经落地，但整体仍未宣称 GA。README 重点说明当前定位、结构、入口与开发方式，而不是过度承诺完整产品态能力。
>
> 授权说明：DASALL 当前不是开源软件。仓库源码仅供阅读与评审；除非事先取得 Gan GAN 的书面授权，否则不得使用、运行、复制、修改、分发、再许可或商业化本仓库任何部分。书面授权联系邮箱：Whisky.Gan@gmail.com。

## 目录

- [项目定位](#项目定位)
- [核心特性](#核心特性)
- [架构与仓库结构](#架构与仓库结构)
- [常用二进制入口](#常用二进制入口)
- [快速开始](#快速开始)
- [Profile 体系](#profile-体系)
- [测试与验证](#测试与验证)
- [文档导航](#文档导航)
- [参与开发](#参与开发)
- [项目状态](#项目状态)
- [许可证](#许可证)

## 项目定位

DASALL 不是单一聊天程序，也不是把 LLM 直接接到工具调用上的薄封装。它的目标是提供一套可长期运行、可审计、可恢复、可部署到多种入口与多种硬件形态的 Agent 系统底座。

它面向以下典型场景：

- x86 桌面或工作站上的本地 Agent Runtime
- ARM Embedded Linux 与边缘节点上的受约束部署
- 本地模型、局域网模型、云模型混合路由
- 单 Agent 到多 Agent 的渐进式扩展
- CLI、daemon、gateway、TUI 等多入口统一控制面

## 核心特性

- 契约优先：先冻结 contracts，再实现各子系统，避免共享语义漂移。
- 控制与认知分离：LLM 参与推理，不接管系统主流程；Runtime 保持唯一全局主控。
- Profile 驱动：通过 desktop_full、cloud_full、edge_balanced、edge_minimal、factory_test 管理裁剪和运行策略。
- 多入口部署：仓库同时提供 TUI、CLI、本地 daemon、HTTP gateway、simulator 等入口壳层。
- 可观测与可恢复：日志、trace、metric、audit、checkpoint、deadline、降级和恢复链路是一等能力。
- 子系统解耦：cognition、llm、memory、knowledge、tools、services、infra、platform 各自 owner 清晰。

## 架构与仓库结构

整体上可以把 DASALL 理解为“contracts 与 profiles 横切全局，runtime 负责主控，cognition 负责建议，能力子系统提供治理后的可消费能力，infra/platform 提供稳定底座”。

### 关键目录

| 目录 | 作用 |
| --- | --- |
| `contracts/` | 冻结跨模块共享对象、错误码、事件与边界守卫，是全仓库的语义基线。 |
| `runtime/` | Agent Control Plane，负责主循环、FSM、预算、检查点、恢复准入与全局主控。 |
| `cognition/` | 感知、规划、推理、反思与响应构建，输出建议而不是最终执行裁定。 |
| `llm/` | 模型适配、路由、Prompt 资产与治理。 |
| `memory/` | 上下文编排、摘要压缩、记忆写回与会话支持。 |
| `knowledge/` | 检索、证据构建、索引与知识增强能力。 |
| `tools/` | 工具治理、校验、执行、审计与 observation digest。 |
| `services/` | 对外部执行与数据能力做服务封装，供 tools 受控调用。 |
| `infra/` | logging、metrics、tracing、config、secret、health、policy、diagnostics 等底座。 |
| `platform/` | Linux/ARM 等 OS、线程、文件、网络、IPC、HAL 抽象。 |
| `profiles/` | Build profile 与 runtime policy 的裁剪和投影。 |
| `access/` + `apps/` | 接入层与产品入口，当前包括 CLI、daemon、gateway、simulator、TUI。 |
| `tests/` | contract、unit、integration、fixture、mock 等多层测试。 |
| `docs/` | 架构、蓝图、专项 TODO、交付物、部署文档与 worklog。 |

### 设计原则

- 控制与认知分离
- 认知与执行分离
- 平台与业务分离
- 契约优先
- 可裁剪、可定制
- 可观测
- 可恢复

## 常用二进制入口

| 二进制 | 说明 |
| --- | --- |
| `dasall` | TUI 正式入口。适合交互式终端体验。 |
| `dasall-cli` | 结构化本地控制面入口，适合 `ping`、`readiness`、`run`、配置与运维脚本。 |
| `dasall-daemon` | 本地 UDS daemon，负责受控运行时装配与请求处理。 |
| `dasall-gateway` | HTTP gateway 入口，适合对外提供接入面。 |
| `dasall_simulator` | 模拟器与集成联调入口。 |

说明：bare `dasall` 当前代表 TUI 入口；非交互式结构化控制面任务应使用 `dasall-cli`。

## 快速开始

### 环境要求

- Linux 环境，推荐 x86_64 或 ARM64
- 支持 C++20 的编译器（GCC 或 Clang）
- CMake 3.19+（使用 preset 时）或 CMake 3.16+（手工 configure 时）
- Ninja 或 GNU Make
- 若依赖不在仓库子模块或本地缓存中，允许 configure 阶段通过 FetchContent 解析第三方依赖

第三方依赖解析顺序固定为：submodule > local cache > FetchContent。

### 方式一：使用仓库预设

```bash
cmake --preset vscode-linux-ninja
cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_gateway
ctest --preset vscode-linux-ninja --output-on-failure
```

构建输出目录位于 `build/vscode-linux-ninja/`。

### 方式二：使用通用命令行 configure

```bash
cmake -S . -B build-ci -G Ninja
cmake --build build-ci --target dasall-cli dasall-daemon dasall_gateway
ctest --test-dir build-ci --output-on-failure
```

如果本机没有 Ninja，可以改用：

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
```

### 启动本地 daemon

```bash
mkdir -p /tmp/dasall-demo
chmod 700 /tmp/dasall-demo

./build-ci/apps/daemon/dasall-daemon \
  --socket-path /tmp/dasall-demo/control.sock
```

如果你使用的是 preset 构建，请把上面的 `build-ci` 替换为 `build/vscode-linux-ninja`。

### 使用 CLI 检查控制面

```bash
./build-ci/apps/cli/dasall-cli \
  --socket-path /tmp/dasall-demo/control.sock \
  ping

./build-ci/apps/cli/dasall-cli \
  --socket-path /tmp/dasall-demo/control.sock \
  readiness

./build-ci/apps/cli/dasall-cli \
  --socket-path /tmp/dasall-demo/control.sock \
  run '{"prompt":"binary smoke"}'
```

其中：

- `ping` 用于确认 daemon 可达
- `readiness` 用于查看本地控制面 readiness 状态
- `run` 用于检查主链连通性；完整生产路径还依赖 profile、LLM 资产与 secret 配置

### 可选：启用 TUI 构建

TUI 使用 FTXUI，默认不在仓库基础构建中启用。如果你要构建 TUI：

```bash
cmake -S . -B build-tui -G Ninja -DDASALL_ENABLE_TUI_FTXUI=ON
cmake --build build-tui --target dasall-tui
./build-tui/apps/tui/dasall
```

## Profile 体系

仓库当前内置 5 档 profile，用于统一管理编译裁剪与运行策略，而不是在业务代码里散落平台分支。

| Profile | 建议场景 |
| --- | --- |
| `desktop_full` | 本地工作站/桌面开发与较完整能力基线 |
| `cloud_full` | 云侧或资源更充足的部署形态 |
| `edge_balanced` | 边缘节点上的平衡型能力裁剪 |
| `edge_minimal` | 资源紧张设备上的最小可用能力集 |
| `factory_test` | 工厂测试、验证或受控集成环境 |

Profile 资产位于 [profiles/](profiles/)；详细约束见 [docs/architecture/DASALL_profiles模块详细设计.md](docs/architecture/DASALL_profiles模块详细设计.md)。

## 测试与验证

仓库采用 contract、unit、integration 分层测试，并强调针对改动切片的 Focused validation。

常用命令：

```bash
cmake --build build-ci --target dasall_unit_tests
ctest --test-dir build-ci --output-on-failure
```

如果你在处理某个子系统，优先使用聚焦命令，例如：

```bash
ctest --test-dir build-ci -R "Runtime|Cognition|Memory|Knowledge" --output-on-failure
```

更多部署与 installed-package 验证入口见：

- [docs/deploy/daemon/README.md](docs/deploy/daemon/README.md)
- [scripts/packaging/README.md](scripts/packaging/README.md)

## 文档导航

如果你第一次进入这个仓库，建议按下面顺序建立上下文：

1. [docs/architecture/DASALL_Agent_architecture.md](docs/architecture/DASALL_Agent_architecture.md)
2. [docs/architecture/DASALL_Engineering_Blueprint.md](docs/architecture/DASALL_Engineering_Blueprint.md)
3. [docs/plans/DASALL_工程落地实现步骤指引.md](docs/plans/DASALL_工程落地实现步骤指引.md)

按主题继续深入时，可参考：

- 架构总览：[docs/architecture/](docs/architecture/)
- 交付物与评估：[docs/deliverables/](docs/deliverables/)
- 部署与运维：[docs/deploy/](docs/deploy/)
- 专项 TODO：[docs/todos/](docs/todos/)
- 开发记录：[docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_开发执行记录.md)

## 参与开发

仓库当前还没有单独的 `CONTRIBUTING.md`。对未获授权的第三方，本仓库不授予贡献或使用许可；对已获得 Gan GAN 书面授权的协作者，协作约束已经体现在设计文档、TODO、Deliverable 与 Worklog 中。建议按下面的节奏参与：

1. 先确认目标子系统的详细设计、ADR 边界与 TODO 切片。
2. 变更共享契约前，先更新设计与 contracts 消费面，再提交实现。
3. 保持依赖方向稳定：上层依赖下层，Runtime 保持唯一全局主控，Cognition 只输出建议。
4. 每个改动都附带 Focused build/test 命令，不依赖“只看 diff”的主观验收。
5. 若任务来自专项交付，完成后把证据回写到对应 deliverable 与 worklog。

对仓库边界最关键的三条 ADR：

- ADR-006：ContextOrchestrator 负责语义上下文，PromptComposer 负责消息装配
- ADR-007：ReflectionEngine 负责失败语义判断，RecoveryManager 负责恢复执行
- ADR-008：AgentOrchestrator 负责全局主控，MultiAgentCoordinator 负责协同子域编排

## 项目状态

- 当前状态：活跃开发中，持续做子系统评估、边界收敛与生产级缺口治理
- 适合用途：架构研究、子系统实现、定向回归、部署与 packaging 基线建设
- 尚未承诺：整体 GA、全部入口一键式生产交付、所有子系统一次性端到端闭环

如果你的目标是快速理解仓库，请优先从 CLI + daemon 的本地控制面主链开始，再逐步进入 profile、cognition、memory、knowledge 与 packaging gate。

## 许可证

本仓库采用专有授权 [LICENSE](LICENSE)。源码仅供阅读与评审；任何使用均需 Gan GAN 事先书面授权。授权联系邮箱：Whisky.Gan@gmail.com。