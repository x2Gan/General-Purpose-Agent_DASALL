# DASALL CLI config 交互式部署配置专项 TODO

最近更新时间：2026-05-08
阶段：Detailed Design -> Special TODO
适用范围：`apps/cli` 本地 `config` 工作流、`apps/daemon` package-mode 配置入口、`infra/secret` bootstrap 导入接缝、`debian/` 安装后引导、`tests/unit/apps/cli`、`tests/integration/apps/cli`、`tests/integration/infra/secret`、`scripts/packaging`
当前结论：`dasall-cli config` 方案方向 Ready，CLCFG-TODO-001~005 的设计前置冻结已完成，但工程实现仍处于 Build Not Started。当前仓库已具备 CLI 本地命令骨架、daemon `validate-only`、file secret backend 与 SecretManager 读链、daemon/CLI 本地控制面基础测试；其中 CLCFG-TODO-001/002/003/004/005 已完成并冻结 config v1 命令 grammar、TTY/non-TTY、exit contract、安装态 canonical socket=`/run/dasall/daemon.sock`、`0600 root/sudo-only` operator model、安装态 `dasall config` 命令名、`InstallState` / `ConfigActionPlan` / `ConfigFileWriteTransaction` 的 P0 口径、bootstrap-only secret import seam，以及 `ToolSkillPageMode=hidden|summary_only|editable` 的 P0/P1/P2 capability 边界与未决问题处置；`CLCFG-BLK-002`、`CLCFG-BLK-003` 已随之解阻。建议按 P0/P1/P2 三阶段推进：P0 先交付 profile/daemon/service 的本地配置闭环；P1 再补 LLM secret onboarding；P2 才开放 tools/skills operator surface 的真实编辑能力。

## 1. 概述与目标

### 1.1 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md`
2. `docs/architecture/DASALL-cli本地控制面详细设计.md`
3. `docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md`
4. `docs/architecture/DASALL_llm子系统详细设计.md`
5. `docs/architecture/DASALL_tools子系统详细设计.md`
6. `docs/architecture/DASALL_infra_secret模块详细设计.md`
7. `docs/plans/DASALL_工程落地实现步骤指引.md`
8. `docs/worklog/DASALL_开发执行记录.md`
9. `docs/todos/cli/DASALL-cli本地控制面专项TODO.md`
10. `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`
11. `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`
12. `docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md`
13. `docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md`
14. 当前代码与测试现状：`apps/cli/src/*`、`apps/daemon/src/*`、`access/include/daemon/*`、`infra/include/secret/*`、`infra/src/secret/*`、`tests/unit/access/Cli*`、`tests/unit/apps/cli/*`、`tests/unit/infra/secret/*`、`tests/integration/access/*`
15. 最佳实践基线：AWS CLI `configure`、GitHub CLI `auth login`、Docker post-install、Kubernetes `kubeadm init`、HashiCorp Vault `operator init`

编制原则：

1. 不改写 ADR-006、ADR-007、ADR-008 的 owner 边界。
2. 不把 `config` 扩张成 Runtime 主控、daemon 自部署 owner 或第二配置中心。
3. 每个任务必须包含代码目标、测试目标、验收命令三件套。
4. 已完成的 CLI/daemon/secret 基线必须复用，不重复规划为未开始任务。
5. 设计缺口先写成冻结或解阻任务，再进入 Build 任务。
6. P0 只承诺 profile/daemon/service 本地配置闭环；LLM secret onboarding 和 tools/skills operator surface 分阶段推进。

### 1.2 专项目标

1. 把安装后首次部署与后续修改统一收敛为一个本地 `config` 工作流命令族。
2. 让 `config show/plan/validate/apply` 成为结构化、可测试、可脚本消费的 operator surface。
3. 把 profile、daemon 配置、service enable/start、summary 展示纳入 P0 最小闭环。
4. 为 P1 的 LLM secret onboarding 留出 bootstrap-only secret import seam，但不破坏既有 `ISecretManager` 消费接口。
5. 为 P2 的 tools/skills 页面保留 capability-gated 演进空间，而不是在 P0 伪造“默认安装能力可任意启停”的错觉。

### 1.3 范围边界

纳入范围：

1. `apps/cli` 本地 `config` 子命令 grammar、state probe、action plan、interactive wizard、summary formatter。
2. `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 的受控读写与事务语义。
3. `dasall-daemon --validate-only`、`systemctl start/restart/reload/enable` 的受控编排与 preflight。
4. 安装态 `dasall config` onboarding 文案、README.Debian、postinst next steps、installed-package smoke。
5. P1 的 LLM secret bootstrap 导入接缝与 redacted summary。

不纳入范围：

1. daemon 内部 Runtime 主链、恢复执行、调度与多 Agent 编排。
2. provider baseline / prompt baseline / profile baseline 资产的直接编辑。
3. tools/skills/plugin 的 operator-facing enable list 在 owner 未冻结前的正式开放。
4. 远程控制面、streaming attach、GUI wizard、云端引导服务。

### 1.4 本轮评审后修订口径

本轮评审后，本文档采用以下修订口径作为后续实施基线：

1. 当前状态从“设计已 Ready”收敛为“方案方向 Ready，P0 Build 前置冻结未完成”。
2. P0 operator access 先采纳 `0600 root/sudo-only`，`0660 dasall group` 仅作为后续演进项，不再在 P0 主路径中二选一摇摆。
3. 所有本地构建与测试验收命令统一使用 `vscode-linux-ninja` CMake preset，避免复用既有构建目录时重新指定不一致的生成器。
4. 组件命名以 config 设计文档为准：`ConfigPreflightChecker`、`ConfigPlanFormatter`、`ConfigSummaryFormatter` 是 P0 输出/校验面的一组稳定 owner。
5. `tests/integration/apps/cli` 与 `unit;cli;config` / `integration;cli;config` label 拓扑必须先由 CLCFG-TODO-018 落盘，再进入 workflow 集成测试。
6. CLCFG-TODO-012 和 CLCFG-TODO-015 虽保留任务 ID，但内部必须按子切片分别验收：`show` / `plan` / `validate` 不混验，service action / installed onboarding 不混验。

## 2. 当前状态

### 2.1 当前代码与文档证据

| 证据对象 | 当前状态 | 对 TODO 的直接含义 |
|---|---|---|
| `apps/cli/src/CliCommandParser.cpp` | 当前只有 `help/version/ping/readiness/run/submit/status/cancel/diag` | `config` grammar 与本地分发入口尚不存在 |
| `apps/cli/src/main.cpp` | 当前只有 `help/version` 为本地命令，其余全部走 daemon IPC | `config` 必须新增第三条本地工作流路径 |
| `docs/todos/cli/DASALL-cli本地控制面专项TODO.md` | CLI 专项 `CLI-TODO-001` ~ `014` 已全部完成 | config 不能重复规划 parser/formatter/wire baseline，只能在其上扩展 |
| `apps/daemon/src/DaemonConfigValidator.*` | 已具备 `validate-only` 入口且不创建 listener | P0 可复用为配置写入后的第一道执行校验 |
| `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md` | daemon 本地控制面专项已 close-ready，socket mode 当前真实 policy 为 `0600` | operator access model 不能再模糊承诺“加组即可访问” |
| `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md` | package-ready 尚未完成；当前工作树尚未落 `debian/` payload 与安装态命令别名 | `dasall config` 的 installed-package 交付需要和 packaging 并行推进，且必须由对应任务显式创建或接入 `debian/` 目录 |
| `infra/include/secret/ISecretManager.h` | 仅暴露 `get/materialize/release/rotate/revoke/inspect` | secret bootstrap 写入面不存在，P1 必须新增 bootstrap-only 接缝 |
| `infra/src/secret/backends/FileSecretBackend.*` | file backend 已能按受控 `root_dir` 读取加密 fixture 且不生成额外明文文件 | P1 可复用现有 file backend 作为安装态 secret sink |
| `infra/src/secret/SecretManagerFacade.*` | 已具备读链、lease 生命周期、轮换与审计/健康基线 | P1 无需重写 secret owner，只需补 bootstrap 导入面 |
| `docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md` | `CFG-BLK-003` 仍提示 `secret://` 解析契约曾是 blocker | config/secret 协同任务必须把 bootstrap 写入与 `secret://` 投影边界写清楚 |
| `docs/architecture/DASALL_cli_config交互式部署配置设计.md` | 已冻结 config 命令定位、状态机、页面顺序、P0/P1/P2 分层 | TODO 可直接按该设计进入 Design -> Build 拆解 |

