# DASALL CLI config 交互式部署配置设计

文档版本：v0.1
日期：2026-05-07
状态：Draft

## 1. 目标与结论

本文定义 `dasall-cli config` 命令的产品与工程设计，目标是把“安装成功后的首次部署引导”和“后续修改配置”的入口统一收敛为一个可交互、可自动化、可审计的 CLI 工作流。

本文的核心结论只有四条：

1. `dasall-cli config` 必须是 `apps/cli` 侧的本地 orchestration 命令，而不是新的 daemon RPC。
2. 包安装阶段继续保持非交互；首次部署交互必须发生在安装完成之后，由操作者显式运行 `dasall-cli config` 触发。
3. `config` 只能写入既有 canonical 配置入口和 secret owner 认可的接缝，不能演化为第二配置中心。
4. 首版必须 capability-gated：daemon/profile/service 启停属于 P0；LLM secret onboarding 属于 P1；tools/skills 激活页只在对应 owner surface 冻结后开放，当前不允许伪造“已支持”。

评审后补充两条约束：

5. P0 进入 Build 前必须先冻结安装态 socket canonical path、socket 权限模型、命令安装名与配置来源优先级；这些不是 wizard 实现细节，而是后续任务拆分的前置契约。
6. `config` 首版必须输出结构化 action plan，并支持 dry-run / plan 类只读验证入口；交互式 wizard 只是 action plan 的一种输入方式，不应让服务启停、reload/restart 和文件写入判断散落在页面流程中。

本文是 [DASALL-cli本地控制面详细设计.md](DASALL-cli本地控制面详细设计.md) 与 [DASALL_Ubuntu平台DPKG打包方案设计.md](DASALL_Ubuntu平台DPKG打包方案设计.md) 的补充设计，专门回答“安装后如何用单一命令完成部署初始化和后续修改”。

### 1.1 命名约定

本文沿用源码树中的二进制名 `dasall-cli` 描述开发态命令示例。Ubuntu 正式包若按打包设计安装为 `/usr/bin/dasall`，则对用户公开的稳定命令应映射为：

```text
开发态：dasall-cli config
安装态：dasall config
```

后续 Build 任务必须同时覆盖两层命名：源码 target / 测试可继续使用 `dasall-cli`，包交付文档、manpage、postinst next steps 和 operator workflow 必须以安装态 `dasall config` 作为唯一公开命令名。`dasall-cli` 只保留为源码树 target、开发态示例和测试上下文。本文在未特别说明时使用 `dasall-cli config` 代表该命令族的语义面。

## 2. 问题定义

当前仓库已经冻结了本地控制面的基本部署契约：

1. Ubuntu 包安装必须是非交互式，`postinst` 只输出 next steps，不启动 wizard。
2. daemon 的 canonical 启动输入目前分散在 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json`。
3. CLI 现状只有 `help/version` 两个本地命令，其他命令都通过 UDS 请求 daemon。
4. LLM provider 的真实密钥不能出现在 provider baseline 资产或仓库跟踪文件里，只能通过 `secret://...` / `profile://...` / infra secret 注入。
5. tools/skills 虽然已有运行时与 importer 基线，但“部署期默认激活面”和“本地配置 owner”尚未冻结为 package-ready 的 operator surface。

因此，DASALL 当前缺的不是“再多一个配置文件”，而是一个受控的 operator workflow：

1. 发现当前系统处于 fresh install、partial config、configured-but-stopped 还是 drifted 状态。
2. 以交互式方式收集必要输入，并复用当前值作为默认答案。
3. 把非敏感配置写回 canonical 文件，把敏感值写到 secret owner 认可的后端。
4. 在操作者确认后执行 validate、start、enable 等系统动作。
5. 最后给出配置汇总、服务状态与未完成项，而不是让用户手工拼接多个命令和文件。

## 3. 当前边界与工程事实

### 3.1 已冻结事实

| 事实 | 当前依据 | 对 `config` 的含义 |
|---|---|---|
| 包安装保持非交互 | Ubuntu DPKG 打包设计 7.7.3 / 7.7.4 | `config` 必须是安装后显式运行的命令，不能把交互塞回 `postinst` |
| CLI 是纯客户端壳层 | CLI 本地控制面详细设计 1.1 / 1.2；`apps/cli/src/main.cpp` | `config` 不得把 Runtime/Access 主控逻辑拉回 CLI |
| daemon 启停由 systemd/supervisor 管理 | daemon 部署契约 6.3；DPKG 设计 7.7 | `config` 负责受控调用 service manager，而不是自建守护生命周期 |
| daemon canonical 配置入口是 `/etc/default/dasall-daemon` 和 `/etc/dasall/daemon.json` | DPKG 设计 7.6 / 7.10.9 | `config` 必须写这两个入口，不再发明平行 daemon 配置面 |
| LLM 密钥必须走 `secret://` / `profile://` / infra secret | LLM 详细设计 6.15.2 / 6.15.4 | `config` 不能把 API key 写进 provider asset 或 `daemon.json` |
| `ISecretManager` 当前无 `create/set` 公共入口 | `infra/include/secret/ISecretManager.h` | `config` 的 secret onboarding 需要新增 bootstrap-only 写入接缝，不能假装现成可用 |
| tools/skills 部署激活 owner 未冻结 | tools 详细设计 6.5.4 / 6.12.5 | 首版 `config` 不能把 tools/skills 页面做成必填主路径 |

### 3.2 当前实现差距

当前仓库并不缺“把 `config` 解析成一个命令”的能力，而是缺三类 owner surface：

1. CLI 侧本地 workflow 组件：目前 parser / main / formatter 都按“本地 help/version + daemon RPC”二分，没有 local provisioning flow。
2. secret 初始写入接缝：`ISecretManager` 只暴露 get/materialize/release/rotate/revoke/inspect，当前缺少“把首次收集到的明文 secret 安全导入 backend”的接口。
3. tools/skills 部署配置 owner：当前已有 SkillRegistry / SkillRuntime / importer，但 package-ready 的 operator-facing enable list、deployment overlay 或 plugin bridge 还没形成稳定面。

换言之，`config` 首版不应承诺“把 DASALL 全部可选能力都交互式配置完成”，而应承诺“把当前真正稳定、真正必要、真正有 canonical sink 的部署输入收敛成一个命令”。

### 3.3 P0 Build 前置冻结项

评审结论是：`config` 方向正确，但不能在以下契约未冻结前直接进入实现。P0 Build 前必须先完成下表的冻结，否则 wizard 会被迫在实现中临时决定部署事实。

| 前置项 | 必须冻结的结论 | 若不冻结的风险 | 后续任务落点 |
|---|---|---|---|
| 安装态 socket canonical path | 冻结为 `/run/dasall/daemon.sock` | CLI 默认、daemon 示例、DPKG 配置与 wizard 默认值继续漂移 | package/defaults 任务；daemon/CLI 默认 endpoint 复核 |
| socket 权限模型 | P0 冻结为 `0600 root/sudo-only`，`0660 dasall group` 只作为后续演进项记录 | `OperatorAccessPage` 加组后仍无法访问 daemon socket | platform/UnixIpcProvider、DaemonSocketPolicy、systemd unit、integration tests |
| 命令安装名 | 冻结为开发态 `dasall-cli`、安装态 `/usr/bin/dasall` / `dasall config` | 文档、manpage、postinst 和测试命令不一致 | CLI parser/help、packaging install rule、docs |
| profile 来源 | `/etc/default/dasall-daemon` 是否继续承载 `profile_id`，以及其与 `daemon.json` 的边界 | profile 被误塞进 daemon deployment JSON 或形成双写 | DaemonConfigFileStore、systemd EnvironmentFile |
| daemon 配置 schema | `/etc/dasall/daemon.json` 的 key 集合、版本字段与注释策略 | `config` 写出的文件无法被 `--validate-only` 稳定消费 | DaemonConfigFileStore、ConfigDiffPlanner |
| action plan schema | plan 的 JSON / human 摘要主键、reload/restart/start/enable 判定 | wizard 流程不可审计、不可 dry-run、测试难拆 | ConfigDiffPlanner、ConfigSummaryFormatter |
| 文件写入事务 | 临时文件、fsync、rename、备份、权限保留、失败回滚 | 配置中途损坏，导致 daemon 无法启动 | DaemonConfigFileStore unit/integration |

### 3.4 P0 冻结结论：socket、operator model 与安装命令名

`CLCFG-TODO-002` 现冻结以下 P0 口径，后续 Build 任务只允许消费，不得再把它们改回“二选一待定”：

1. 安装态 daemon canonical socket path 固定为 `/run/dasall/daemon.sock`；`/run/dasall/control.sock` 不作为 v1 package-mode 的默认对外 endpoint。
2. `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md` 中 `DMD-TODO-037` 已证明真实 socket mode 收敛为 `0600`，因此 P0 operator access model 固定为 `root/sudo-only`；wizard、OperatorAccessPage、README.Debian、postinst hint 和 operator docs 不得再把“加入 `dasall` 组即可访问”写成当前主路径。
3. `0660 dasall group` 仅保留为后续演进项记录；若未来重新开放，必须同步更新 `UnixIpcProvider`、`DaemonSocketPolicy`、systemd unit、package 文档、integration tests 与 operator onboarding，然后再单独冻结。
4. 开发态示例与源码 target 继续使用 `dasall-cli config`；正式包安装后的公开命令名固定为 `/usr/bin/dasall`，对外 operator 文档、manpage、README.Debian 与 postinst next steps 统一写作 `dasall config`。

## 4. 行业最佳实践调研与收敛

本设计参考了多类成熟 CLI 与本地部署工具实践：

### 4.1 AWS CLI `aws configure`

行业启发：

1. `configure` 是安装之后单独显式运行的命令，而不是安装器里的问题集。
2. 交互式输入会显示当前值，用户直接回车即可保留原值。
3. 命令同时支持后续修改，而不是只服务于“首次初始化”。
4. profile 是一等概念，配置修改围绕 profile 而不是散落临时 flag。

对 DASALL 的落地含义：

1. `dasall-cli config` 默认必须是“带当前值的 edit flow”。
2. 首装和后续修改使用同一状态机，不单独做两个命令。
3. `profile_id` 必须是 wizard 里的核心选择项，而不是隐藏实现细节。

### 4.2 GitHub CLI `gh auth login`

行业启发：

1. 交互式引导和 headless 模式并存。
2. 敏感凭据不通过普通 flag 传递，而是走浏览器、stdin、credential store 或环境变量。
3. 安全存储优先；只有找不到 credential store 时才回退到较弱方案，并显式提示。

对 DASALL 的落地含义：

1. LLM key、token、ownership HMAC secret 等敏感值不得通过普通 `--api-key` 形式进入命令历史。
2. `config` 必须支持 masked prompt / stdin / import-file 这三种输入源。
3. summary 页只能展示 redacted 状态，不能回显 secret 明文。

### 4.3 Docker post-install steps

行业启发：

1. 安装流程保持非交互，部署选择由安装后单独步骤完成。
2. “是否允许非 root 使用 CLI”通过组权限明确建模，而不是靠放宽 socket 权限。
3. “是否开机自启”是显式 systemd 决策，不与安装成功自动绑定。

对 DASALL 的落地含义：

1. P0 采用 `0600 root/sudo-only`，`config` 不展示把操作者加入 `dasall` 组的主路径，只保留 sudo/root 运维提示。
2. `config` 应明确询问是否 `enable --now`，而不是默认自动启服务。
3. 若未来重新引入 group access，必须把“修改组成员后需重新登录会话”作为显式 operator 操作说明，而不是默认安装语义。

### 4.4 Kubernetes `kubeadm init`

行业启发：

1. bootstrap 类命令先做 preflight，再执行分阶段动作，而不是边问边改系统。
2. 支持 `--dry-run` 和配置文件输入，便于 CI、审计和 runbook 复现。
3. phase / skip-phases 模型证明：复杂部署流程需要结构化 action plan，而不是单纯依赖交互页面顺序。

对 DASALL 的落地含义：

1. `config` 必须先生成 plan，再 apply；交互式 wizard 只负责生成 desired state。
2. P0 应冻结 `config plan` 或 `config --dry-run` 的语义，至少能展示将写哪些文件、是否需要 restart、是否会 start/enable。
3. preflight 必须覆盖权限、systemd 可用性、canonical 文件可写性、daemon binary 可执行性和 `--validate-only` 可用性。

### 4.5 HashiCorp Vault `operator init`

行业启发：

1. bootstrap secret 是受控的一次性初始化面，不应混入普通查询接口。
2. 初始化状态必须可判定；已经初始化、未初始化和错误状态要有不同退出语义。
3. 敏感输出需要显式保护，必要时支持加密或只输出 redacted 状态。

对 DASALL 的落地含义：

1. `SecretBootstrapWriter` 应是 `infra/secret` 持有的 bootstrap-only internal seam，不并入 `ISecretManager` consumer-facing ABI；成功导入后只返回 redacted `auth_ref`，命名固定为 `secret://llm/providers/<provider_ref>`。
2. `LLMSecretPage` 应区分 `missing`、`configured`、`rotated`、`runtime_verified`，不能把 secret 写入成功等同于 provider 可真实调用。
3. P1 前 `config` 可以展示 secret 缺口，但不能宣称 LLM onboarding 完成。

### 4.6 Agent / MCP 本地工具安全实践

行业启发：

1. 本地工具、插件、MCP server 或 skill bundle 可能执行本机命令，必须有显式 consent 与最小权限边界。
2. 不应把 token passthrough、宽泛 scope 或未知来源 local server 配置伪装成“安装默认能力”。
3. 对本地可执行扩展，展示 exact command、来源、权限范围与撤销路径比“默认启用”更重要。

对 DASALL 的落地含义：