### 2.2 总体判断

1. `config` 功能的真实缺口不在 daemon 基础能力，而在 CLI 本地 orchestration、canonical sink 写入、secret bootstrap 写入面和安装态 onboarding。
2. P0 的 build-tree 路径可以在现有仓库基线上推进，但前提是先冻结 socket/operator model、命令 grammar、action plan schema、配置写入事务口径和测试拓扑；installed-package 路径必须等待 packaging payload 接线。
3. P1 与 P2 不适合和 P0 绑成一个大任务，否则会重新把 secret owner、tools owner 和 package owner 的边界混在一起。
4. 当前最稳妥的工程路径是：先交付 `config show/plan/validate` 与交互式 P0 向导，再把 secret 和 tools/skills 作为后续 capability-gated 扩展补进来。

## 3. 约束条件

### 3.1 约束清单

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| CLCFG-TC001 | config 设计 5.2；CLI 详设 1.2 | Must | `config` 必须是 CLI 本地 orchestration 命令，而不是 daemon RPC | `main.cpp` 必须新增 local workflow dispatch |
| CLCFG-TC002 | Ubuntu DPKG 打包设计 7.7.3/7.7.4 | Must | 安装阶段保持非交互，首次部署交互发生在安装后显式运行 `config` | packaging 任务必须走 README/postinst next steps，而不是 maintainer script 交互 |
| CLCFG-TC003 | ADR-008；daemon/CLI 设计 | Must-Not | `config` 不得接管 Runtime 主控、恢复执行或 daemon 生命周期 owner | 任务只能规划文件写入、校验、service manager 编排 |
| CLCFG-TC004 | config 设计 7.1；打包设计 7.6/7.10.9 | Must | profile 与 daemon 配置只能落到 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` | 不允许新增 `~/.dasall/config.yaml` 平行真相源 |
| CLCFG-TC005 | LLM 设计 6.15.2/6.15.4 | Must | LLM secret 不得写入 provider baseline 资产或 `daemon.json` | P1 必须引入 secret sink |
| CLCFG-TC006 | `ISecretManager` 当前 ABI | Must | public ABI 只有 get/materialize/release/rotate/revoke/inspect，无 create/set | secret bootstrap 写入必须是 bootstrap-only internal seam |
| CLCFG-TC007 | daemon 专项评审与 DMD-TODO-037 | Must | 当前真实 socket policy 以 `0600` 为基线 | P0 若不先改 daemon/package，不得承诺 group-based operator access |
| CLCFG-TC008 | config 设计 4.4/5.1 | Must | `config` 必须支持 `show/plan/validate` 等非修改路径；交互式 wizard 只是 desired state 的一种输入方式 | 必须先实现 action plan，再实现页面流程 |
| CLCFG-TC009 | AWS/GitHub/kubeadm/Vault 实践 | Should | 当前值预填、回车保留旧值、支持非 TTY fail-closed 与 file-driven headless 入口 | `CliCommandParser` 与 prompt engine 必须同时考虑 TTY/non-TTY |
| CLCFG-TC010 | tools 设计 6.5.4/6.12.5 | Must | tools/skills operator surface 未冻结前只能 capability-gated | ToolSkillPage 首版只能隐藏或只读摘要 |
| CLCFG-TC011 | 工程规范与 TODO 基线 | Must | 每项任务必须给出代码目标、测试目标、验收命令三件套 | 不允许只列抽象目标 |
| CLCFG-TC012 | 命名约定与打包设计 | Must | 开发态 `dasall-cli config` 与安装态 `dasall config` 必须同时覆盖 | parser/help、manpage、postinst、installed-package smoke 要同步收口 |

## 4. Design Track 映射

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| `config` 命令族 grammar 与 TTY/non-TTY 语义 | 设计 5.1、6.1、10.1 | 补设计 / 本地命令 contract | CLCFG-TODO-001、006 | 先冻结命令面与退出语义，再进入 parser/main 改造 |
| 安装态 socket/operator model 与命令安装名 | 设计 3.3/3.4、6.2.6、7.1 | 评审解阻 / 部署契约 | CLCFG-TODO-002、015 | 先解决 `0600` vs `0660`、`dasall-cli` vs `dasall`，否则 wizard 页面会说错话 |
| InstallState、ActionPlan、事务语义 | 设计 6.1、8.1、9.2/9.3 | 类型冻结 / 配置写入模型 | CLCFG-TODO-003、007、009、013 | 让计划、写入、回滚与 service action 有结构化 owner |
| secret bootstrap-only import seam | 设计 6.2.4、8.4、11.2 | 评审解阻 / secret owner 接缝 | CLCFG-TODO-004、016、021 | 不扩写 `ISecretManager` 公共消费接口，只补 bootstrap internal seam |
| ToolSkillPage capability-gated 边界 | 设计 6.2.5、12.3 | 评审解阻 / P2 预留 | CLCFG-TODO-005、017 | 先冻结“隐藏或只读”，再谈可编辑 page |
| state probe / preflight / service manager | 设计 6.1、6.2.1、8.3 | 组件骨架 / 主流程 | CLCFG-TODO-008、010、012、014 | 把 current state、preflight 与 service action 从页面逻辑里剥出来 |
| interactive wizard 与 summary | 设计 6.2、8.1、14 | UX / 工作流 | CLCFG-TODO-011、014 | 交互页序列必须建立在已冻结的 plan/apply 模型之上 |
| 测试、installed-package gate 与证据收口 | 设计 13、14；TODO 基线 | 测试 / QA / 收口 | CLCFG-TODO-018、019、020、021、022 | build-tree 和 installed-package 两层 gate 都要补齐 |

## 5. Build Track 映射

| Build 切片 | 当前缺口 | 对应任务 ID | 预期产物 |
|---|---|---|---|
| CLI grammar 与 local dispatch | parser/main 无 `config` 子命令 | CLCFG-TODO-006 | `config/show/plan/validate/apply` 可被解析和分发 |
| Config 类型与 state probe | 缺 `InstallState`、`ConfigActionPlan`、`InstallStateProbe` | CLCFG-TODO-007、008 | 结构化状态与只读探测骨架 |
| 文件写入与事务 | 缺 `DaemonConfigFileStore` 与原子写回模型 | CLCFG-TODO-009、013 | `/etc/default` 与 `daemon.json` 受控写入 |
| service action 与 preflight | 缺 `systemctl` 适配、root/systemd 检测和 plan -> apply 执行器 | CLCFG-TODO-010、012、013、015 | validate/start/restart/enable 的本地编排闭环 |
| interactive wizard | 缺 prompt engine、页面状态机、默认值复用与 review/apply | CLCFG-TODO-011、014 | 可交互的 P0 向导 |
| secret onboarding | 缺 bootstrap 写入 seam 与 redacted summary | CLCFG-TODO-016、021 | P1 的 LLM key 受控落盘与验证 |
| tools/skills capability 页 | 缺 owner-surface 与 operator-facing enable list | CLCFG-TODO-017 | P2 的 capability-gated ToolSkillPage |
| 测试 / packaging / closeout | 缺 config 专属 unit/integration、installed-package smoke 与最终 gate | CLCFG-TODO-018 ~ 022 | build-tree + installed-package 双层验收 |

## 6. 任务表

### 6.1 补设计 / 评审解阻任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLCFG-TODO-001 | Done | 冻结 config v1 命令 grammar、TTY/non-TTY 与 exit contract | config 设计 4.7、5.1、10.1；CLI 详设命令面 | `config`、`config show`、`config plan`、`config validate`、`config apply --from-file`；TTY 要求；非 TTY fail-closed；exit family | L2 | `docs/architecture/DASALL_cli_config交互式部署配置设计.md`；`docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-001-config-grammar与TTY语义冻结.md` | `CliCommand.name`；`CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`CliExitDecision` 对本地 config disposition 的扩展策略 | process：命令矩阵与 usage 一致性评审；focused parser regression 入口盘点 | `rg -n "config show|config plan|config validate|config apply|TTY|dry-run|--from-file" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-001-config-grammar与TTY语义冻结.md` | 无 | 无 | grammar、TTY 和 exit disposition 形成唯一口径 | deliverable 文档；更新后的设计与专项 TODO | 仅当 `config` 命令族、TTY/non-TTY 语义、`plan/apply` 边界与本地 exit 口径都可二值评审时完成 |
| CLCFG-TODO-002 | Done | 冻结 P0 安装态 socket canonical path、root/sudo-only operator access model 与命令安装名 | config 设计 3.3/3.4、6.2.6；打包设计 7.6/7.7/7.10.9；daemon 评审与 `DMD-TODO-037` | 安装态唯一 socket path；P0 采用 `0600 root/sudo-only`；`0660 dasall group` 仅作为后续演进项记录；开发态 `dasall-cli` 与安装态 `dasall` 的映射 | L2 | `docs/architecture/DASALL_cli_config交互式部署配置设计.md`；`docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`；`docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-002-socket与operator-model冻结.md` | `DaemonEndpointDefaults`；`DaemonSocketPolicy`；README/postinst/operator 文案口径 | process：socket/operator/access model 一致性评审 | `rg -n "0600 root/sudo-only|0660 dasall group|后续演进|/run/dasall|/usr/bin/dasall|dasall-cli" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-002-socket与operator-model冻结.md` | 无 | CLCFG-BLK-001、CLCFG-BLK-004 | daemon、packaging、config 三方对安装态路径、P0 root/sudo-only 模型与安装态命令名达成一致 | deliverable 文档；相关专项 TODO 回链 | 仅当 wizard 不再需要猜测 socket/access 模型，P0 不再展示加组主路径，且安装态命令名唯一时完成 |
| CLCFG-TODO-003 | Done | 冻结 InstallState、ActionPlan schema 与配置文件事务语义 | config 设计 6.1、8.1、9.2/9.3、10.3 | `FreshInstall/BootstrapPending/ConfiguredStopped/ConfiguredRunning/Drifted/Unsupported`；`file_writes/secret_writes/service_*` 主键；`manual_followups/blocked_actions`；临时文件、fsync、rename、备份与失败回滚 | L2 | `docs/architecture/DASALL_cli_config交互式部署配置设计.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-003-state-plan-transaction冻结.md` | `InstallState`；`ConfigActionPlan`；`ConfigFileWriteTransaction` | process：state/plan/transaction 字段一致性评审 | `rg -n "FreshInstall|BootstrapPending|ConfiguredStopped|ConfiguredRunning|Drifted|Unsupported|file_writes|secret_writes|service_validate_requested|service_reload_required|service_restart_required|service_start_requested|service_enable_requested|manual_followups|blocked_actions|rename|fsync" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/cli/deliverables/CLCFG-TODO-003-state-plan-transaction冻结.md` | 无 | 无 | state、plan、transaction 三套模型冻结 | deliverable 文档 | 仅当后续实现不再需要在代码里临时发明 action plan 键或回滚步骤时完成 |
| CLCFG-TODO-004 | Done | 冻结 bootstrap-only secret import seam 与 `secret://` 投影契约 | config 设计 6.2.4、8.4、11.2；secret 设计 6.7/6.8/6.9；infra config `CFG-BLK-003` | `SecretBootstrapWriter` 所在 owner；file backend root；bootstrap import 输入/输出；`auth_ref` 生成与 redacted summary 规则 | L2 | `docs/architecture/DASALL_cli_config交互式部署配置设计.md`；`docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md`；`docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-004-secret-bootstrap-seam冻结.md` | `SecretBootstrapWriter`；`SecretProvisioningResult`；`auth_ref` projection；`secret://` naming rule | process：config/secret/config-center 三方契约评审 | `rg -n "SecretBootstrapWriter|secret://|auth_ref|bootstrap-only|CFG-BLK-003|FileSecretBackend|SecretManagerFacade" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-004-secret-bootstrap-seam冻结.md` | 无 | 无（`CLCFG-BLK-002` 已于 2026-05-08 经本任务解阻） | secret owner 与 config-center 对 bootstrap/projection 契约回链完成 | deliverable 文档；相关专项 TODO 回链 | 仅当 P1 不再依赖口头说明“以后会有写入 API”，且 `secret://` 投影规则可测试时完成 |
| CLCFG-TODO-005 | Done | 冻结 ToolSkillPage 的 P0/P1/P2 capability 边界与未决问题处置 | config 设计 6.2.5、12.3；tools 设计 6.5.4/6.12.5 | P0 隐藏或只读摘要；P1 继续不开放；P2 才允许 editable page；bundle/source/allowlist/撤销路径展示要求 | L2 | `docs/architecture/DASALL_cli_config交互式部署配置设计.md`；`docs/todos/tools/DASALL_tools子系统专项TODO.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-005-toolskill-capability边界冻结.md` | `ConfigCapabilityResolver::tool_skill_capability()`；`ToolSkillPageMode` | process：tools/config owner scope review | `rg -n "ToolSkillPage|capability-gated|allowlist|PluginSkillBundleImporter|SkillRegistry|SkillRuntime" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-005-toolskill-capability边界冻结.md` | 无 | 无（CLCFG-BLK-003 已于 2026-05-08 经本任务解阻） | 无 | deliverable 文档；相关专项 TODO 回链 | 仅当 P0 不再伪装支持 editable tools/skills，而 P2 的开放前提写清楚时完成 |