1. ToolSkillPage 在 owner surface 未冻结前必须隐藏或只读摘要；P0/P1 不开放编辑。
2. 若系统已检测到 builtin/plugin/skill bundle capability，可展示 summary-only 页面，但只允许显示 bundle/source、tool allowlist、profile constraints、撤销方式和 audit 摘要，不出现 enable/disable 控件。
3. 只有 P2 在 tools owner 冻结 operator-facing deployment config owner 后，才允许开放 editable page；external importer 继续保持 explicit opt-in / feature flag 语义，不作为默认安装能力。
4. `config` 不得绕过 tools 自己的 policy gate、SkillRegistry、PluginExtensionBridge 或未来 deployment overlay owner。

### 4.7 收敛后的通用原则

`dasall-cli config` 必须遵守以下行业共识：

1. 安装非交互，配置交互独立。
2. 当前值可见，回车保留旧值。
3. 默认人类友好，但保留后续 headless / CI 模式。
4. 敏感输入不走普通 flags。
5. 服务启动和开机自启都要求显式确认。
6. 先 plan 后 apply；交互流程不得直接散落执行系统动作。
7. 结果必须可总结、可验证、可回滚，不允许 wizard 完成后仍让用户猜测系统状态。
8. 本地 tools/skills/plugin 扩展必须显式 consent、最小权限、可撤销，不得默认放入 P0 主路径。

## 5. 设计决策

### 5.1 命令定位

新增命令族：

```text
dasall-cli config
dasall-cli config show
dasall-cli config plan [--from-file <path>] [--dry-run]
dasall-cli config validate
dasall-cli config apply --from-file <path> --no-input
```

其中：

1. `dasall-cli config`：默认进入交互式 wizard；首次部署与后续修改都走这一入口。
2. `dasall-cli config show`：只读展示当前 canonical 配置摘要、service 状态与缺失项。
3. `dasall-cli config plan`：只生成 action plan，不写文件、不写 secret、不启动服务；`--dry-run` 是其语义别名或兼容 flag。
4. `dasall-cli config validate`：只做配置校验，不执行 start/enable。
5. `dasall-cli config apply --from-file <path> --no-input`：P0 唯一允许的无交互 mutating 入口；首版可先落最小 headless apply，但 `--from-file` 与 `--no-input` 的语义必须先冻结。

命令语义约束：

1. `config` 无子命令时要求 TTY；非 TTY 默认 fail-closed，并提示使用 `config plan --from-file` 或 `config apply --from-file`。
2. `config show` 必须允许非 root 执行，但只能读取权限允许的摘要；无法读取的项显示 `unavailable`，不能要求提权后才展示全部页面。
3. `config plan` 和 `config validate` 不得修改系统状态。
4. `config show`、`config plan`、`config validate` 无论是否在 TTY 中执行，都不得弹出 prompt、masked input 或确认页。
5. `config apply --from-file` 必须要求 `--no-input` 或等价确认策略，避免 CI 中隐式卡在交互提示。
6. `--help` / `-h` 在顶层和各级子命令都应优先显示帮助文本并返回成功，不得继续执行文件探测、校验或 service 动作。

### 5.1.1 config exit contract

`config` 命令族必须复用 CLI v1 既有 `CliExitDecision` family，不得引入新的对外退出码。对外 exit code 固定继续收敛到 `0/2/3/4/5/6/7`：

| 场景 | exit code | 说明 |
|---|---|---|
| `help`、`show`、`plan`、`validate`、`apply` 成功完成 | `0` | 包括交互式 wizard 成功结束和 `plan --dry-run` 成功输出 |
| 参数/usage/TTY 契约错误 | `2` | 包括未知子命令、非法 flag 组合、非 TTY 调用交互式 `config`、`apply` 缺少 `--from-file` 或 `--no-input` |
| 本地依赖或本地执行入口不可用 | `3` | 包括必须调用的本地 binary / helper 不可执行 |
| 权限不足或 operator access 模型拒绝 | `4` | 包括需要 root/sudo 的 mutating 调用和 P0 `0600 root/sudo-only` 模型下的非特权写路径 |
| 确定性配置失败 | `5` | 包括 `validate-only` 失败、文件事务失败、rollback 失败 |
| 可重试或待外部条件满足的阻断 | `6` | 包括可恢复的 preflight block、短暂 not-ready 或待手工解阻后可重试的 service action 阻塞 |
| config 本地 contract / projection 不变量破坏 | `7` | 包括 parser / coordinator / formatter 之间不应到达的内部契约冲突 |

`config` 本地工作流后续可以引入 `local_argument_error`、`local_permission_required`、`local_dependency_unavailable`、`local_validation_failed`、`local_retryable_block` 等内部 disposition，但只能投影到上述既有 exit family，不能新增第八种对外退出码。

### 5.2 为什么 `config` 不是 daemon RPC

`config` 若做成 daemon 命令，会直接碰到三个边界问题：

1. 首次部署时 daemon 尚未启动，无法要求用户先用 daemon 来配置 daemon 自身。
2. `config` 需要写 `/etc/default`、`/etc/dasall/daemon.json`、group membership、systemd enable 状态，这些都属于主机运维面，而不是 daemon 的请求处理面。
3. 让 daemon 承担安装后 bootstrap、secret 接收与 systemd 自启控制，会把 apps/daemon 从“本地 Access owner”扩张为“系统部署 owner”，与当前架构边界冲突。

因此，`config` 必须是：

1. CLI 侧本地命令。
2. 可在 daemon 未启动时工作。
3. 只在需要时调用 daemon 或 systemd 做校验与状态探测。

### 5.3 `config` 的职责与非职责

职责：

1. 探测当前部署状态。
2. 收集 operator 输入并应用到 canonical sink。
3. 调用 `--validate-only`、`ping`、`readiness` 和 systemd 状态命令做验证。
4. 在完成后输出可读 summary。

非职责：

1. 不直接装配 Runtime。
2. 不重写 Provider baseline / Prompt baseline / Profile baseline 资产。
3. 不把 tools/skills 的 module-local 运行时对象升格为 CLI 自己的配置真相源。
4. 不把 secret 管理 owner 从 infra/secret 抢到 apps/cli。

### 5.4 配置来源优先级

P0 必须冻结安装态下的配置来源优先级。建议采用以下顺序：

1. 二进制内置 defaults：仅作为兜底。
2. profile baseline：由 `/usr/share/dasall/profiles/` 提供运行策略与 profile 投影。
3. deployment override：`/etc/dasall/daemon.json`，只承载 `daemon.*` 部署覆盖。
4. entry selection：`/etc/default/dasall-daemon`，承载当前代码仍需经命令行传入的 `profile_id` / install-mode asset root 等入口参数。
5. explicit command override：`--config-file`、`--profile-id`、`--socket-path` 等只用于调试、validate 或显式 override；与配置文件同键冲突时 fail-closed。

`config` 写入时必须遵守：

1. `profile_id` 写 `/etc/default/dasall-daemon`，不写入 `/etc/dasall/daemon.json`，除非 daemon loader 后续正式支持该字段。
2. `daemon.*` 写 `/etc/dasall/daemon.json`。
3. secret material 写 secret backend。
4. service enable/start 写 systemd state。
5. operator access 写 OS group membership，前提是 socket 权限模型已经支持 group access。

### 5.5 Build-ready 判定

本文进入 Build-ready 的最低条件不是“页面流程写清楚”，而是以下契约均已可测试：

1. `config show` 能在 fresh install / configured / drifted 三类状态输出稳定摘要。
2. `config plan` 能输出结构化 action plan，并且不产生任何副作用。
3. `config validate` 能复用 daemon `--validate-only`，并把失败原因映射到稳定错误域。
4. `config` wizard 生成的 desired state 与 `config apply --from-file` 使用同一 plan/apply 内核。
5. socket path 与 socket permission model 已冻结，OperatorAccessPage 不再依赖未决假设。

## 6. 用户体验与工作流

### 6.1 状态探测而非“首次部署标记文件”

首装与否不应由单一 marker file 判定，而应从 canonical sources 推导：

1. `/etc/default/dasall-daemon` 是否存在且可解析出有效 `profile_id`。
2. `/etc/dasall/daemon.json` 是否存在且通过 `dasall-daemon --validate-only`。
3. selected profile 所需的 secret refs 是否已 provisioned。
4. `dasall-daemon.service` 是否已安装、运行、enabled。
5. 启动后 `ping` / `readiness` 是否成功。

`InstallState` v1 必须是闭集，只允许以下六种状态；实现期不得再引入 `PartialConfig`、`Unknown`、`NeedsGroupRelogin` 等并行命名。判定时先识别 `Unsupported` 与 `FreshInstall`，其余可部署场景只允许落到 `BootstrapPending`、`ConfiguredStopped`、`ConfiguredRunning` 或 `Drifted`。

建议将整体状态投影为：

| 状态 | 判定条件 | `config` 默认行为 |
|---|---|---|
| `FreshInstall` | canonical 文件缺失或均未初始化 | 全量 wizard |
| `BootstrapPending` | 文件存在但 validate / secret / service 仍缺一项 | 从缺口页继续 |
| `ConfiguredStopped` | 配置有效但服务未运行 | 默认引导到 start / enable 页面 |
| `ConfiguredRunning` | 配置有效且服务运行 | 默认进入 edit flow，值全部预填 |
| `Drifted` | 文件可读但 validate 失败、service 与配置不一致或 secret 缺失 | 先展示 drift 摘要，再进入修复模式 |
| `Unsupported` | 缺少 systemd 或 install payload 不完整 | 仅允许 show / file edit / validate，不自动做服务动作 |

### 6.2 交互页顺序

首版建议的 wizard 页顺序如下：

1. `WelcomeAndStatePage`
2. `ProfileSelectionPage`
3. `DaemonConfigPage`
4. `LLMSecretPage`（仅 capability 可用时展示）
5. `ToolSkillPage`（仅 capability 可用时展示，当前默认隐藏或只读）
6. `OperatorAccessPage`
7. `ReviewAndApplyPage`
8. `ServiceActionPage`
9. `SummaryPage`

各页的设计要点：

#### 6.2.1 WelcomeAndStatePage

显示：

1. 当前状态投影。
2. 缺失项摘要。
3. 本次是否处于首次部署、修复配置还是修改现有配置。

#### 6.2.2 ProfileSelectionPage

必须收集：

1. `profile_id`

建议行为：

1. 列出当前已安装 profile，例如 `desktop_full`、`edge_balanced`、`factory_test`。
2. 当前值作为默认值；fresh install 时默认 `desktop_full`。
3. 明确标注“选择 profile 不等于直接修改 baseline 资产”。

持久化位置：

1. `/etc/default/dasall-daemon`

#### 6.2.3 DaemonConfigPage

首版最小必填集：

1. `socket_path`
2. `log_format`
3. `diag_enabled`
4. `override_enabled`
5. `watchdog_enabled`

首版可选高级项：

1. `listen_backlog`
2. `max_payload_bytes`
3. `dispatch_timeout_ms`
4. `shutdown_grace_ms`
5. `receipt_ttl_sec`
6. `accept_workers`
7. `dispatch_workers`
8. `log_level`

设计要求：

1. 所有字段必须以当前值预填。
2. 如果检测到当前代码 / 安装文档在 socket canonical path 上存在漂移，不要发明第三个默认值，而是展示“当前检测值 + 推荐值 + 漂移警告”。
3. 若修改项超出 `SIGHUP` allowlist，wizard 必须在 review 阶段标记“需要 restart”。

持久化位置：

1. `/etc/dasall/daemon.json`

#### 6.2.4 LLMSecretPage

本页不是“编辑 provider 资产”，而是“为选中的 baseline provider refs provision secret”。

首版建议只覆盖这类输入：

1. 选中 profile 所需 provider 的 API key / bearer token
2. 可选的 provider auth profile 名称

本页必须遵守：

1. 不把明文 secret 写入 `/etc/dasall/daemon.json`。
2. 不修改 `/usr/share/dasall/llm/providers/...` baseline 资产。
3. summary 只显示 `configured / missing / rotated` 状态，不显示 secret 值。

当前工程现实：

1. `ISecretManager` 还没有公开的初始 `create/set` 入口。
2. 因此首版要么新增 bootstrap-only secret import seam，要么把本页降级为“收集但不落盘”。
3. 从工程可交付性出发，必须选择前者，但该 seam 不能破坏现有 `ISecretManager` consumer-facing ABI。

推荐接缝：

1. 新增 infra/secret 内部 bootstrap writer，例如 `SecretBootstrapWriter` 或 `ISecretProvisioningTransaction`。
2. 仅供 operator bootstrap/import 场景使用，不并入 `ISecretManager` 当前公共查询接口。
3. 首版 file backend 建议把 install-mode root 固定到 `/var/lib/dasall/secrets`，与 `StateDirectory=dasall` 对齐。
4. `LLMSecretPage` 只负责收集 provider ref、masked secret input、可选 auth profile 名称，并把 `SecureBuffer` 交给该 internal seam；不得自己拼接或写入 secret 文件。
5. 导入成功时，bootstrap seam 至少返回 `SecretProvisioningResult{auth_ref, backend_root, provisioning_state}`，其中 `auth_ref` 命名固定为 `secret://llm/providers/<provider_ref>`。
6. 如果导入流程在提交前失败，不得留下半成品 `auth_ref`；若已创建 candidate record，必须由 bootstrap seam 显式 revoke/remove，再把页面状态保持为 `missing`。

#### 6.2.5 ToolSkillPage

本页必须 capability-gated，并显式收敛为 `ToolSkillPageMode = hidden | summary_only | editable`。

当前冻结结论：