### 6.2 骨架与公共接口面任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLCFG-TODO-006 | NotStarted | 扩展 CLI parser/main 以支持 config 子命令族 | config 设计 5.1；CLI 详设 parser/main contract | `config/show/plan/validate/apply` grammar、usage、local-only dispatch | L3 | `apps/cli/src/CliCommandParser.h`；`apps/cli/src/CliCommandParser.cpp`；`apps/cli/src/main.cpp`；`tests/unit/access/CliDaemonCommandParserTest.cpp`；新增 `tests/unit/apps/cli/ConfigCommandParserTest.cpp` | `CliCommandParser::parse()`；`CliCommandParser::usage_string()`；`main()` local config dispatch | unit：`ConfigCommandParserTest`；回归 `CliDaemonCommandParserTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall_unit_tests && ctest --preset vscode-linux-ninja -R "CliDaemonCommandParserTest|ConfigCommandParserTest"` | CLCFG-TODO-001 | 无 | grammar 已冻结 | parser/main 代码与单测 | 仅当 `config` 子命令解析不影响既有 daemon-facing 命令，且 usage 文本稳定时完成 |
| CLCFG-TODO-007 | NotStarted | 定义 config 类型面：InstallState、ActionPlan、DesiredConfig、ApplyResult | config 设计 6.1、7、9.3 | 状态枚举、计划对象、review diff、应用结果、manual followups | L2 | 新增 `apps/cli/src/config/ConfigCommandTypes.h`；`apps/cli/src/config/ConfigCommandTypes.cpp`；新增 `tests/unit/apps/cli/ConfigCommandTypesTest.cpp` | `InstallState`；`ConfigActionPlan`；`DesiredConfigSnapshot`；`ConfigApplyResult` | unit：`ConfigCommandTypesTest` 验证默认值、必填键与状态投影 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests && ctest --preset vscode-linux-ninja -R "ConfigCommandTypesTest"` | CLCFG-TODO-003 | 无 | state/plan schema 已冻结 | config 类型头源与单测 | 仅当 P0 所需状态、计划和应用结果都具备单一类型 owner 时完成 |
| CLCFG-TODO-008 | NotStarted | 新增 InstallStateProbe 与 ConfigCapabilityResolver 骨架 | config 设计 6.1、6.2.1、8.1 | canonical 文件、service 状态、secret 缺口、systemd 可用性与 capability 探测 | L2 | 新增 `apps/cli/src/config/InstallStateProbe.h`；`apps/cli/src/config/InstallStateProbe.cpp`；`apps/cli/src/config/ConfigCapabilityResolver.h`；`apps/cli/src/config/ConfigCapabilityResolver.cpp`；新增 `tests/unit/apps/cli/InstallStateProbeTest.cpp`；`tests/unit/apps/cli/ConfigCapabilityResolverTest.cpp` | `InstallStateProbe::probe()`；`ConfigCapabilityResolver::resolve()` | unit：state probe 正反例与 capability-gated 分支 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests && ctest --preset vscode-linux-ninja -R "InstallStateProbeTest|ConfigCapabilityResolverTest"` | CLCFG-TODO-003、005 | CLCFG-BLK-001、CLCFG-BLK-003 | socket/operator/toolskill 边界已冻结 | probe/capability 组件与单测 | 仅当 `FreshInstall` 到 `Unsupported` 的状态投影可重复验证，且 capability 不再在页面代码中散落判断时完成 |
| CLCFG-TODO-009 | NotStarted | 新增 DaemonConfigFileStore 与配置事务 helper | config 设计 6.2.3、7.1、8.1、9.3 | `/etc/default/dasall-daemon`、`/etc/dasall/daemon.json` 的读写、备份、回滚与权限保留 | L2 | 新增 `apps/cli/src/config/DaemonConfigFileStore.h`；`apps/cli/src/config/DaemonConfigFileStore.cpp`；新增 `tests/unit/apps/cli/DaemonConfigFileStoreTest.cpp` | `load_current()`；`write_desired()`；`rollback_last_write()`；`ConfigFileWriteTransaction` | unit：成功写入、部分失败回滚、权限保留、invalid JSON 拒绝 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests && ctest --preset vscode-linux-ninja -R "DaemonConfigFileStoreTest"` | CLCFG-TODO-003 | 无 | transaction contract 已冻结 | file store 头源与单测 | 仅当 CLI 不再直接拼写 `/etc` 文件格式，且失败回滚有自动化覆盖时完成 |
| CLCFG-TODO-010 | NotStarted | 新增 ServiceManagerAdapter、PrivilegeProbe 与 ConfigPreflightChecker | config 设计 4.4、6.2.8、8.3、10.2 | root 检测、TTY/systemd 检测、`systemctl` 编排、`validate-only` preflight、non-systemd degrade | L2 | 新增 `apps/cli/src/config/ServiceManagerAdapter.h`；`apps/cli/src/config/ServiceManagerAdapter.cpp`；`apps/cli/src/config/PrivilegeProbe.h`；`apps/cli/src/config/ConfigPreflightChecker.h`；对应 `*.cpp`；新增 `tests/unit/apps/cli/ServiceManagerAdapterTest.cpp`；`tests/unit/apps/cli/ConfigPreflightCheckerTest.cpp` | `ServiceManagerAdapter::plan_service_actions()`；`ServiceManagerAdapter::apply()`；`PrivilegeProbe::require_root_for_write()`；`ConfigPreflightChecker::run()` | unit：systemd/no-systemd、root/non-root、validate-only success/failure | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests && ctest --preset vscode-linux-ninja -R "ServiceManagerAdapterTest|ConfigPreflightCheckerTest"` | CLCFG-TODO-002、003 | CLCFG-BLK-001、CLCFG-BLK-005 | operator model 与 service action schema 已冻结 | service/preflight 组件与单测 | 仅当 service 行为不再靠页面直接调用 shell 命令，且 preflight 可以独立复验时完成 |
| CLCFG-TODO-011 | NotStarted | 新增 WorkflowCoordinator、PromptEngine、PlanFormatter 与 SummaryFormatter 骨架 | config 设计 6.2、8.1、8.4、14 | coordinator 驱动、masked prompt、默认值复用、plan dry-run JSON/human 投影、summary JSON/human 投影 | L2 | 新增 `apps/cli/src/config/CliConfigWorkflowCoordinator.h`；`apps/cli/src/config/CliConfigWorkflowCoordinator.cpp`；`apps/cli/src/config/InteractivePromptEngine.h`；`apps/cli/src/config/InteractivePromptEngine.cpp`；`apps/cli/src/config/ConfigPlanFormatter.h`；`apps/cli/src/config/ConfigPlanFormatter.cpp`；`apps/cli/src/config/ConfigSummaryFormatter.h`；`apps/cli/src/config/ConfigSummaryFormatter.cpp`；新增 `tests/unit/apps/cli/ConfigPlanFormatterTest.cpp`；`tests/unit/apps/cli/ConfigSummaryFormatterTest.cpp` | `CliConfigWorkflowCoordinator::run()`；`InteractivePromptEngine::prompt_*()`；`ConfigPlanFormatter::format_*()`；`ConfigSummaryFormatter::format_*()` | unit：plan/summary formatter 与 masked/default prompt 行为 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests && ctest --preset vscode-linux-ninja -R "ConfigPlanFormatterTest|ConfigSummaryFormatterTest"` | CLCFG-TODO-001、007、008、010 | 无 | grammar/type/probe/service 骨架已存在 | workflow/prompt/plan/summary 骨架 | 仅当后续实现已有单一 coordinator owner，且 plan 与 summary 输出口径已集中时完成 |

### 6.3 配置/策略/组件实现任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLCFG-TODO-012 | NotStarted | 分步实现 `config show`、`config plan/dry-run`、`config validate` 非交互工作流 | config 设计 5.1、6.1、6.2.1、6.2.7、6.2.9 | 012.1 `show` 只读摘要；012.2 `plan`/`--dry-run` 输出 action plan；012.3 `validate` 复用 daemon `--validate-only` 且不修改系统状态 | L2 | 更新 `apps/cli/src/main.cpp`；`apps/cli/src/config/CliConfigWorkflowCoordinator.cpp`；`apps/cli/src/config/InstallStateProbe.cpp`；`apps/cli/src/config/ConfigPlanFormatter.cpp`；`apps/cli/src/config/ConfigSummaryFormatter.cpp` | `run_show()`；`run_plan()`；`run_validate()`；`ConfigActionPlan` human/JSON 输出 | unit：`ConfigShowWorkflowTest`、`ConfigPlanWorkflowTest`、`ConfigValidateWorkflowTest`；integration：`ConfigShowValidateIntegrationTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigShowWorkflowTest|ConfigPlanWorkflowTest|ConfigValidateWorkflowTest|ConfigShowValidateIntegrationTest|DaemonConfigValidatorTest"` | CLCFG-TODO-006 ~ 011 | 无 | skeleton 与 preflight 组件已落盘 | 本地只读工作流代码与测试 | 仅当 `show`、`plan/dry-run`、`validate` 三个子切片各自有输出契约、测试和无副作用证明时完成 |
| CLCFG-TODO-013 | NotStarted | 实现 ConfigDiffPlanner、apply executor 与 `config apply --from-file` | config 设计 5.1、7.5、8.1、9.2/9.3、10.1 | desired-state fixture schema、desired vs current diff、文件写入、manual followups、headless apply、rollback | L2 | 新增 `apps/cli/src/config/ConfigDiffPlanner.h`；`apps/cli/src/config/ConfigDiffPlanner.cpp`；新增 `tests/fixtures/apps/cli/config/desired_state_minimal.yaml`；更新 `CliConfigWorkflowCoordinator.cpp`；`DaemonConfigFileStore.cpp` | `ConfigDiffPlanner::build_plan()`；`apply_plan()`；`apply_from_file()`；`DesiredConfigSnapshot` parser | unit：`ConfigDiffPlannerTest`；integration：`ConfigApplyWorkflowTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigDiffPlannerTest|ConfigApplyWorkflowTest"` | CLCFG-TODO-007、009、010、012 | CLCFG-BLK-005 | action plan、desired-state fixture 与 file transaction 已冻结 | diff planner / apply executor 代码、fixture 与测试 | 仅当 `config apply --from-file` 可以在无交互模式下复用同一 action plan 管线，并在写入失败时回滚时完成 |
| CLCFG-TODO-014 | NotStarted | 实现交互式 P0 wizard 页面与当前值复用 | config 设计 6.2.1 ~ 6.2.9、14 | `WelcomeAndStatePage`、`ProfileSelectionPage`、`DaemonConfigPage`、`ReviewAndApplyPage`、`ServiceActionPage`、`SummaryPage`；回车保留当前值；P0 不展示加组主路径 | L2 | 更新 `apps/cli/src/config/CliConfigWorkflowCoordinator.cpp`；`InteractivePromptEngine.cpp`；必要时新增 `apps/cli/src/config/pages/*`；更新 `ConfigPlanFormatter.cpp`；`ConfigSummaryFormatter.cpp` | `run_interactive()`；page builder；default value reuse；masked/confirm prompt | integration：`ConfigInteractiveWizardTest`；`ConfigFreshInstallWorkflowTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigInteractiveWizardTest|ConfigFreshInstallWorkflowTest"` | CLCFG-TODO-008、010、011、012、013 | CLCFG-BLK-001、CLCFG-BLK-005 | show/plan/apply 骨架已稳定；P0 root/sudo-only operator model 已冻结 | wizard 实现、页面测试与交付说明 | 仅当 fresh install 与 existing config 两条路径都能以当前值预填、生成正确 plan，且不会误导用户通过加组获取 P0 访问权限时完成 |
| CLCFG-TODO-015 | NotStarted | 分切片接入 service action 编排与安装态 onboarding UX | config 设计 1.1、6.2.8、7.1、12.1；打包设计 7.7.4、7.10.1、7.10.9 | 015.1 service action apply/restart/enable；015.2 安装态 `dasall config` 命令、README.Debian、postinst next steps、manpage、installed-package explicit start/enable 流程 | L2 | `apps/cli/CMakeLists.txt`；新增或更新 `debian/dasall.1` 或等价 manpage；新增或更新 `debian/dasall-daemon.README.Debian`；新增或更新 `debian/dasall-daemon.postinst`；必要时 `scripts/packaging/*` | `ServiceManagerAdapter::apply()`；installed command alias；operator onboarding 文案 | integration：`ConfigModifyExistingWorkflowTest`；packaging smoke：`pkg-smoke-local-control-plane` 扩展 config 入口 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigModifyExistingWorkflowTest|CliDaemonSocketPathIntegrationTest" && test -d debian && rg -n "dasall config|README.Debian|enable --now dasall-daemon.service" debian` | CLCFG-TODO-002、010、013、014 | CLCFG-BLK-004、CLCFG-BLK-005 | service action 可在 build-tree 复验；packaging 安装态命令与 service/config payload 已可接线 | service action 代码；installed-package onboarding 代码/文档 | 仅当 service action 与 installed onboarding 两个子切片各自有独立证据，且 docs/postinst/manpage 口径一致时完成 |
| CLCFG-TODO-016 | Blocked | 实现 SecretBootstrapWriter 与 LLMSecretPage P1 onboarding | config 设计 6.2.4、8.4、12.2；secret 设计 6.7/6.8/6.9 | masked prompt、stdin/import file、file backend 导入、redacted summary、runtime verification pending | L2 | 新增 `infra/src/secret/SecretBootstrapWriter.h`；`infra/src/secret/SecretBootstrapWriter.cpp`；更新 `infra/CMakeLists.txt`；新增 `apps/cli/src/config/LlmSecretPage.*`；新增 `tests/unit/apps/cli/LlmSecretPageTest.cpp`；`tests/integration/infra/secret/SecretBootstrapWriterIntegrationTest.cpp` | `SecretBootstrapWriter::import_secret()`；`LlmSecretPage::collect_and_apply()`；`auth_ref` projection | unit：`LlmSecretPageTest`；integration：`SecretBootstrapWriterIntegrationTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_infra dasall-cli dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -R "LlmSecretPageTest|SecretBootstrapWriterIntegrationTest|FileSecretBackendTest|SecretManagerFacadeTest"` | CLCFG-TODO-004、011 | CLCFG-BLK-002 | bootstrap-only secret seam 已冻结并接入 file backend root 策略 | secret bootstrap writer、LLM page、测试与交付件 | 仅当 secret 不经普通 flag、不回写 `daemon.json`、summary 只显示 redacted ref 时完成 |
| CLCFG-TODO-017 | Blocked | 实现 ToolSkillPage capability 摘要与 P2 可编辑预留 | config 设计 6.2.5、12.3；tools 设计 6.5.4/6.12.5 | P0/P1 隐藏或只读摘要；P2 才允许 editable controls；bundle/source/allowlist/撤销提示 | L2 | 新增 `apps/cli/src/config/ToolSkillPage.*`；更新 `ConfigCapabilityResolver.cpp`；必要时新增 `tests/unit/apps/cli/ToolSkillPageTest.cpp` | `ToolSkillPage::render()`；`ToolSkillCapability`；只读 summary -> editable transition 条件 | unit：`ToolSkillPageTest` | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall_unit_tests && ctest --preset vscode-linux-ninja -R "ToolSkillPageTest|ConfigCapabilityResolverTest"` | CLCFG-TODO-005、011 | CLCFG-BLK-003 | tools owner 冻结 operator-facing deployment surface | ToolSkillPage 代码与测试 | 仅当 P0/P1 不再误导用户“默认安装 skills/tools 可任意修改”，且 P2 预留条件清晰时完成 |

### 6.4 测试支撑 / 集成 / 门禁任务

| Task ID | Status | 任务标题 | 设计依据 | 精确范围 | 粒度 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置任务 | 关联阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CLCFG-TODO-018 | NotStarted | 优先建立 config unit / integration topology 与 discoverability | 工程规范 4.1；CLI 专项测试拓扑基线 | `tests/unit/apps/cli`、新增 `tests/integration/apps/cli`、必要的 `tests/integration/infra/secret` 注册与 `unit;cli;config` / `integration;cli;config` label | L2 | 更新 `tests/unit/apps/cli/CMakeLists.txt`；`tests/unit/CMakeLists.txt`；新增 `tests/integration/apps/cli/CMakeLists.txt`；更新 `tests/integration/CMakeLists.txt` | config tests discoverability；`ctest -N` 与 `-L config` 入口；config 测试不再只借挂 `access` label | build/test：discoverability 验证 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -N | rg "ConfigCommandParserTest|InstallStateProbeTest|ConfigFreshInstallWorkflowTest"` | 无 | 无 | 测试拓扑目录、CMake 入口和 label 规范先落盘 | config 专属测试拓扑与 discoverability 证据 | 仅当 config 相关单测/集成测试有独立可发现拓扑，而不是继续借挂 access 测试时完成 |
| CLCFG-TODO-019 | NotStarted | 补齐 config focused unit / contract 回归矩阵 | config 设计 13、14；TODO 基线 | parser、types、state probe、capability、file store、diff planner、plan formatter、summary formatter、exit contract | L2 | 新增或更新 `tests/unit/apps/cli/ConfigCommandParserTest.cpp`、`ConfigCommandTypesTest.cpp`、`InstallStateProbeTest.cpp`、`ConfigCapabilityResolverTest.cpp`、`DaemonConfigFileStoreTest.cpp`、`ConfigDiffPlannerTest.cpp`、`ConfigPlanFormatterTest.cpp`、`ConfigSummaryFormatterTest.cpp`；必要时新增 `tests/contract/access/ConfigOutputContractTest.cpp` | focused unit/contract matrix | `ctest` focused gate | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall_unit_tests dasall_contract_tests && ctest --preset vscode-linux-ninja -R "ConfigCommandParserTest|ConfigCommandTypesTest|InstallStateProbeTest|ConfigCapabilityResolverTest|DaemonConfigFileStoreTest|ConfigDiffPlannerTest|ConfigPlanFormatterTest|ConfigSummaryFormatterTest|ConfigOutputContractTest"` | CLCFG-TODO-006 ~ 013、018 | 无 | config 组件已逐步落盘 | focused tests 与 contract gate | 仅当 config 核心组件均有 focused 自动化出口，且 `--json` / human summary 口径可回归时完成 |
| CLCFG-TODO-020 | NotStarted | 补齐 P0 workflow 集成与 drift repair 门 | config 设计 6.1、6.2、9、14；kubeadm 分阶段实践 | fresh install、configured running 修改、drifted repair、non-systemd degrade、service action 分支 | L2 | 新增 `tests/integration/apps/cli/ConfigFreshInstallWorkflowTest.cpp`；`tests/integration/apps/cli/ConfigModifyExistingWorkflowTest.cpp`；`tests/integration/apps/cli/ConfigDriftRepairWorkflowTest.cpp`；必要时新增 test harness | `config` interactive / plan / apply / service action integration | integration：fresh/modify/drift/non-systemd 四类路径 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigFreshInstallWorkflowTest|ConfigModifyExistingWorkflowTest|ConfigDriftRepairWorkflowTest|DaemonConfigValidatorTest"` | CLCFG-TODO-012 ~ 015、018 | CLCFG-BLK-005 | workflow 与 service adapter 已可组合执行 | config workflow integration tests | 仅当 fresh install、后续修改与 drift repair 三条主路径都可自动化复验时完成 |
| CLCFG-TODO-021 | Blocked | 补齐 secret onboarding 与 installed-package smoke / autopkgtest 门 | config 设计 12.2；打包设计 7.10.7；secret 设计 9 | P1 secret import、installed-package `dasall config` smoke、autopkgtest metadata、rootful validator script | L2 | 新增或更新 `debian/tests/control`；`debian/tests/pkg-smoke-local-control-plane`；必要时新增 `scripts/packaging/validate_cli_config_v1.sh`；`tests/integration/infra/secret/SecretBootstrapWriterIntegrationTest.cpp` | secret/install-package 双层 gate | integration：secret onboarding；installed-package：`dasall config` smoke；`autopkgtest --validate` | `autopkgtest --validate . && test -x debian/tests/pkg-smoke-local-control-plane && test -x scripts/packaging/validate_cli_config_v1.sh` | CLCFG-TODO-015、016、018、020 | CLCFG-BLK-002、CLCFG-BLK-004、CLCFG-BLK-005 | secret seam、packaging payload、validator script 全部就绪 | installed-package smoke、validator script 与相关集成测试 | 仅当 build-tree 与 installed-package 两层 gate 都覆盖 `config` 主路径时完成 |
| CLCFG-TODO-022 | NotStarted | 回写 config Gate、交付证据与统一验收入口 | TODO 基线；config 设计 14；专项收口要求 | Gate 状态、deliverables、worklog、one-shot validator 与残余 blocker/risk/OQ 回写 | L2 | 更新 `docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md`；新增 `docs/todos/cli/deliverables/CLCFG-TODO-022-config专项Gate与交付证据收口.md`；更新 `docs/worklog/DASALL_开发执行记录.md`；必要时新增 `scripts/packaging/validate_cli_config_v1.sh` | `CLCFG-GATE-*`；统一验收入口；残余 blocker/risk/OQ 状态 | process + validator：gate 一致性复验 | `test -f docs/todos/cli/deliverables/CLCFG-TODO-022-config专项Gate与交付证据收口.md && rg -n "CLCFG-TODO-|CLCFG-GATE-|CLCFG-BLK-|CLCFG-RISK-|CLCFG-OQ-" docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-022-config专项Gate与交付证据收口.md docs/worklog/DASALL_开发执行记录.md && bash scripts/packaging/validate_cli_config_v1.sh` | CLCFG-TODO-018 ~ 021 | 无 | 统一 validator 与 gate 证据已具备 | 收口 deliverable、worklog 与 one-shot validator | 仅当 config 专项每个 gate 都有正式命令、证据与残余风险说明，并具备 one-shot 验收入口时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 设计冻结 | CLCFG-TODO-001 ~ 005 | 串行为主 | 先把 grammar、socket/operator model、action plan、secret seam、ToolSkillPage 边界冻住 |
| B 骨架建账 | CLCFG-TODO-018 -> CLCFG-TODO-006 ~ 011 | 018 先行建立 CMake/label 拓扑；006/007/008/009/010 可局部并行；011 依赖前项 | 先建立 config 专属测试拓扑，再接入 parser/types/probe/store/service/workflow 骨架 |
| C P0 非交互内核 | CLCFG-TODO-012 -> 013 | 串行 | 先实现 show/plan/validate，再做 apply/from-file |
| D P0 交互与安装态接线 | CLCFG-TODO-014 -> 015 | 串行 | wizard 必须建立在稳定的 plan/apply 与 operator model 之上 |
| E focused tests 与 workflow gate | CLCFG-TODO-019 -> 020 -> 022 | 串行 | 先 focused unit/contract，再 workflow integration，最后统一收口 |
| F P1 secret onboarding | CLCFG-TODO-016 -> 021 | 阻塞解除后串行 | 先补 bootstrap seam，再做 installed-package/secret 双层 gate |
| G P2 tools/skills | CLCFG-TODO-017 | 后置 | tools owner surface 未冻结前不进入 Build |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 失败处置 |
|---|---|---|---|---|
| CLCFG-GATE-01 | Design Freeze Gate | CLCFG-TODO-001 ~ 005 后 | grammar、socket/operator model、action plan、secret seam、ToolSkillPage scope 全部无 `TBD` | 停止进入 parser/实现阶段，先补 design deliverable |
| CLCFG-GATE-02 | Skeleton Discoverability Gate | CLCFG-TODO-006 ~ 011、018 后 | config parser/types/probe/store/service/workflow 组件可编译，`ctest -N` 可发现 config tests | 停止进入 workflow 实现，先修 CMake / 测试拓扑 |
| CLCFG-GATE-03 | NonInteractive Plan Gate | CLCFG-TODO-012/013/019 后 | `show/plan/validate/apply --from-file` 可稳定输出结构化计划且 focused tests 全绿 | 停止进入交互式 wizard，先修 plan/apply 内核 |
| CLCFG-GATE-04 | Interactive P0 Gate | CLCFG-TODO-014/015/020 后 | fresh install / existing modify / drift repair 三条路径可复验，service action 与 installed onboarding 文案一致 | 停止宣称 P0 可交付，先修 wizard/service/packaging 接线 |
| CLCFG-GATE-05 | Secret Onboarding Gate | CLCFG-TODO-016/021 后 | secret import 不经普通 flag、不回写 `daemon.json`、summary 只显 redacted ref，installed-package smoke 可跑通 | 保持 P1 Blocked，不对外宣称 LLM onboarding ready |
| CLCFG-GATE-06 | Closeout Gate | CLCFG-TODO-022 后 | gate、deliverables、worklog、validator script 与 blocker/risk/OQ 状态一致 | 不进入 close-ready，仅保留 draft-ready |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| CLCFG-BLK-001 | daemon 当前真实 socket policy 为 `0600`，而安装/交互草案仍存在 `dasall` 组访问想象 | CLCFG-TODO-002、010、014、015、020 | P0 明确冻结为 `0600 root/sudo-only`；`0660 dasall group` 作为后续演进另行立项 | 在 CLCFG-TODO-002 中冻结 P0 operator model，并把 group access 从主路径移除 | 若后续确需 group access，单独联动 daemon/platform/packaging 开新任务，不影响 P0 交付 |
| CLCFG-BLK-002 | 已解阻（2026-05-08）：CLCFG-TODO-004 已冻结 `infra/secret` internal bootstrap seam、install-mode root `/var/lib/dasall/secrets`、`auth_ref=secret://llm/providers/<provider_ref>` 与 redacted summary 规则 | CLCFG-TODO-016、021 | 无；后续只需按该契约实现 `SecretBootstrapWriter`、`LLMSecretPage` 与 integration tests | 证据回链到 docs/architecture/DASALL_cli_config交互式部署配置设计.md 6.2.4/8.4/11.2、docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md、docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md 与 docs/todos/cli/deliverables/CLCFG-TODO-004-secret-bootstrap-seam冻结.md | 若后续把 bootstrap 写入并入 `ISecretManager` 公共 ABI、重开 install root 默认值或改写 `secret://` naming rule，则重新转为 Blocked |
| CLCFG-BLK-003 | 已解阻（2026-05-08）：CLCFG-TODO-005 已冻结 `ToolSkillPageMode=hidden|summary_only|editable`、P0/P1 hidden-or-summary-only、P2 editable 前提与未决问题处置 | CLCFG-TODO-017 | 无；后续只需按该契约实现 capability resolver、summary formatter 与 page mode gating | 证据回链到 docs/architecture/DASALL_cli_config交互式部署配置设计.md 6.2.5/11.2/12.3、docs/todos/tools/DASALL_tools子系统专项TODO.md 补充结论与 docs/todos/cli/deliverables/CLCFG-TODO-005-toolskill-capability边界冻结.md | 若后续在 P0/P1 暴露 editable toggle、绕过 `PluginExtensionBridge` / `SkillRegistry` / `ToolPolicyGate`，或把 external importer 写成默认安装能力，则重新转回 Blocked |
| CLCFG-BLK-004 | packaging 安装态命令别名、README/postinst/autopkgtest 仍未全量落盘 | CLCFG-TODO-002、015、021 | packaging 提供 `/usr/bin/dasall`、README/postinst next steps、installed-package smoke 入口 | 在 CLCFG-TODO-015 中与 PKG-TODO-009/013/014/016/017 对齐 | 开发态先只交付 `dasall-cli config`，安装态文档保持 next steps 占位 |
| CLCFG-BLK-005 | 缺少 rootful/systemd 集成 smoke 与 one-shot validator | CLCFG-TODO-010、013、014、015、020、021、022 | 具备 service harness 与 `validate_cli_config_v1.sh` 之类统一入口 | 先完成 CLCFG-TODO-020，随后在 021/022 落 validator script | 若 CI/rootful 环境暂缺，只能宣称 build-tree ready，不宣称 installed-package ready |

## 9. 测试矩阵与统一验收命令

### 9.1 单元测试矩阵

| 对应任务 | 测试清单 | 目的 |
|---|---|---|
| CLCFG-TODO-006 | `ConfigCommandParserTest`、`CliDaemonCommandParserTest` | 守住 `config` 子命令 grammar 与既有 CLI 命令不回归 |
| CLCFG-TODO-007 | `ConfigCommandTypesTest` | 守住 InstallState / ActionPlan / ApplyResult 类型面 |
| CLCFG-TODO-008 | `InstallStateProbeTest`、`ConfigCapabilityResolverTest` | 守住状态探测与 capability-gated 分支 |
| CLCFG-TODO-009 | `DaemonConfigFileStoreTest` | 守住 canonical 文件读写、回滚与权限语义 |
| CLCFG-TODO-010 | `ServiceManagerAdapterTest`、`ConfigPreflightCheckerTest` | 守住 preflight、systemd/no-systemd 与 root/non-root 分支 |
| CLCFG-TODO-011 | `ConfigPlanFormatterTest`、`ConfigSummaryFormatterTest` | 守住 human/JSON plan 与 summary 口径 |
| CLCFG-TODO-013 | `ConfigDiffPlannerTest` | 守住 plan -> apply 决策与 manual followups |
| CLCFG-TODO-016 | `LlmSecretPageTest` | 守住 masked prompt、redacted summary 与 secret input source |
| CLCFG-TODO-017 | `ToolSkillPageTest` | 守住 capability-gated 只读/隐藏策略 |