1. P0：如果 tools/skills 只具备 internal runtime，或虽有 active capability 但 operator-facing deployment config owner 尚未冻结，则本页默认 `hidden`；仅在系统已经检测到 active bundle/plugin/skill capability 时，才允许降级为 `summary_only`。
2. P1：LLM secret onboarding 不扩张 tools surface；本页继续维持 `hidden` 或 `summary_only`，不得出现 bundle/source/importer 开关，也不得暗示“默认安装的 skills/tools 可任意开关”。
3. P2：只有在 tools owner 冻结了 operator-facing deployment config surface、plugin extension bridge 或等价扩展面可稳定探测，并且 `bundle/source/allowlist/profile constraints/revoke path/audit result` 都可被投影到 action plan / summary 时，本页才允许切到 `editable`。
4. `config` 只读取 `ToolConfigAdapter`、`ToolRegistry`、`SkillRegistry`、`SkillRuntime` 的投影视图，不直接写 active plugin set，不绕过 policy gate，也不直接驱动 importer 默认开启。

#### 6.2.6 OperatorAccessPage

P0 本页不负责 user/group 变更；它只负责解释当前 operator access model，并把 socket 可见性、sudo 建议和后续动作说清楚。

P0 页面职责：

1. 展示 canonical socket path `/run/dasall/daemon.sock`、当前探测到的 socket mode/owner，以及当前用户是否可访问。
2. 如果当前调用缺少访问权限，明确提示本版采用 `0600 root/sudo-only`，并建议使用 `sudo dasall config ...` 或在 root shell 下重试。
3. 允许输出“未来版本可评估 group access”，但不得把 `dasall` 组加入流程、`usermod` 或重新登录提示当作当前主路径。

安全要求：

1. 不通过放宽 socket 为 `0666` 解决权限问题。
2. 不执行 `groupadd`、`usermod`、`gpasswd` 等系统变更。
3. postinst、README.Debian、wizard summary 与本页提示必须共享同一 `0600 root/sudo-only` 口径。
4. 若未来恢复 `0660 dasall group` 模型，必须新开冻结任务并同步 `UnixIpcProvider`、`DaemonSocketPolicy`、systemd unit、package 文档与 integration tests，然后本页才可重新变为可变更页面。

#### 6.2.7 ReviewAndApplyPage

必须展示：

1. 配置 diff 摘要。
2. 哪些更改只需写文件。
3. 哪些更改需要 `reload`。
4. 哪些更改需要 `restart`。
5. 哪些能力因 owner 未就绪而仍处于 pending。

Review 阶段不得直接执行动作。它只能展示 `ConfigDiffPlanner` 生成的 action plan，并要求操作者确认。确认后进入 apply 阶段；dry-run / plan 模式停留在本页等价输出。

#### 6.2.8 ServiceActionPage

必须询问：

1. 是否立即执行 validate。
2. 是否立即启动 daemon。
3. 是否设置开机自启。
4. 若当前服务已运行且有 restart-required 变更，是否立即重启。

本页的动作优先级：

1. `validate-only`
2. `reload`（仅当变更集全在 allowlist 中）
3. `restart`
4. `enable --now` 或 `start`

#### 6.2.9 SummaryPage

必须汇总：

1. 选中的 profile。
2. daemon 核心配置摘要。
3. secret refs 当前状态。
4. daemon service 状态：installed / running / enabled。
5. `ping` / `readiness` 校验结果。
6. operator access 提示。
7. 未完成项与下一步建议。

## 7. 配置数据与 canonical sink 设计

### 7.1 首版必须覆盖的数据

| 类别 | 字段 | 是否首装必填 | canonical sink |
|---|---|---|---|
| Profile | `profile_id` | 是 | `/etc/default/dasall-daemon` |
| Daemon | `socket_path` | 是 | `/etc/dasall/daemon.json` |
| Daemon | `log_format` | 否，给默认值 | `/etc/dasall/daemon.json` |
| Daemon | `diag_enabled` / `override_enabled` / `watchdog_enabled` | 否，给默认值 | `/etc/dasall/daemon.json` |
| Daemon | timeout / worker / backlog 等 | 否，高级页 | `/etc/dasall/daemon.json` |
| LLM | baseline provider 所需 secret value | 条件必填 | secret backend |
| Service | start now | 是，必须明确回答 | systemd state |
| Service | enable on boot | 是，必须明确回答 | systemd state |
| Operator access | add user to `dasall` group | 否 | OS group membership |

### 7.2 当前不应纳入首版必填的数据

以下内容不应在首版 `config` 中作为必填或默认开放项：

1. Provider baseline/package 本身的编辑。
2. 自定义 provider 实例与 endpoint overlay。
3. tools/skills/plugin 扩展的 enable list。
4. access 远程入口认证材料、TLS/mTLS、gateway/http listener 配置。

原因不是这些内容“不重要”，而是它们当前没有统一的 package-ready operator surface；如果贸然纳入 `config`，只会制造新的平行配置中心。

### 7.3 `config` 不得新建的平行配置面

`config` 明确不得创建以下反模式：

1. `~/.dasall/config.yaml` 这种平行主配置文件。
2. 把 profile、daemon、llm、tools 全部折叠到一个新的 CLI 私有配置源。
3. 直接修改 `/usr/share/dasall/` 下的 baseline 资产。
4. 把 secret 明文回写到 provider catalog、prompt asset 或日志中。

### 7.4 文件写入、备份与回滚

`DaemonConfigFileStore` 必须通过 `ConfigFileWriteTransaction` 事务化写 canonical 文件，不能直接覆盖目标文件。P0 固定采用“同目录临时文件 + `fsync(2)` + `rename(2)` + validate-only + 回滚”的闭环序列；其中 `rename(2)` 提供同文件系统内的原子替换，而 `fsync(2)` 明确要求目录项持久化需额外对父目录执行同步。

`ConfigFileWriteTransaction` v1 规则冻结为：

1. 事务单元是一次 apply 批次；对 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 的所有目标改动统一编排，不能一个文件成功、另一个文件失败后留半提交状态。
2. 读取当前文件时保留原始权限、owner/group、换行风格和未知字段，并为每个被触及的 canonical 文件保留有限数量的 `.last-known-good` 快照；备份不得包含 secret 明文。
3. 写入前先在内存中生成 desired model，并完成本地 schema 校验；校验未通过时不得创建任何临时文件。
4. 对每个目标文件都在同目录写临时文件，设置目标权限/owner/group，写完后先 `fsync` 临时文件。
5. `fsync` 父目录后，再通过 atomic rename 替换目标文件；rename 只能发生在同目录、同文件系统内。
6. rename 完成后必须再次 `fsync` 父目录，确保目录项更新具备崩溃后可见性。
7. 全部文件替换成功后，统一执行一次 `dasall-daemon --validate-only --profile-id <id> --config-file /etc/dasall/daemon.json`。
8. 任一步骤的写入、`fsync`、rename 或 validate 失败，都必须用相同的“同目录临时文件 + `fsync` + rename + 父目录 `fsync`”序列把所有已触及文件回滚到上一个可用版本，并在 summary / action result 中标记 `apply_failed_rolled_back`。

`/etc/default/dasall-daemon` 的写入同样必须走事务化流程；它虽然是简单 key-value 文件，但承载 daemon 启动入口，不能用 ad hoc 字符串拼接覆盖。

### 7.5 Desired State 文件格式