### 9.2 集成测试矩阵

| 对应任务 | 测试清单 | 目的 |
|---|---|---|
| CLCFG-TODO-012 | `ConfigShowValidateIntegrationTest`、`ConfigShowWorkflowTest`、`ConfigPlanWorkflowTest`、`ConfigValidateWorkflowTest` | 分别验证非交互 `show`、`plan/dry-run`、`validate` 的主路径 |
| CLCFG-TODO-013 | `ConfigApplyWorkflowTest` | 验证 apply/from-file、文件回滚与 `validate-only` 接线 |
| CLCFG-TODO-014 | `ConfigInteractiveWizardTest`、`ConfigFreshInstallWorkflowTest` | 验证交互式 fresh install 主路径与当前值复用 |
| CLCFG-TODO-015 | `ConfigModifyExistingWorkflowTest`、`CliDaemonSocketPathIntegrationTest` | 验证 existing modify 路径与安装态 endpoint 契约 |
| CLCFG-TODO-020 | `ConfigDriftRepairWorkflowTest` | 验证 drift repair 与 degrade 分支 |
| CLCFG-TODO-016/021 | `SecretBootstrapWriterIntegrationTest`、installed-package `pkg-smoke-local-control-plane` 扩展用例 | 验证 secret onboarding 与安装态 config UX |

### 9.3 质量门矩阵

| Gate ID | 正式命令 | 对应任务 |
|---|---|---|
| CLCFG-GATE-01 | `rg -n "config show|config plan|config validate|SecretBootstrapWriter|ToolSkillPage|0600|/usr/bin/dasall" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-00*.md` | CLCFG-TODO-001 ~ 005 |
| CLCFG-GATE-02 | `cmake --preset vscode-linux-ninja && cmake --build --preset vscode-linux-ninja --target dasall-cli dasall_unit_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -N | rg "ConfigCommandParserTest|InstallStateProbeTest|ConfigFreshInstallWorkflowTest"` | CLCFG-TODO-006 ~ 011、018 |
| CLCFG-GATE-03 | `cmake --build --preset vscode-linux-ninja --target dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigCommandParserTest|ConfigCommandTypesTest|InstallStateProbeTest|ConfigCapabilityResolverTest|DaemonConfigFileStoreTest|ConfigDiffPlannerTest|ConfigPlanFormatterTest|ConfigSummaryFormatterTest|ConfigOutputContractTest|ConfigShowWorkflowTest|ConfigPlanWorkflowTest|ConfigValidateWorkflowTest|ConfigApplyWorkflowTest"` | CLCFG-TODO-012、013、019 |
| CLCFG-GATE-04 | `cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_integration_tests && ctest --preset vscode-linux-ninja -R "ConfigInteractiveWizardTest|ConfigFreshInstallWorkflowTest|ConfigModifyExistingWorkflowTest|ConfigDriftRepairWorkflowTest|DaemonConfigValidatorTest"` | CLCFG-TODO-014、015、020 |
| CLCFG-GATE-05 | `autopkgtest --validate . && cmake --build --preset vscode-linux-ninja --target dasall_integration_tests && ctest --preset vscode-linux-ninja -R "SecretBootstrapWriterIntegrationTest|FileSecretBackendTest|SecretManagerFacadeTest" && bash scripts/packaging/validate_cli_config_v1.sh` | CLCFG-TODO-016、021 |
| CLCFG-GATE-06 | `rg -n "CLCFG-GATE-|CLCFG-BLK-|CLCFG-RISK-|CLCFG-OQ-" docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-022-config专项Gate与交付证据收口.md docs/worklog/DASALL_开发执行记录.md && bash scripts/packaging/validate_cli_config_v1.sh` | CLCFG-TODO-022 |