`config apply --from-file` 的输入不应直接等同于 `/etc/dasall/daemon.json`。建议定义单独的 operator desired state 文件，最小结构如下：

```yaml
schema_version: dasall.config.apply.v1
profile_id: desktop_full
daemon:
  socket_path: /run/dasall/daemon.sock
  log_format: json
  diag_enabled: false
service:
  start_now: true
  enable_on_boot: false
operator_access:
  add_users:
    - gangan
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: stdin
```

该文件只表达 desired state 和 secret 输入来源，不直接嵌入 secret 明文。若未来必须支持离线 import-file，文件权限必须被校验为 owner-only，导入后 summary 仍只显示 redacted refs。

## 8. 架构设计

### 8.1 CLI 侧组件建议

| 组件 | 落点建议 | 职责 |
|---|---|---|
| `CliConfigWorkflowCoordinator` | `apps/cli/src/config/` | 统一驱动 state probe、page flow、apply、summary |
| `InstallStateProbe` | `apps/cli/src/config/` | 读取当前文件、service、secret refs、daemon 状态，投影部署状态 |
| `ConfigCapabilityResolver` | `apps/cli/src/config/` | 判断 LLM secret onboarding、tools/skills、service control 是否可用 |
| `InteractivePromptEngine` | `apps/cli/src/config/` | 处理 TTY prompt、masked input、默认值与确认页 |
| `DaemonConfigFileStore` | `apps/cli/src/config/` | 读写 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` |
| `SecretBootstrapWriter` | `infra/src/secret/` | 把初始 secret 以 secure buffer 方式导入 file backend，返回 `SecretProvisioningResult(auth_ref, backend_root, provisioning_state)`，且不暴露为普通 CLI flag |
| `ServiceManagerAdapter` | `apps/cli/src/config/` | 封装 `systemctl` 的 `status/start/restart/reload/enable` 行为 |
| `ConfigDiffPlanner` | `apps/cli/src/config/` | 比较 current vs desired，输出 write/reload/restart/start/enable action plan |
| `ConfigSummaryFormatter` | `apps/cli/src/config/` | 输出人类可读和 JSON summary |
| `ConfigPlanFormatter` | `apps/cli/src/config/` | 输出 dry-run / plan 的 stable human 与 JSON 视图 |
| `ConfigPreflightChecker` | `apps/cli/src/config/` | 检查权限、systemd、daemon binary、canonical path、socket policy 与文件可写性 |

### 8.2 `config` 与现有 CLI 主链的关系

建议把 CLI 命令分成两类：

1. `daemon request commands`：`ping`、`readiness`、`run`、`status`、`cancel`、`diag`
2. `local workflow commands`：`help`、`version`、`config`

其中：

1. `config` 不进入 `CliRequestBuilder`。
2. `config` 可以在内部复用 `CliIpcClient` 做 post-start `ping/readiness` 验证。
3. `config` 的 parser / usage / output contract 单独冻结，不混入 daemon request schema。

### 8.3 service manager 交互规则

`config` 应优先通过 `systemd` 工作；若探测到非 systemd 环境，则降级为：

1. 继续写 canonical 文件。
2. 继续执行 `dasall-daemon --validate-only`。
3. 输出手工启动命令。
4. 明确标注“自动 enable/start 不可用”。

### 8.4 secret bootstrap 规则

`config` 的 secret bootstrap 必须遵守：

1. 明文只在进程内短时间存在，使用 `SecureBuffer` 或等价对象持有。
2. masked prompt 禁止回显。
3. summary、日志、审计只展示 redacted ref，例如 `secret://llm/providers/deepseek-prod (configured)`。
4. `auth_ref` 命名固定为 `secret://llm/providers/<provider_ref>`；provider baseline 资产和 `daemon.json` 只允许保存该 redacted ref，不允许保存 secret 明文或第二套本地路径。
5. install-mode file backend root 固定映射到 `/var/lib/dasall/secrets`；build-tree / dev-mode 如需不同目录，只能通过 `infra.secret.file.root_dir` 的部署层投影表达，不能发明第二个安装态默认值。
6. 初始导入成功后，后续消费仍通过 `ISecretManager`、`SecretManagerFacade`、`FileSecretBackend` 标准读路径完成；导入失败不得留下半成品 `auth_ref` 或 candidate record。

### 8.5 socket policy 适配规则

`config` 不能自行改变 socket 安全策略，只能读取并呈现已冻结的部署策略。实现时建议增加 `SocketPolicyProbe` 或并入 `InstallStateProbe`，负责输出：

1. 当前 daemon 默认 socket path。
2. package 推荐 socket path。
3. 当前 filesystem socket mode / owner / group。
4. systemd unit 中的 `User=`、`Group=`、`RuntimeDirectory=`、`UMask=`。
5. 当前用户是否具备访问 daemon socket 的能力。

如果探测到“文档承诺 group access，但实际 socket mode 为 `0600`”这类冲突，状态必须投影为 `Drifted` 或 `Unsupported`，并阻止 OperatorAccessPage 执行加组动作。

## 9. 状态机与 action plan

### 9.1 状态机

```text
FreshInstall
  -> BootstrapPending
  -> ConfiguredStopped
  -> ConfiguredRunning

ConfiguredRunning
  -> Drifted
  -> ConfiguredRunning

Drifted
  -> BootstrapPending
  -> ConfiguredStopped
  -> ConfiguredRunning
```

### 9.2 变更到动作的映射

| 变更类型 | 动作 |
|---|---|
| 仅 `daemon.diag_enabled` 变化，且 daemon 正在运行 | 可尝试 `systemctl reload` |
| `socket_path`、`workers`、`timeouts`、`log_format`、`profile_id` 变化 | 需要 restart |
| 仅写入首次配置，服务未启动 | validate -> start / enable |
| secret 变化，但当前 runtime 尚未消费该链路 | 只标记为 configured，summary 提示“runtime verification pending” |
| operator access 与当前 `0600 root/sudo-only` 模型不匹配 | P0 不写系统组；只在 `manual_followups` 或 `blocked_actions` 中提示使用 sudo/root |

### 9.3 Diff 规划原则

`ConfigDiffPlanner` 必须输出结构化 action plan，而不是流程里临时判断。建议 plan 至少包含：

1. `schema_version`
2. `state_before`
3. `state_after_expected`
4. `file_writes`
5. `secret_writes`
6. `service_validate_requested`
7. `service_reload_required`
8. `service_restart_required`
9. `service_start_requested`
10. `service_enable_requested`
11. `manual_followups`
12. `blocked_actions`

其中：

1. `state_before` / `state_after_expected` 只能使用 `InstallState` 闭集里的六个枚举值。
2. `manual_followups` 与 `blocked_actions` 在 v1 固定为 string 数组。
3. `service_actions` 与 `operator_access_actions` 不再作为 v1 schema key，避免与顶层 `service_*` 双轨并存。

建议冻结的 plan JSON 形态：

```json
{
  "schema_version": "dasall.config.plan.v1",
  "state_before": "BootstrapPending",
  "state_after_expected": "ConfiguredRunning",
  "file_writes": [
    {
      "path": "/etc/dasall/daemon.json",
      "operation": "update",
      "requires_root": true,
      "restart_required": true,
      "changed_keys": ["daemon.socket_path", "daemon.log_format"]
    }
  ],
  "secret_writes": [
    {
      "ref": "secret://llm/providers/deepseek-prod",
      "operation": "create_or_rotate",
      "runtime_verification": "pending"
    }
  ],
  "service_validate_requested": true,
  "service_reload_required": false,
  "service_restart_required": true,
  "service_start_requested": true,
  "service_enable_requested": false,
  "manual_followups": [],
  "blocked_actions": []
}
```

plan 生成必须是纯计算过程，不读取 secret 明文、不写文件、不调用 `systemctl start/restart/enable`。

## 10. 安全与审计要求

### 10.1 敏感输入规则

1. 不接受 `dasall-cli config --api-key xxx` 这类普通 flag。
2. 支持 masked prompt。
3. 支持 `stdin` / import file 供 headless 模式使用。
4. `--no-input` 与交互式 `config` 冲突时必须 fail-closed，除非同时提供 `--from-file`。

### 10.2 权限规则

1. 修改 `/etc`、写 secret backend、变更 group、控制 systemd 都需要 root 或等价权限。
2. 非特权用户调用 `config` 时，应允许 `show`，但对写操作明确返回“需以 root/sudo 运行”。
3. 不允许通过降低 socket 权限绕过 operator access 模型。
4. socket 权限模型必须由 daemon/platform/package 共同冻结，`config` 不得在运行时临时 `chmod` socket。
5. 如果当前部署为 `0600` socket，非 root `config show` 可以报告 daemon socket 不可访问，但不能建议加入 `dasall` 组作为有效修复。
6. 如果当前部署为 `0660` socket，`config` 只能把用户加入 `dasall` 组，不能把 socket 改成 `0666` 或把 daemon 用户加入操作者私有组。

### 10.3 审计规则

`config` 完成后应至少记录：

1. 谁执行了配置流程。
2. 哪些 canonical 文件被修改。
3. 哪些 secret refs 被新增或轮换。
4. daemon 是否成功 validate / start / enable。
5. 最终处于何种部署状态。

## 11. 与当前工程现状的贴合度评估

### 11.1 已经具备的落点

| 能力 | 当前状态 | 说明 |
|---|---|---|
| CLI 命令解析与本地命令分流 | 基本具备 | 已有 `help/version` 本地路径，可扩展出 `config` |
| daemon canonical 配置文件 | 已冻结 | `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 已存在明确契约 |
| service enable/start 语义 | 已冻结 | 首装不自动启；operator 显式选择 `enable --now` |
| validate-only 校验路径 | 已具备 | `dasall-daemon --validate-only` 已是正式部署契约的一部分 |
| SecretManager 读路径 | 已具备基线 | `ISecretManager`、`SecretManagerFacade`、`FileSecretBackend` 已存在 |

### 11.2 已冻结前置项与当前 blocker

| blocker | 影响 | 设计应对 |
|---|---|---|
| socket path / socket permission 模型已由 CLCFG-TODO-002 冻结 | 下游 Build 若重新打开 `0660 dasall group` 或第二默认 path，会重新制造 package/CLI/daemon 口径分叉 | 后续实现只允许消费 `/run/dasall/daemon.sock` 与 `0600 root/sudo-only`；如需 group access，必须新开冻结任务 |
| 开发态命令名与安装态命令名映射已由 CLCFG-TODO-002 冻结 | 若 README/manpage/postinst/operator docs 继续混用 `dasall-cli`，安装态命令面会再次分叉 | 开发态保留 `dasall-cli config`；安装态文档/manpage/postinst/operator workflow 统一 `dasall config` |
| bootstrap-only secret import seam 已由 CLCFG-TODO-004 冻结 | P1 Build 不再依赖口头假设“未来会有 create/set API”，但 provider runtime verification 仍是后续实现项 | internal `SecretBootstrapWriter` 固定归属 `infra/secret`；`auth_ref` 统一为 `secret://llm/providers/<provider_ref>`；install-mode root 固定 `/var/lib/dasall/secrets`；读路径继续复用 `ISecretManager` / `SecretManagerFacade` / `FileSecretBackend` |
| LLM 真实 secret/endpoint/profile 注入链仍有残余 blocker | 即使 secret 写入，也未必能立刻证明 provider 真调用 ready | summary 分离 `configured` 与 `runtime verified` 两层状态 |
| ToolSkillPage capability boundary 已由 CLCFG-TODO-005 冻结 | `config` 不再把 internal runtime / plugin activation 冒充 operator-facing deployment surface；P0/P1 保持 `hidden` 或 `summary_only`，P2 才允许 `editable` | `ToolSkillPageMode` 固定为 `hidden` / `summary_only` / `editable`；external importer 继续默认 behind feature flag；`config` 只读取 bundle/source/allowlist/revoke/audit 投影，不绕过 tools policy gate |
| 安装态 socket canonical path 仍有文档漂移 | wizard 默认值可能与 service / CLI / docs 不一致 | 在实现前先冻结 install canonical socket 路径；实现期用 detected current value 而非发明新默认 |

## 12. 分阶段实施建议

### 12.1 Phase P0：设计冻结与最小本地工作流

范围：

1. 冻结安装态 socket canonical path、socket permission model、命令安装名映射与配置来源优先级。
2. 冻结 `config` 命令 grammar，包括 `show`、`plan`、`validate`、交互式 wizard 与 future `apply --from-file`。
3. 冻结 state probe / preflight / diff planner / summary model。
4. 实现 `config show` / `config plan` / `config validate` / 交互式 `config` 的 P0 workflow。
5. 只覆盖 profile、daemon 文件、validate、start、enable；operator group 页面仅在 socket 权限模型支持时开放。

验收重点：

1. fresh install 可在一次 wizard 中完成 profile + daemon file + validate + start + enable。
2. existing install 可预填当前值并修改后完成 restart / reload 规划。
3. final summary 能稳定展示 service 状态与 readiness。
4. `config plan` 在不写文件、不启动服务的前提下输出与实际 apply 一致的 action plan。
5. socket 权限与 operator access 提示不再互相矛盾。

### 12.2 Phase P1：LLM secret onboarding

范围：

1. 新增 bootstrap-only secret import seam。
2. `LLMSecretPage` 具备 masked prompt、stdin/import file 和 redacted summary。
3. baseline provider 的 `auth_ref` 可被 provision 到 file backend。

验收重点：

1. secret 不经普通 flags。
2. secret 不回写 provider asset / daemon.json。
3. summary 只显示 redacted refs。

### 12.3 Phase P2：tools/skills/operator extensions

前提：

1. tools/skills 的 deployment config owner 已冻结。
2. plugin extension bridge 或等价扩展面可被稳定探测。
3. `bundle/source/allowlist/profile constraints/revoke path/audit result` 已能稳定投影到 summary 与 action plan。