### 9.4 统一验收命令

最终统一验收入口：

`bash scripts/packaging/validate_cli_config_v1.sh`

该脚本应由 CLCFG-TODO-021/022 落盘，并至少覆盖以下步骤：

1. `cmake --preset vscode-linux-ninja`
2. `cmake --build --preset vscode-linux-ninja --target dasall-cli dasall-daemon dasall_unit_tests dasall_contract_tests dasall_integration_tests`
3. `ctest --preset vscode-linux-ninja -R "ConfigCommandParserTest|ConfigCommandTypesTest|InstallStateProbeTest|ConfigCapabilityResolverTest|DaemonConfigFileStoreTest|ServiceManagerAdapterTest|ConfigPreflightCheckerTest|ConfigPlanFormatterTest|ConfigSummaryFormatterTest|ConfigDiffPlannerTest|ConfigShowWorkflowTest|ConfigPlanWorkflowTest|ConfigValidateWorkflowTest|ConfigShowValidateIntegrationTest|ConfigApplyWorkflowTest|ConfigInteractiveWizardTest|ConfigFreshInstallWorkflowTest|ConfigModifyExistingWorkflowTest|ConfigDriftRepairWorkflowTest|DaemonConfigValidatorTest"`
4. installed-package / rootful smoke：`dasall config` 首装、修改与显式 start/enable 路径
5. 若 P1 已开启，再补 `SecretBootstrapWriterIntegrationTest` 与 redacted secret summary 验证

## 10. 风险与回退策略

### 10.1 风险表

| 风险 ID | 对应设计 Risk | 风险描述 | 触发条件 | 缓解策略 | 回退策略 |
|---|---|---|---|---|---|
| CLCFG-RISK-001 | TODO 新增 | socket/operator model 若继续摇摆，wizard 会给出错误运维建议 | `0600` 与 `0660` 并行对外承诺 | 在 CLCFG-TODO-002 中先冻结 P0 采用单一模型 | 若冻结失败，P0 删除 OperatorAccessPage 的状态修改动作，仅保留警告 |
| CLCFG-RISK-002 | TODO 新增 | config apply 部分写入后失败，导致 daemon 无法启动 | `/etc/default` 或 `daemon.json` 中途损坏 | CLCFG-TODO-003/009/013 引入事务、备份、回滚与 `validate-only` 前置校验 | 自动回滚到上一个已知有效文件，并输出手工恢复命令 |
| CLCFG-RISK-003 | 设计 11.2 | secret bootstrap 若直接扩到 `ISecretManager`，会污染既有 ABI | 为求快速交付把 create/set 并入 public consumer API | 严格采用 bootstrap-only internal seam | P1 保持 Blocked，只显示 secret 缺口，不收集明文 |
| CLCFG-RISK-004 | 设计 12.3 | ToolSkillPage 若提前开放 editable controls，会绕过 tools owner | owner surface 未冻结却给出启停按钮 | P0/P1 固定为隐藏或只读摘要 | 彻底隐藏该页面，后续靠 OQ/Blocker 重新打开 |
| CLCFG-RISK-005 | TODO 新增 | installed-package smoke 缺失时，功能可能只在 build tree 虚绿 | 没有 rootful/systemd/autopkgtest 验证 | 在 CLCFG-TODO-021/022 增加 validator script 与 installed-package smoke | 暂不宣称 package-ready，只宣称 build-tree ready |

### 10.2 回退策略

1. 文件写入失败：立即回滚最近一次 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 备份，并强制重新跑 `validate-only`。
2. service action 失败：保留新配置文件，但在 summary 中明确标记 `configured_but_not_running`，同时输出 `systemctl status` / `journalctl` next steps。
3. secret import 失败：不得写入半成品 `auth_ref`；若已创建 candidate record，则按 bootstrap seam 约定显式 revoke 或删除。
4. non-systemd 环境：不执行自动 start/enable，只保留文件写入和手工启动命令提示。
5. packaging smoke 不通过：回退到开发态 `dasall-cli config` 入口，不更新安装态 README/postinst 的主路径宣称。

## 11. 可行性结论

1. P0 可执行性：高。现有 CLI/daemon/secret 基线已足够支撑 `config show/plan/validate`、P0 wizard、canonical 文件写入、`validate-only` 与 service action 编排；真正需要先补的是设计冻结和 workflow 组件。
2. P1 可执行性：中。`FileSecretBackend`、`SecretManagerFacade`、`SecretRotationCoordinator` 已经存在，但 bootstrap 写入面缺失，因此必须先完成 CLCFG-TODO-004，再进入 CLCFG-TODO-016。
3. P2 可执行性：低。tools/skills 的 operator-facing deployment surface 尚未冻结，不适合纳入当前 Build 主线。
4. 推荐执行策略：先把 CLCFG-TODO-001 ~ 015、018 ~ 020、022 作为 P0 原子任务包推进；P1 仅在 secret seam 冻结后开启；P2 继续保持 Blocked。
5. 推荐架构取舍：为了让 P0 真正落地，建议在 CLCFG-TODO-002 中正式采纳“socket mode 维持 `0600`、P0 以 root/sudo-only 为 operator model、group access 作为后续演进”的保守方案。该方案与当前 daemon 真实行为一致，能避免把 packaging/platform/daemon 三处同时重开大改。

## 12. 未决问题处置表

| OQ ID | 问题 | 当前处置 | 影响任务 | 处置说明 | 后续动作 |
|---|---|---|---|---|---|
| CLCFG-OQ-001 | P0 是否支持 `dasall` 组访问，而不是 root/sudo-only？ | 采纳：P0 不支持；延后到后续版本 | CLCFG-TODO-002、010、014、015 | 当前 daemon 真实 socket policy 为 `0600`，先按现状交付可落地路径 | 如未来确需 group access，再联动 daemon/platform/packaging 单独立项 |
| CLCFG-OQ-002 | 安装态稳定命令是否应为 `dasall config`？ | 采纳 | CLCFG-TODO-002、015、021 | 开发态保持 `dasall-cli config`，安装态对用户公开 `dasall config` | 在 packaging 与 manpage 中同步落盘 |
| CLCFG-OQ-003 | `config apply --from-file` 是否属于 P0？ | 采纳：属于 P0，但只要求最小 headless apply | CLCFG-TODO-001、013 | CI/headless 最小入口要先冻结，否则 plan/apply 不完整 | 先支持 file-driven desired state，不承诺更复杂 CI secret 注入 |
| CLCFG-OQ-004 | secret 初始写入应否扩展 `ISecretManager`？ | 拒绝 | CLCFG-TODO-004、016 | public consumer-facing ABI 不应吸收 bootstrap 写入能力 | 采用 `SecretBootstrapWriter` 或等价 internal seam |
| CLCFG-OQ-005 | ToolSkillPage P0 是否显示可编辑控件？ | 拒绝 | CLCFG-TODO-005、017 | owner surface 未冻结前不得伪装为 ready | P0/P1 隐藏或只读摘要，P2 再评估 |