范围：

1. `ToolSkillPageMode` 从 `summary_only` 升级为 `editable`。
2. 允许选择 default bundles / allowlist / importer source，但 external importer 仍需显式 feature flag / consent，不作为默认安装能力。
3. 继续遵守 capability-gated，不为未安装能力或未被 profile 允许的能力展示伪选项。
4. revoke / disable 路径必须保持 source-scoped、可审计，且不绕过 `PluginExtensionBridge`、`SkillRegistry` 与 `ToolPolicyGate`。

## 13. 实现映射建议

建议的未来实现落点：

1. `apps/cli/src/CliCommandParser.h`
2. `apps/cli/src/CliCommandParser.cpp`
3. `apps/cli/src/main.cpp`
4. `apps/cli/src/config/CliConfigWorkflowCoordinator.cpp`
5. `apps/cli/src/config/InstallStateProbe.cpp`
6. `apps/cli/src/config/ConfigPreflightChecker.cpp`
7. `apps/cli/src/config/ConfigDiffPlanner.cpp`
8. `apps/cli/src/config/DaemonConfigFileStore.cpp`
9. `apps/cli/src/config/ServiceManagerAdapter.cpp`
10. `apps/cli/src/config/ConfigPlanFormatter.cpp`
11. `apps/cli/src/config/ConfigSummaryFormatter.cpp`
12. `infra/src/secret/SecretBootstrapWriter.cpp` 或等价 bootstrap seam

建议的测试出口：

1. `tests/unit/apps/cli/ConfigCommandParserTest.cpp`
2. `tests/unit/apps/cli/InstallStateProbeTest.cpp`
3. `tests/unit/apps/cli/ConfigPreflightCheckerTest.cpp`
4. `tests/unit/apps/cli/ConfigDiffPlannerTest.cpp`
5. `tests/unit/apps/cli/DaemonConfigFileStoreTest.cpp`
6. `tests/unit/apps/cli/ServiceManagerAdapterTest.cpp`
7. `tests/unit/apps/cli/ConfigPlanFormatterTest.cpp`
8. `tests/unit/apps/cli/ConfigSummaryFormatterTest.cpp`
9. `tests/integration/apps/cli/ConfigFreshInstallWorkflowTest.cpp`
10. `tests/integration/apps/cli/ConfigModifyExistingWorkflowTest.cpp`
11. `tests/integration/apps/cli/ConfigPlanDryRunIntegrationTest.cpp`
12. `tests/integration/infra/secret/SecretBootstrapWriterIntegrationTest.cpp`

## 14. 验收标准

`dasall-cli config` 设计完成并可进入 Build 的验收标准建议如下：

1. 包安装后无需任何额外文档跳转，用户只需运行一次 `dasall config` 即可完成当前稳定范围内的首装引导。
2. `config` 在非首次部署时能读取当前值并安全修改，而不是重置已有配置。
3. 所有非敏感配置都有 canonical sink；所有敏感配置都有 secret sink；没有第三套平行配置源。
4. start / enable / reload / restart 行为都来自结构化 action plan，而不是散落在交互流程里的临时判断。
5. 最终 summary 能明确回答四件事：配了什么、没配什么、服务现在是什么状态、下一步还需要做什么。
6. `config plan` / `--dry-run` 能在不产生副作用的前提下给出与 apply 一致的 action plan。
7. socket canonical path、socket permission model、operator access 提示、systemd unit 与 CLI 默认 endpoint 已统一，不再存在“加组但 socket 仍不可访问”的矛盾。
8. 文档同时说明开发态 `dasall-cli` 与安装态 `dasall` 的命令名映射。

## 15. 最终建议

从当前 DASALL 工程状态看，`config` 的正确方向不是“做一个很炫的安装向导”，而是：

1. 先把已冻结的本地控制面部署输入收口为一个本地 workflow 命令。
2. 再通过 capability-gated 的页面逐步吸纳 LLM secret、tools/skills 等可扩展配置面。
3. 始终守住三条边界：CLI 不接管 Runtime 主控，daemon 不接管安装后系统部署，config 不制造第二配置中心。

如果后续进入实现阶段，建议先以 P0 为最小闭环切入；否则一旦把 LLM secret onboarding、tools/skills activation、service enable/start、operator group、daemon config 改动全部绑成一个大任务，极容易重新掉回“需求很多，但 owner surface 并未冻结”的老问题。

## 16. 后续任务拆分建议

为了便于从本设计进入 TODO 拆分，建议把后续实施拆成以下原子任务组。每个任务都必须包含代码目标、测试目标与验收命令。

### 16.1 P0-A：部署契约冻结

1. 冻结安装态 socket canonical path，并同步 DPKG 设计、daemon deploy README、systemd unit、CLI 默认 endpoint 与 config 文档。
2. 冻结 socket permission model 为 `0600 root/sudo-only`；`0660 dasall group` 只保留为 future note。
3. 冻结命令安装名映射：开发态 `dasall-cli`，安装态 `/usr/bin/dasall`，对外 operator 示例统一 `dasall config`。
4. 冻结 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 的字段归属和优先级。

### 16.2 P0-B：CLI config 命令骨架

1. 扩展 `CliCommandParser` 支持 `config show|plan|validate|apply` grammar。
2. 在 `main.cpp` 增加 local workflow dispatch，不进入 `CliRequestBuilder`。
3. 补 `ConfigCommandParserTest` 与 usage/help contract。

### 16.3 P0-C：state probe 与 preflight

1. 实现 `InstallStateProbe`，从 canonical 文件、service 状态、daemon readiness 和 secret refs 推导部署状态。
2. 实现 `ConfigPreflightChecker`，检查权限、systemd、daemon binary、socket policy、文件可写性。
3. 补 FreshInstall / BootstrapPending / ConfiguredStopped / ConfiguredRunning / Drifted / Unsupported 单测矩阵。

### 16.4 P0-D：plan / diff / formatter

1. 实现 `ConfigDiffPlanner`，输出 `dasall.config.plan.v1`。
2. 实现 `ConfigPlanFormatter` 与 `ConfigSummaryFormatter` 的 human / JSON 输出。
3. 补 dry-run integration，证明 plan 不写文件、不启动服务。

### 16.5 P0-E：daemon config file store 与 service adapter

1. 实现 `DaemonConfigFileStore`，事务化写 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json`。
2. 实现 `ServiceManagerAdapter`，封装 validate、reload、restart、start、enable、status。
3. 补原子写、回滚、validate failure、非 systemd fallback 测试。

### 16.6 P1：secret bootstrap

1. 新增 bootstrap-only secret import seam。
2. 接入 masked prompt / stdin / import-file。
3. 补 secret redaction、权限校验、configured vs runtime_verified 状态测试。

### 16.7 P2：tools/skills operator surface

1. 等 tools/skills deployment owner 冻结后，再开放 ToolSkillPage 可编辑能力。
2. 支持 bundle/source/allowlist/profile constraints 展示与撤销。
3. 补 policy gate、audit、plugin unload/revoke 集成测试。
