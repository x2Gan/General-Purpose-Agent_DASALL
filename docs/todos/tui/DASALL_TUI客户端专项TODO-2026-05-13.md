# DASALL TUI 客户端专项 TODO

最近更新时间：2026-05-22
阶段：Detailed Design -> Special TODO  
适用范围：`apps/tui/`、`tests/unit/tui/`、`tests/integration/tui/`、`tests/fixtures/tui/`、`cmake/`、`apps/CMakeLists.txt`、`debian/`、`scripts/packaging/`、`docs/todos/tui/`  
当前结论：TUI 客户端架构方向成立，但只能分阶段进入执行。本文是后续 TUI 专项的主执行账本；`docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md` 仅作为交互、布局、fake 场景和人工评审输入，不再作为并行推进账本。`apps/tui` no-daemon prototype、TUI module-local DTO、model/reducer、fake data source、composer、selector、terminal probe 可拆到 L2/L3；FTXUI renderer、snapshot 和 full-screen 小样依赖 third-party 与终端样品 gate；daemon projection、session open/close、真实 LLM route preference 的 carrier 已冻结但 route catalog / daemon attach / submit echo 仍待实现，命令迁移与 Debian 安装态切换继续受前置 gate 约束，不能伪装成可直接推进的 Build 任务。

## 1. 文档头

### 1.1 输入依据

| 类别 | 输入 |
|---|---|
| 详细设计 | `docs/architecture/DASALL_TUI客户端设计方案.md` |
| 总体架构 | `docs/architecture/DASALL_Agent_architecture.md` |
| 工程蓝图 | `docs/architecture/DASALL_Engineering_Blueprint.md` |
| ADR | `docs/adr/ADR-005-architecture-review-baseline.md`、`docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md`、`docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md`、`docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md` |
| SSOT | `docs/ssot/CrossModuleDataProjectionMatrix.md`、`docs/ssot/RuntimePolicyConsumerMatrix.md`、`docs/ssot/AccessUnaryProductionPathV1.md`、`docs/ssot/BinaryEntrypointReadinessV1.md`、`docs/ssot/RuntimeAppCompositionV1.md`、`docs/ssot/SystemIntegrationGateMatrix.md` |
| 计划与规范 | `docs/plans/DASALL_工程落地实现步骤指引.md`、`docs/development/DASALL_工程协作与编码规范.md` |
| 现有 TUI 交付输入 | `docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md` 仅作为参考输入；其中 fake-only、deterministic scenario、CJK/IME/resize、snapshot、manual review 安排迁入本文对应任务和 gate |
| 代码现状 | `apps/CMakeLists.txt` 当前未接入 `apps/tui`；`apps/cli/CMakeLists.txt` 中 `dasall-cli` 仍 `OUTPUT_NAME dasall`；`cmake/DASALLThirdParty.cmake` 已有统一 third-party resolver 但未接入 FTXUI；`tests/unit/tui/` 与 `tests/integration/tui/` 当前不存在；LLM internal streaming 已有实现基础但 TUI 端到端 streaming lifecycle 未闭环 |
| 行业参考 | 采用详细设计第 3 章已收敛的 Agent CLI/TUI、FTXUI、prompt_toolkit、Textual、Aider、Claude Code、Gemini CLI 等实践结论；重点吸收 terminal-first、headless/script 分离、成熟 composer/history/search/editor、非交互 JSON/stream-json 路径分离等通用实践 |

### 1.2 编制原则

| ID | 原则 | 落地方式 |
|---|---|---|
| TUI-PP-01 | 不改写已冻结 ADR 与 SSOT | 所有任务只新增 `apps/tui` 壳层和 TUI projection seam，不把 TUI 升级为 runtime、profile、model route 或 recovery owner |
| TUI-PP-02 | 设计证据不足先补设计 | 权限模型与 `/clear` 会话语义已先行冻结；route preference 承载、daemon projection、命令迁移继续作为前置阻塞或评审门禁 |
| TUI-PP-03 | 每项任务有三件套 | 每项 TODO 均提供代码目标、测试目标、验收命令；设计任务用文档目标和 `rg` 验收，不标记为 Build-ready |
| TUI-PP-04 | 原子任务最小化 | 一项任务只围绕一个接口、一个数据结构、一个初始化路径、一个错误路径或一个测试入口展开 |
| TUI-PP-05 | 分层推进 | fake-only prototype 先行；正式 daemon attach、status projection、selector 真链路和命令迁移后置 |
| TUI-PP-06 | 任务颗粒度适配 LLM 执行 | 每个任务限定在 1 个主目标和 1 组 focused tests，避免单次上下文过大导致实现降质 |
| TUI-PP-07 | 主账本唯一 | 本文为状态推进、证据回写和阻塞解锁的唯一主 TODO；小样 TODO 的内容只作为设计输入或迁移来源，不重复维护状态 |

## 2. 子系统目标与范围

### 2.1 需求确认

| 需求项 | 确认结论 | 来源依据 |
|---|---|---|
| 入口定位 | TUI 归属 Product & Access Layer，是人机入口壳层 | TUI 详设 2.2、4.1、8.1；架构 3.4.1 |
| 命令目标态 | 最终安装态 `dasall` 进入 TUI，`dasall-cli` 保留结构化 CLI | TUI 详设 5.1、附件 B |
| 小样策略 | 先交付 `dasall_tui_prototype`，fake-only，不接 daemon，不改变安装态命令 | TUI 详设 8.4、10、附件 A；现有小样 TODO |
| 正式链路 | 正式 TUI 经 daemon/access projection 接入 Runtime 主链 | TUI 详设 4.2、9.5.3、9.5.4、10 |
| 首版流式口径 | LLM internal streaming 已有实现基础，但首版 TUI 不宣称端到端 stream-ready，采用 unary + accepted_async + query/poll | TUI 详设 4.3、7.3 |
| 状态展示 | 只展示受控 projection、summary、reason-code、timestamp、correlation id | TUI 详设 7.1、7.2、7.4 |
| 模型选择 | TUI 只维护 `NextTurnPreference` 草稿，最终由 profiles/llm/ModelRouter 裁定 | TUI 详设 6.1~6.6；ADR-006；RuntimePolicyConsumerMatrix |
| 会话范围 | 首版短生命周期前台 session；不做跨启动恢复和多 session 管理 | TUI 详设 5.2 |
| 输入能力 | composer 支持多行、历史、反向搜索、外部编辑器和 slash command | TUI 详设 5.5、5.6、9.5.5 |
| 降级路径 | 非 TTY、权限不足、daemon 不可用、终端能力不足必须 fail-closed 或降级 | TUI 详设 5.7、9.5.10 |

### 2.2 纳入范围

| 范围 | 说明 |
|---|---|
| `apps/tui` 独立工程 | `TuiApp`、screen model、reducer、data source、terminal probe、FTXUI adapter、views、command parser |
| TUI module-local 对象 | `TuiScreenModel`、`TuiAction`、`NextTurnPreference`、`TuiSessionView`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiComposerState` |
| fake-only 小样 | 在本文内实现 no-daemon prototype；复用小样 TODO 的 deterministic scenarios、布局断点、CJK/IME/resize、snapshot、manual review 要求，但状态只在本文推进 |
| daemon projection 接线 | `ITuiDataSource` seam、`DaemonTuiDataSource`、`TuiIpcController`、submit/poll/status/route/session projection |
| 测试与门禁 | unit、contract-like projection tests、integration、snapshot/golden、manual CJK/IME/resize evidence、command routing gate |
| 命令迁移准备 | 在正式 ready 后释放 `dasall` 命令并同步 `dasall-cli`、Debian、manpage、postinst、autopkgtest、packaging smoke |

### 2.3 不纳入范围

| 非范围 | 原因 |
|---|---|
| Runtime 主控、FSM、RecoveryManager、ContextOrchestrator、ModelRouter 实现 | 这些 owner 已由架构与 ADR 冻结，不属于 TUI 职责 |
| raw Chain-of-Thought、provider-private reasoning_content 展示 | TUI 详设明确禁止展示 |
| 通用 streaming attach/reconnect/replay cursor | 当前端到端 streaming not ready；只能后续单独设计 |
| 多 session 列表、跨启动恢复、长期历史管理 | TUI 首版非目标 |
| TUI 私有对象直接上升 contracts | 只有多个入口共享稳定语义时才评估 admission |
| fake-only 小样阶段命令迁移 | 命令迁移必须后置到样品、权限、projection、selector、packaging gate 全部通过 |

## 3. 输入依据与约束清单

### 3.1 Must / Should / Must-Not 约束

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的影响 |
|---|---|---|---|---|
| TUI-TC001 | TUI 详设 4.1；架构 3.4.1 | Must | TUI 只做入口壳层，不持有 runtime、context、recovery、reflection、model route、tool policy owner 权 | 所有任务限定在 `apps/tui`、projection seam、packaging；禁止下沉改 runtime 主控 |
| TUI-TC002 | TUI 详设 4.2、9.3 | Must | TUI 经 daemon/access + platform IIPC 接主链，不直接调用 runtime implementation | `DaemonTuiDataSource` 必须依赖 `TuiIpcController` / public seam，不能直连 `AgentFacade` |
| TUI-TC003 | TUI 详设 4.3、7.3 | Must | LLM internal streaming 不等于端到端 TUI streaming ready，首版不宣称 stream-ready | 所有实时任务采用 unary/accepted_async/query/poll；bounded event feed 单独延后，不复用 supporting shape 冒充 public streaming |
| TUI-TC004 | TUI 详设 4.4、5.1、附件 B | Must | 当前 `dasall` 命令仍由 CLI `OUTPUT_NAME dasall` 占用，迁移后置 | 命令释放任务全部依赖 TUI ready 与 packaging matrix |
| TUI-TC005 | TUI 详设 5.5、9.5.5 | Must | composer 状态和键盘行为是核心交互能力 | composer 必须独立状态机、独立测试、人工 IME 门禁 |
| TUI-TC006 | TUI 详设 6.1~6.6 | Must | selector 只表达 next-turn preference，不拥有最终 route | `NextTurnPreference` 局部对象先行；真实 carrier 已由 TUI-TODO-004 冻结为 access/runtime typed sidecar + llm-local route input |
| TUI-TC007 | TUI 详设 7.1~7.4；CrossModuleDataProjectionMatrix | Must | TUI 只消费受控 projection，不消费内部对象 dump | `TuiStatusProjection`、`TuiToolSummaryView`、`TuiModelRouteProjection` 必须单列 |
| TUI-TC008 | TUI 详设 8.4、9.5.11 | Must | FTXUI 只作为 `apps/tui` private dependency，不能泄漏到 model/reducer/access/runtime/contracts | 需要 no-leak boundary test 与 snapshot harness |
| TUI-TC009 | TUI 详设 5.7、9.5.10 | Must | 非 TTY、尺寸不足、permission denied、daemon unavailable 必须可判定降级 | terminal probe、startup test、permission denied failure path 必须单列 |
| TUI-TC010 | ADR-006 | Must-Not | TUI/LLM 不得侵入 ContextOrchestrator 语义上下文 owner | selector 与 status 只展示 route/status 投影，不组装上下文 |
| TUI-TC011 | ADR-007 | Must-Not | TUI 不得裁定 recovery，只展示 recovery summary | `TuiStatusPanel` 只消费 `recovery_summary`，不实现恢复逻辑 |
| TUI-TC012 | ADR-008 | Must-Not | TUI 不得形成第二个 AgentOrchestrator 或最终响应 owner | TUI submit 只提交入口意图，最终结果来自 access/runtime 投影 |
| TUI-TC013 | RuntimePolicyConsumerMatrix | Must | profile/runtime policy 语义 owner 不在 TUI | TUI 不读取 YAML、不写 profile override；route preference 真链路需 profiles/llm 评审 |
| TUI-TC014 | AccessUnaryProductionPathV1 | Must | Access production ingress 证据不能由 mock/ping/liveness 冒充 | daemon attach task 必须依赖 production projection seam，不以 fake source 冒充 |
| TUI-TC015 | BinaryEntrypointReadinessV1 | Must | app binary readiness 必须区分 accepted/degraded/stub/default/bridge/health | TUI startup header 只能显示 projection，不重新定义 readiness |
| TUI-TC016 | SystemIntegrationGateMatrix | Must | subsystem smoke、fixture gate、true integration、release-preflight 分层记录 | 小样 smoke 不得写成正式 release ready；命令迁移需 Gate-TUI-08 |
| TUI-TC017 | 工程规范 3.1~3.7 | Must | C++20、include/src 分离、错误语义明确、新接口配测试 | 每个 public/local interface 均绑定 unit 或 integration 测试 |

### 3.2 当前代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `apps/tui/` | 当前不存在 | 必须先建立独立 target 和目录拓扑 |
| `apps/CMakeLists.txt` | 当前只 `add_subdirectory(cli/runtime_support/daemon/gateway/simulator)` | TUI 需要新增接线；prototype 阶段不得安装 |
| `apps/cli/CMakeLists.txt` | `dasall-cli` 目标仍 `OUTPUT_NAME dasall` | bare `dasall` 命令释放是后置迁移任务 |
| `cmake/DASALLThirdParty.cmake` | 已有 `dasall_resolve_dependency()` 与 cpp-httplib 示例 | FTXUI 应走统一 third-party resolver，而不是散落 FetchContent |
| `tests/unit/tui/`、`tests/integration/tui/` | 当前不存在 | TUI unit/integration discoverability 必须前置 |
| `docs/todos/tui/DASALL_TUI小样快速实现专项TODO-2026-05-12.md` | 已有 fake-only 小样 TODO，覆盖 deterministic scenario、CJK/IME/resize、snapshot、样品评审 | 本文迁入其有效安排；小样 TODO 不再作为独立执行账本 |
| LLM streaming | `LLMManager::stream_generate` 已有实现并具备集成测试 | 修正旧详设口径；TUI 仍只能按 unary/polling 首版交付 |
| access streaming shape | `AccessDisposition::StreamAttached`、`stream_requested`、`subscription_ref` 等 supporting fields 存在 | 只能作为未来 streaming 设计输入，不能视为 attach/replay lifecycle 已 ready |
| runtime LLM request | 当前 Runtime 主响应请求仍按 unary 组织，且尚未把 TUI route preference 投影进 `selection_hint` / llm route input | `NextTurnPreference` carrier 设计已由 TUI-TODO-004 冻结；build 落盘后置到 027~029，streaming 仍单独后置 |
| daemon socket policy | 默认 `/run/dasall/daemon.sock` 与 root/sudo-only operator path 对普通用户 TUI 有 permission denied 风险 | TUI-TODO-001 是命令迁移硬前置 |
| Debian/scripts 命令面 | `debian/` 与 `scripts/packaging/` 中仍大量调用 installed `dasall` 结构化控制面 | TUI-TODO-030 必须先产出旧入口 inventory 和迁移矩阵 |
| `docs/ssot/*` | 已冻结 projection、entrypoint readiness、runtime app composition、integration gate 分层 | TUI 正式接线和命令迁移必须回链这些 SSOT |

## 4. 粒度可行性评估

### 4.1 总体粒度结论

| 结论项 | 判断 |
|---|---|
| 是否足以支撑接口级拆分 | 是。`ITuiDataSource`、`TuiApp`、`TuiReducer`、`TuiComposer`、`TuiModelSelector`、`TuiTerminalCapabilityProbe` 等接口和方法语义已在详设 9.5 展开 |
| 是否足以支撑数据结构级拆分 | 是。`NextTurnPreference`、`TuiSessionView`、`TuiMessageView`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiComposerState`、`TuiScreenModel` 字段已给出 |
| 是否足以支撑函数级拆分 | 局部可以。TUI module-local reducer、parser、composer、selector、probe、renderer adapter 可拆到 L3；daemon projection、route true chain 和 command migration 只能 L2/L1/Blocked |
| 当前最细可落地粒度 | no-daemon skeleton、module-local DTO/model/reducer/fake data source/composer/selector/probe：L3；FTXUI renderer、snapshot、daemon data source 与 route projection：L2；权限模型与 `/clear` 会话语义：L1 已冻结；命令迁移：L1/Blocked |
| 是否可直接全量进入执行 | 否。只能分阶段执行，不能把正式 daemon attach、model pin 生效、bare `dasall` 迁移作为无阻塞 Build 任务 |

### 4.2 可落盘对象提取表

| 类别 | 可落盘对象 | 设计锚点 | 建议落位 | 当前状态 |
|---|---|---|---|---|
| 入口与生命周期 | `TuiApp`、`TuiAppOptions`、`run()`、`dispatch_action()`、`tick()`、`shutdown()` | 9.5.1 | `apps/tui/src/app/` | 未落盘 |
| 状态模型 | `TuiAction`、`TuiScreenModel`、`TuiReducer`、`TuiFocusState`、`TuiBanner`、`TuiModalState` | 9.5.2 | `apps/tui/src/model/` | 未落盘 |
| 数据源 seam | `ITuiDataSource`、`FakeTuiDataSource`、`DaemonTuiDataSource` | 9.5.3 | `apps/tui/src/data/` | 未落盘 |
| Projection DTO | `TuiSessionView`、`TuiTurnReceipt`、`TuiEventProjection`、`TuiRouteCatalogView`、`TuiToolSummaryView` | 7.4、9.2、9.6 | `apps/tui/src/data/TuiProjectionTypes.h` | 未落盘 |
| IPC 控制 | `TuiIpcController`、`submit_turn()`、`query_status()`、`query_route_catalog()`、`close_session()` | 9.5.4 | `apps/tui/src/ipc/` | 未落盘，daemon projection seam 未冻结 |
| 输入 composer | `TuiComposer`、`TuiComposerState`、`TuiInputHistory`、`handle_key()`、`open_external_editor()` | 5.5、9.5.5 | `apps/tui/src/view/` 或 `apps/tui/src/input/` | 未落盘 |
| LLM selector | `NextTurnPreference`、`TuiRoutePreferenceMode`、`TuiModelRouteProjection`、`TuiModelSelector` | 6.1~6.6、9.5.6 | `apps/tui/src/view/` + `apps/tui/src/data/` | 未落盘；真实承载位置未冻结 |
| Transcript | `TuiTranscriptView`、`TuiMessageView`、`render_transcript()`、`toggle_collapse()` | 5.3、5.4、9.5.7 | `apps/tui/src/view/` | 未落盘 |
| Status panel | `TuiStatusPanel`、`TuiStatusProjection`、`format_stage_badge()` | 7.1、7.4、9.5.8 | `apps/tui/src/view/` | 未落盘 |
| Slash command | `TuiSlashCommandParser`、`parse()`、`to_action()`、`help_entries()` | 5.6、9.5.9 | `apps/tui/src/command/` | 未落盘；`/clear` 语义已由 TUI-TODO-002 冻结 |
| Terminal probe | `TuiTerminalCapabilityProbe`、`TuiTerminalCapabilities`、`TuiStartupMode` | 5.7、9.5.10 | `apps/tui/src/terminal/` | 未落盘 |
| FTXUI adapter | `FtxuiRendererAdapter`、`TuiDesignTokens`、`TuiLayoutMetrics`、`render_to_screen()` | 8.4、9.5.11 | `apps/tui/src/terminal/`、`apps/tui/src/view/` | 未落盘；FTXUI 未接入；no-daemon skeleton 可先使用 mock renderer |
| CMake/注册点 | `apps/tui/CMakeLists.txt`、`apps/CMakeLists.txt`、`tests/unit/tui/CMakeLists.txt`、`tests/integration/tui/CMakeLists.txt` | 9.1、11、12 | CMake 拓扑 | 未落盘 |
| Packaging | CLI `OUTPUT_NAME`、TUI target install、Debian install/manpage/postinst/autopkgtest | 5.1、附件 B | `apps/cli/`、`apps/tui/`、`debian/`、`scripts/packaging/` | 后置，Blocked |

### 4.3 粒度可行性评估表

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| TUI 产品边界与命令目标态 | 1、2.2、5.1、附件 B | L1 | final command owner、CLI/TUI 分离、迁移前置门禁明确，且 TUI-TODO-001 已冻结 v1 root/sudo-only backend、ordinary-user fail-closed 与 user-level daemon future-only 口径 | bare `dasall` release gate、projection/packaging matrix 尚未通过 | 权限模型已解阻；继续补 gate evidence 后再评审命令迁移 |
| `apps/tui` 工程拓扑 | 9.1、11 | L2 | 建议目录、target 分阶段、CMake 接线明确 | FTXUI 依赖来源未验证，但不阻塞 no-daemon skeleton | 先 no-FTXUI/mock-renderer prototype target，后 FTXUI renderer 与正式 target |
| `TuiScreenModel` / `TuiReducer` | 9.2、9.5.2、11 | L3 | 字段、职责、接口、失败语义、测试出口明确 | 无实质缺口 | 直接拆 L3 实现任务 |
| `NextTurnPreference` | 6.5、6.6、9.2 | L3/L2 | enum 与字段已给出，UI 草稿语义明确；TUI-TODO-004 已冻结 typed carrier、`request_context`/`client_capabilities`/profile override 的拒绝口径，以及 advisory/fail-closed 规则 | route catalog projection、daemon attach 与 submit echo 尚未实现 | UI 草稿可 L3；真实 build 链路继续按 027~029 分阶段落盘 |
| `ITuiDataSource` | 9.2、9.5.3 | L3 | 方法集、fake/daemon 双实现、失败语义明确；`TUI-TODO-003` 已冻结 daemon projection seam 的 DTO/owner/reason code 基线 | runtime session open/close/query 公共 seam 不完整 | 接口与 fake 可直接 Build；daemon source 只受 session seam 与 mapping/controller 落盘节奏约束 |
| `TuiIpcController` | 9.5.4 | L2 | 方法名、五个 operation、reason code 家族与调用方向明确；`TUI-TODO-003` 已冻结 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope` 边界 | 具体 serialization mapping 与 header 草案尚未落盘 | 先落 `TUI-TODO-021` mapping 文档，再实现 controller |
| `TuiComposer` | 5.5、9.5.5 | L3 | 状态、键位、接口、失败语义、测试出口明确 | IME 人工证据需要 spike | 直接拆实现 + manual gate |
| `TuiModelSelector` | 6.1~6.4、9.5.6 | L3/L2 | 三模式、过滤条件、disabled reason、测试出口明确 | 真 provider/model catalog projection 未冻结 | fake selector L3；真实 route catalog L2/Blocked |
| `TuiTranscriptView` | 5.4、7.1、7.2、9.5.7 | L3 | 展示内容、禁止内容、接口、测试出口明确 | snapshot harness 未落盘 | 直接拆 view + snapshot |
| `TuiStatusPanel` | 7.1、7.4、9.5.8 | L3/L2 | status 字段、projection 规则、接口明确；`TUI-TODO-003` 已冻结 status/tool/event 的摘要边界 | runtime/access status projection producer 尚未落盘 | fake/status view L3；真实 projection contract 已可按 `TUI-TODO-025` 接线 |
| `TuiSlashCommandParser` | 5.6、9.5.9 | L3/L2 | 命令集合、解析规则、fail-closed 语义明确，且 `/clear` 已冻结为新的前台 session 语义 | daemon session close/open 细节仍后置到 session lifecycle seam | parser 可直接 L3；`/clear` daemon integration 继续随 026/BLK-TUI-007 后置 |
| `TuiTerminalCapabilityProbe` | 5.7、9.5.10 | L3 | probe 项、startup mode、failure 语义、测试出口明确 | 不同终端人工矩阵需补证据 | 直接拆实现 + manual gate |
| `FtxuiRendererAdapter` | 8.1~8.4、9.5.11 | L2/L3 | adapter 边界、private dependency、snapshot harness 明确 | FTXUI CJK/IME/resize 技术验证未完成 | no-FTXUI skeleton 先行；third-party spike 通过后再做 renderer Build |
| `DaemonTuiDataSource` / session seam | 4.4、5.2、9.5.3、10 Phase 4 | L2/Blocked | open/submit/poll/close 方向明确，且 `TUI-TODO-003` 已冻结 daemon projection seam、DTO 字段边界与 reason code | runtime session open/close/query 公共 seam 不完整 | 保持 Blocked，先落 021/022 mapping + controller，再等 session seam 解阻 |
| 命令与 Debian 迁移 | 5.1、附件 B | L1/Blocked | 当前占用、目标状态、迁移矩阵明确 | TUI ready、权限模型、packaging smoke 未过 | 后置评审门禁，不进入早期 Build |

## 5. Design -> TODO 映射表

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| TUI 是 Product & Access Layer 入口壳层 | 2.2、4.1、9.3 | 门禁 / 边界 | TUI-TODO-001、006、007、010、032 | 先冻结边界，再建立 no-leak/no-daemon 证据 |
| 权限与启动身份已冻结 | 5.7、13 TUI-OQ-004 | 补设计 / 风险 | TUI-TODO-001、024、030 | TUI-TODO-001 已冻结 daemon-backed root/sudo-only、ordinary-user fail-closed 与 user-level daemon future-only；024/030 继续消费该基线 |
| `/clear` 语义已冻结 | 5.2、5.6、13 TUI-OQ-001 | 补设计 / 流程 | TUI-TODO-002、026 | TUI-TODO-002 已冻结“新前台 session、保留 input history、不要求即时 daemon close/open”；026 继续实现真实 lifecycle |
| `apps/tui` 独立工程与 prototype target | 9.1、10 Phase 1、11 TUI-DES-001 | CMake / 骨架 | TUI-TODO-006、007、008、032 | 先 prototype，不安装；正式 target 后置 |
| FTXUI private dependency 与 snapshot harness | 8.4、9.5.11、11 TUI-DES-004 | 依赖 / 测试 | TUI-TODO-005、019、020 | no-FTXUI skeleton 不被阻塞；先 resolver 评审，再 renderer adapter 与 snapshot gate |
| MVU screen model 与 reducer | 3.3、9.5.2、11 TUI-DES-003 | 数据结构 / 流程 | TUI-TODO-009、010 | model 与 reducer 不依赖 FTXUI，适合 L3 |
| Projection DTO 不进入 contracts | 7.4、9.2、9.6 | 数据结构 / 接口 | TUI-TODO-003、008、021、022 | `TUI-TODO-003` 已冻结 seam owner、envelope 与 reason code；008/021/022 继续把 DTO 与 mapping 落盘 |
| `ITuiDataSource` fake/daemon 双实现 | 9.5.3、10 Phase 3/4、11 TUI-DES-011 | 接口 / 适配器 | TUI-TODO-011、012、021、022、023 | fake 直接落地，daemon attach 依赖 projection seam |
| `TuiIpcController` 错误归一化 | 9.5.4 | 适配器 / 错误处理 | TUI-TODO-003、021、022、024 | 003 已冻结 envelope 与 reason code taxonomy；021/022/024 继续落实 mapping 与 startup failure path |
| composer 状态机 | 5.5、9.5.5、11 TUI-DES-005 | 流程 / 测试 | TUI-TODO-014 | 键盘行为和 busy draft 是独立主目标 |
| selector 三模式与 Next preference | 6.1~6.6、9.5.6、11 TUI-DES-006 | 数据结构 / 流程 | TUI-TODO-015、027、028、029 | fake selector 可做；真实 pin/depth 链路必须先评审 |
| transcript 受控摘要展示 | 5.4、7.1、7.2、9.5.7 | View / 安全展示 | TUI-TODO-016、020 | 禁 raw CoT/secret/raw tool output，snapshot 覆盖 CJK |
| status panel 消费 projection | 7.1、7.4、9.5.8 | View / projection | TUI-TODO-017、025 | fake status 可做，真实 runtime/access projection 后置 |
| slash command 最小集合 | 5.6、9.5.9、11 TUI-DES-009 | Parser / failure | TUI-TODO-013、026 | 未知命令 fail-closed；`/clear` 已冻结为 local action + session lifecycle follow-up |
| terminal capability probe | 5.7、9.5.10、11 TUI-DES-010 | 初始化 / 降级 | TUI-TODO-018、024 | startup path 必须覆盖非 TTY、低能力、权限失败 |
| command release / Debian migration | 5.1、附件 B | packaging / 门禁 | TUI-TODO-030、031、032、033、034 | 仅在 TUI ready 与 packaging matrix ready 后推进 |
| 交付证据与 worklog 回写 | 10 Phase 9、SystemIntegrationGateMatrix | 文档 / 门禁 | TUI-TODO-035 | 完成后回写 evidence，不用口头结论代替命令证据 |

## 6. 原子任务清单

### 6.1 补设计 / 评审解阻

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TUI-TODO-001 | Done | 补齐启动身份与权限模型决策 | TUI 详设 5.7、13 TUI-RISK-002/TUI-OQ-004；BinaryEntrypointReadinessV1 | root/sudo-only operator path、普通用户 TUI、permission denied | L1 | 无生产代码；已新增 `docs/todos/tui/deliverables/TUI-TODO-001-启动身份与权限模型决策.md` | `TuiStartupPermissionPolicy` 决策文档 | design consistency | `rg -n "普通用户|root|sudo|permission denied|user-level daemon|operator" docs/todos/tui/deliverables/TUI-TODO-001-启动身份与权限模型决策.md` | 无 | 无 | 已通过既有 installed operator model、permission denied 文案与 user-level daemon future-only 口径完成冻结 | 决策文档 | 2026-05-22 已完成：文档明确 daemon-backed root/sudo-only、ordinary-user fail-closed、禁止 TUI 内提权，并列出对命令迁移的影响 |
| TUI-TODO-002 | Done | 补齐 `/clear` 会话行为决策 | TUI 详设 5.2、5.6、13 TUI-OQ-001 | `/clear` 清空视图或新建当前进程内 session | L1 | 无生产代码；已新增 `docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` | `TuiClearSemantics` 决策文档 | design consistency | `rg -n "/clear|清空当前前台|新建当前进程内 session|session_id|input history" docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` | 无 | 无 | 已由 deliverable 冻结 `/clear` 为新前台 session 语义、保留 input history、即时 daemon close/open 后置 | 决策文档 | 2026-05-22 已完成：文档明确 `/clear` 不是单纯清屏也不是 `/exit` 别名；下一次 submit 绑定新 `session_id`，input history 保留，daemon close/open 继续以后续 session seam 为准 |
| TUI-TODO-003 | Done | 补齐 daemon projection seam 设计 | TUI 详设 7.4、9.5.3、9.5.4、10 Phase 3；CrossModuleDataProjectionMatrix | `TuiSessionView`、`TuiTurnReceipt`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiEventProjection` | L2 | `apps/tui/src/data/TuiProjectionTypes.h`；新增 `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` | `TuiProjectionTypes` 字段表、`TuiIpcRequestEnvelope`、`TuiIpcResponseEnvelope` owner 边界 | `TuiProjectionTypesCompileTest` / design consistency | `rg -n "TuiSessionView|TuiTurnReceipt|TuiStatusProjection|TuiModelRouteProjection|TuiEventProjection|open_session|submit_turn|poll_events|route_catalog|close_session" docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` | 无 | 无 | 已完成：deliverable 已冻结 access/daemon owner、五个 operation、DTO 字段边界、reason code taxonomy 与 fake/daemon 复用边界 | 字段表 + seam 设计 | 2026-05-22 已完成：交付物明确 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`、五个 operation、DTO 家族、稳定 reason code 与 fake/daemon 复用边界；`BLK-TUI-003` 已关闭 |
| TUI-TODO-004 | Done | 补齐 NextTurnPreference 真链路承载决策 | TUI 详设 6.4~6.6；RuntimePolicyConsumerMatrix；ADR-006 | UI draft -> Access typed request-scope carrier -> Runtime -> llm route input -> ModelRouter -> route projection | L2 | 无生产代码；已新增 `docs/todos/tui/deliverables/TUI-TODO-004-next-turn-preference承载决策.md` | `NextTurnPreferenceCarrier` 决策文档 | design consistency | `rg -n "NextTurnPreference|ModelSelectionHint|request context|profile override|ModelRouter|fail-closed" docs/todos/tui/deliverables/TUI-TODO-004-next-turn-preference承载决策.md` | 无 | 无 | 已完成：deliverable 已冻结 access/runtime typed carrier、拒绝 `request_context` / `client_capabilities` / profile override，并固定 `Auto` / `PreferDepth` / `PinModel` 的失败语义 | 决策文档 | 2026-05-22 已完成：文档明确真实 carrier 落在 access/runtime owner 的 request-scope sidecar，再映射到 llm-local route input；`PreferDepth` 保持 advisory，`PinModel` 在 disallowed/unavailable/not-supported 时 fail-closed |
| TUI-TODO-005 | Not Started | 校验 FTXUI third-party 接入策略 | TUI 详设 8.2~8.4；`cmake/DASALLThirdParty.cmake` | FTXUI submodule/local cache/FetchContent fallback、private dependency | L2 | `cmake/DASALLThirdParty.cmake`、`apps/tui/CMakeLists.txt`、`docs/todos/tui/deliverables/TUI-TODO-005-ftxui接入评审.md` | `dasall_resolve_dependency(ftxui)` 接入方案 | `TuiFtxuiDependencyBoundaryTest` | `rg -n "FTXUI|ftxui|private dependency|submodule|local cache|FetchContent" docs/todos/tui/deliverables/TUI-TODO-005-ftxui接入评审.md cmake/DASALLThirdParty.cmake` | 无 | BLK-TUI-005 | third-party 来源、版本/commit、Debian policy 风险和 offline fallback 通过评审 | 依赖评审文档 | 文档明确依赖来源与回退策略，禁止 FTXUI 泄漏到非 `apps/tui` 模块；未通过时只阻塞 renderer/snapshot/full-screen 小样，不阻塞 no-daemon skeleton |
| TUI-TODO-006 | Not Started | 注册 TUI 测试拓扑 | TUI 详设 9.1、12；工程规范 3.7 | `tests/unit/tui`、`tests/integration/tui`、fixtures/golden | L2 | `tests/unit/tui/CMakeLists.txt`、`tests/integration/tui/CMakeLists.txt`、`tests/fixtures/tui/`、`tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt` | `DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS`、`DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS` | `TuiTestTopologyDiscoverability` | `ctest --preset vscode-linux-ninja -N | rg "Tui(ScreenModel|Reducer|Composer|PrototypeSmoke)"` | 无 | 无 | CMake 可发现 focused TUI tests | CMake + fixtures 目录 | `ctest -N` 能发现 TUI 单元/集成测试入口，目标集合接入现有 unit/integration 聚合，且 label 区分 unit/integration/snapshot |

### 6.2 骨架、接口与 fake-only 小样

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TUI-TODO-007 | Not Started | 新增 `apps/tui` no-daemon prototype target | TUI 详设 9.1、10 Phase 1、11 TUI-DES-001；小样 TODO TUI-PROTO-004 | `dasall_tui_prototype` fake-only、不安装、可先 mock renderer | L2 | `apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp`、`apps/CMakeLists.txt` | `main()`、`dasall_tui_prototype` | `TuiPrototypeBuildSmokeTest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype` | TUI-TODO-006 | 无 | 测试拓扑可发现；FTXUI 未解锁时允许最小 mock/no-renderer 启动路径 | CMake target + prototype main | target 可构建，且不新增 install rule、不改变 bare `dasall`、不 link daemon/runtime/provider implementation |
| TUI-TODO-008 | Not Started | 定义 TUI projection DTO | TUI 详设 7.4、9.2、9.6、11 | TUI module-local DTO，不上升 contracts；daemon 字段映射后续可扩展 | L3 | `apps/tui/src/data/TuiProjectionTypes.h` | `TuiSessionView`、`TuiTurnReceipt`、`TuiEventProjection`、`TuiRouteCatalogView`、`TuiToolSummaryView`、`TuiModelRouteProjection` | `TuiProjectionTypesTest` | `ctest --preset vscode-linux-ninja -R "TuiProjectionTypes" --output-on-failure` | TUI-TODO-007 | 无 | 详设字段表足以形成 module-local baseline；daemon mapping 未冻结时只允许标注 provisional 字段 | DTO 头文件 + 单测 | DTO 字段与详设字段表一致，不 include runtime/access/llm/provider 私有头，且字段 owner/forbidden raw fields 可被单测断言 |
| TUI-TODO-009 | Not Started | 定义 screen model 与 action | TUI 详设 9.2、9.5.2、11 TUI-DES-003；小样 TODO TUI-PROTO-005 | MVU model/action | L3 | `apps/tui/src/model/TuiAction.h`、`apps/tui/src/model/TuiScreenModel.h` | `TuiAction`、`TuiScreenModel`、`TuiFocusState`、`TuiBanner`、`TuiModalState` | `TuiScreenModelTest` | `ctest --preset vscode-linux-ninja -R "TuiScreenModel" --output-on-failure` | TUI-TODO-008 | 无 | DTO 可编译 | model 头文件 + 单测 | model 不包含 FTXUI 类型、不做 I/O、不读取系统时间 |
| TUI-TODO-010 | Not Started | 实现 reducer 状态迁移 | TUI 详设 3.3、9.5.2、11 TUI-DES-003 | action -> reducer -> model | L3 | `apps/tui/src/model/TuiReducer.h`、`apps/tui/src/model/TuiReducer.cpp` | `reduce(TuiScreenModel, TuiAction)` | `TuiReducerTransitionTest` | `ctest --preset vscode-linux-ninja -R "TuiReducer" --output-on-failure` | TUI-TODO-009 | 无 | screen model/action 已定义 | reducer + tests | 已覆盖 submit、append event、focus switch、banner、unknown action no-op/fail-closed 路径 |
| TUI-TODO-011 | Not Started | 定义 `ITuiDataSource` 接口 | TUI 详设 9.2、9.5.3、11 TUI-DES-011 | fake/daemon 双实现 seam | L3 | `apps/tui/src/data/ITuiDataSource.h` | `open_session()`、`submit_turn()`、`poll_events()`、`route_catalog()`、`close_session()` | `ITuiDataSourceContractTest` | `ctest --preset vscode-linux-ninja -R "ITuiDataSource" --output-on-failure` | TUI-TODO-008 | 无 | Projection DTO 已定义 | 接口头文件 + contract-like 单测 | 接口只依赖 TUI DTO 和 C++ 标准库，不依赖 FTXUI/daemon/runtime implementation |
| TUI-TODO-012 | Not Started | 实现 fake data source 场景回放 | TUI 详设 9.5.3、附件 A；小样 TODO TUI-PROTO-003/006 | deterministic fake source | L3 | `apps/tui/src/data/FakeTuiDataSource.h`、`apps/tui/src/data/FakeTuiDataSource.cpp`、`apps/tui/src/data/FakeScenarioCatalog.h` | `FakeTuiDataSource`、`FakeScenarioCatalog::load()` | `FakeTuiDataSourceTest`、`TuiFakeScenarioCatalogTest` | `ctest --preset vscode-linux-ninja -R "Tui(FakeDataSource|FakeScenarioCatalog)" --output-on-failure` | TUI-TODO-011 | 无 | fake 场景列表覆盖 golden_ready/planning_tools/needs_confirm/recovering/route_switch/narrow_cjk | fake source + tests | 同一 scenario replay 输出一致，且不访问网络、socket、daemon、runtime、provider |
| TUI-TODO-013 | Not Started | 实现 slash command parser | TUI 详设 5.6、9.5.9、11 TUI-DES-009 | `/help`、`/status`、`/session`、`/clear`、`/editor`、`/exit` | L3/L2 | `apps/tui/src/command/TuiSlashCommandParser.h`、`apps/tui/src/command/TuiSlashCommandParser.cpp` | `parse(std::string_view)`、`to_action()`、`help_entries()` | `TuiSlashCommandParserTest` | `ctest --preset vscode-linux-ninja -R "TuiSlashCommandParser" --output-on-failure` | TUI-TODO-010 | 无 | `/clear` 已由 TUI-TODO-002 冻结为 local action；daemon lifecycle integration 继续后置到 TUI-TODO-026 | parser + tests | 未知命令不提交 daemon，已知命令映射为本地 action 或 projection query action；`/clear` 不再返回 blocked 占位 |
| TUI-TODO-014 | Not Started | 实现 composer 状态机 | TUI 详设 5.5、9.5.5、11 TUI-DES-005；小样 TODO TUI-PROTO-012 | ready/editing/history-recall/reverse-search/external-editor/submitting/pending-interaction | L3 | `apps/tui/src/view/TuiComposer.h`、`apps/tui/src/view/TuiComposer.cpp`、`apps/tui/src/view/TuiInputHistory.h` | `handle_key()`、`set_busy()`、`recall_history()`、`open_external_editor()` | `TuiComposerTest`、`TuiComposerHistoryTest` | `ctest --preset vscode-linux-ninja -R "TuiComposer" --output-on-failure` | TUI-TODO-010、013 | BLK-TUI-006 | IME/CJK 行为需人工 gate，自动测试先覆盖状态机 | composer + tests | Enter/Alt+Enter/Ctrl+J/Up/Down/Ctrl+R/busy draft 行为均可二值断言 |
| TUI-TODO-015 | Not Started | 实现 model selector fake 交互 | TUI 详设 6.1~6.5、9.5.6、11 TUI-DES-006；小样 TODO TUI-PROTO-011 | Auto / Prefer Depth / Pin Model | L3 | `apps/tui/src/view/TuiModelSelector.h`、`apps/tui/src/view/TuiModelSelector.cpp` | `NextTurnPreference`、`open_selector()`、`apply_preference()`、`cancel_preference()`、`render_disabled_reason()` | `TuiModelSelectorTest`、`TuiRouteCatalogFilterTest` | `ctest --preset vscode-linux-ninja -R "Tui(ModelSelector|RouteCatalog)" --output-on-failure` | TUI-TODO-008、010、012 | 无 | 真实 carrier 与 fail-closed 规则已由 TUI-TODO-004 冻结；当前任务仍只在 fake route catalog 中验证 UI 行为 | selector + tests | selector 能生成 next-turn-only preference，并展示 disabled reason；不修改 profile、不承诺真实 pin 已 build 生效 |
| TUI-TODO-016 | Not Started | 实现 transcript view | TUI 详设 5.3、5.4、7.2、9.5.7、11 TUI-DES-007 | 当前前台 session transcript、tool summary、折叠、滚动 | L3 | `apps/tui/src/view/TuiTranscriptView.h`、`apps/tui/src/view/TuiTranscriptView.cpp` | `render_transcript()`、`toggle_collapse()`、`scroll_to_bottom()`、`TuiMessageView` | `TuiTranscriptViewTest` | `ctest --preset vscode-linux-ninja -R "TuiTranscriptView" --output-on-failure` | TUI-TODO-009、012 | 无 | model/fake source 已可提供消息 | transcript view + tests | raw CoT、provider-private reasoning、secret、raw tool output 不出现在渲染输出中 |
| TUI-TODO-017 | Not Started | 实现 status panel fake 展示 | TUI 详设 7.1、7.4、9.5.8、11 TUI-DES-008 | stage/tool/pending/budget/recovery/health/safe mode/decision summary | L3 | `apps/tui/src/view/TuiStatusPanel.h`、`apps/tui/src/view/TuiStatusPanel.cpp` | `render_status_panel()`、`format_stage_badge()`、`format_health_summary()` | `TuiStatusPanelTest` | `ctest --preset vscode-linux-ninja -R "TuiStatusPanel" --output-on-failure` | TUI-TODO-008、012 | 无 | fake projection 可用 | status panel + tests | 缺字段显示 unknown/degraded，状态不只依赖颜色表达 |
| TUI-TODO-018 | Not Started | 实现 terminal capability probe | TUI 详设 5.7、9.5.10、11 TUI-DES-010 | TTY、TERM、尺寸、UTF-8、paste、resize、external editor | L3 | `apps/tui/src/terminal/TuiTerminalCapabilityProbe.h`、`apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp` | `probe()`、`select_startup_mode()`、`format_startup_error()`、`TuiTerminalCapabilities` | `TuiTerminalCapabilityProbeTest` | `ctest --preset vscode-linux-ninja -R "TuiTerminalCapability" --output-on-failure` | TUI-TODO-007 | 无 | startup mode taxonomy 已按详设固定 | probe + tests | 非 TTY、尺寸过小、TERM 异常、外部编辑器缺失均有可判定 result code 或 startup issue |
| TUI-TODO-019 | Not Started | 实现 FTXUI renderer adapter | TUI 详设 8.4、9.5.11、11 TUI-DES-004；小样 TODO TUI-PROTO-008/015 | root layout、tokens、metrics、snapshot | L2/L3 | `apps/tui/src/terminal/FtxuiRendererAdapter.h`、`apps/tui/src/terminal/FtxuiRendererAdapter.cpp`、`apps/tui/src/view/TuiDesignTokens.h`、`apps/tui/src/view/TuiLayoutMetrics.h` | `render_root()`、`render_to_screen()`、`apply_layout_metrics()` | `TuiDesignTokensTest`、`TuiMainLayoutSnapshotTest` | `ctest --preset vscode-linux-ninja -R "Tui(DesignTokens|MainLayoutSnapshot)" --output-on-failure` | TUI-TODO-005、009、016、017 | BLK-TUI-005 | FTXUI dependency 可解析且 private link 成立 | renderer adapter + snapshots | FTXUI 类型不进入 model/reducer/data source；80x24/120x36 基线无重叠 |
| TUI-TODO-020 | Not Started | 接线 fake-only `TuiApp` 小样 | TUI 详设 9.5.1、10 Phase 1、附件 A；小样 TODO TUI-PROTO-014/016/017 | probe -> fake source -> reducer -> renderer loop -> exit | L2 | `apps/tui/src/app/TuiApp.h`、`apps/tui/src/app/TuiApp.cpp` | `TuiApp::run(TuiAppOptions)`、`dispatch_action()`、`tick()`、`shutdown()` | `TuiAppStartupTest`、`TuiPrototypeSmokeTest` | `ctest --preset vscode-linux-ninja -R "Tui(AppStartup|PrototypeSmoke)" --output-on-failure` | TUI-TODO-012、014、015、018、019 | BLK-TUI-006 | CJK/IME/resize manual gate 作为转正前置，不阻断 fake smoke | prototype app + smoke + review evidence | 小样可启动退出、可演示 fake transcript/status/selector/composer，不接 daemon、不安装 |

### 6.3 正式 data source、projection 与交互链路

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TUI-TODO-021 | Not Started | 定义 daemon projection 请求响应映射 | TUI 详设 7.4、9.5.4、10 Phase 3/4；AccessUnaryProductionPathV1 | TUI DTO <-> daemon/access projection DTO | L2 | `apps/tui/src/ipc/TuiIpcController.h`、`docs/todos/tui/deliverables/TUI-TODO-021-daemon-projection-mapping.md` | `TuiIpcRequestEnvelope`、`TuiIpcResponseEnvelope`、五个 operation 映射表 | `TuiDaemonProjectionMappingTest` | `rg -n "open_session|submit_turn|poll_events|route_catalog|close_session|permission_denied|timeout|malformed_response" docs/todos/tui/deliverables/TUI-TODO-021-daemon-projection-mapping.md` | TUI-TODO-003、020 | 无 | 无；等待 `TUI-TODO-020` 提供 fake-only app 调用上下文并把 mapping/header 草案落盘 | mapping 文档 + header 草案 | 映射表覆盖 open/submit/poll/route/close 和 `socket_missing`/`permission_denied`/`timeout`/`malformed_response`，且不复用 CLI projection |
| TUI-TODO-022 | Not Started | 实现 `TuiIpcController` 错误归一化 | TUI 详设 9.5.4；5.7 | 本地 IPC 请求、超时、序列化、错误 mapping | L2 | `apps/tui/src/ipc/TuiIpcController.cpp` | `open_session()`、`submit_turn()`、`poll_events()`、`query_route_catalog()`、`close_session()` | `TuiIpcControllerTest`、`TuiIpcPermissionDeniedTest` | `ctest --preset vscode-linux-ninja -R "TuiIpc" --output-on-failure` | TUI-TODO-021 | 无 | 无；待 `TUI-TODO-021` 落盘 request/response mapping 与 platform IIPC 使用方式 | IPC controller + tests | `socket_missing`、`permission_denied`、`timeout`、`schema_mismatch`、`malformed_response` 均转为稳定 user-visible reason code |
| TUI-TODO-023 | Blocked | 实现 `DaemonTuiDataSource` | TUI 详设 9.5.3、10 Phase 4、11 TUI-DES-011 | open session、submit/poll、route catalog、close session | L2 | `apps/tui/src/data/DaemonTuiDataSource.h`、`apps/tui/src/data/DaemonTuiDataSource.cpp` | `open_session()`、`submit_turn()`、`poll_events()`、`route_catalog()`、`close_session()` | `DaemonTuiDataSourceContractTest` | `ctest --preset vscode-linux-ninja -R "TuiDaemonDataSource" --output-on-failure` | TUI-TODO-022 | BLK-TUI-007 | `TUI-TODO-003` 已冻结 projection seam；剩余阻塞仅为 session open/close/query seam 可用 | daemon data source + contract tests | 上层 `TuiApp` 不感知 fake/daemon source 差异，daemon unavailable 时 fail-closed/banner 明确 |
| TUI-TODO-024 | Not Started | 验证启动降级与 daemon unavailable 路径 | TUI 详设 5.7、9.5.1、9.5.10；BinaryEntrypointReadinessV1 | non-TTY、narrow、daemon unavailable、permission denied、profile missing | L2 | `apps/tui/src/app/TuiApp.cpp`、`apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp` | `TuiStartupMode`、`format_startup_error()` | `TuiAppStartupFailureTest` | `ctest --preset vscode-linux-ninja -R "TuiAppStartupFailure" --output-on-failure` | TUI-TODO-001、018、023 | 无 | 无；daemon error mapping 已由 `TUI-TODO-003` 冻结，待 018/023 提供启动路径与 daemon source | startup failure tests | 每个启动失败场景都有确定 exit code 或 fallback mode，且不隐式提权、不启动系统 daemon |
| TUI-TODO-025 | Not Started | 接入 status/tool/recovery 投影刷新 | TUI 详设 7.1、7.4、9.5.8、10 Phase 5 | status projection -> model -> status panel | L2 | `apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/model/TuiReducer.cpp`、`apps/tui/src/view/TuiStatusPanel.cpp` | `TuiStatusProjection`、`TuiToolSummaryView`、`TuiEventProjection` | `TuiStatusProjectionContractTest`、`TuiStatusPanelIntegrationTest` | `ctest --preset vscode-linux-ninja -R "Tui(StatusProjection|StatusPanelIntegration)" --output-on-failure` | TUI-TODO-023 | 无 | 无；status/tool/event 字段边界已由 `TUI-TODO-003` 冻结，待 023 提供 daemon producer | integration tests | stage/tool/pending/budget/recovery/safe-mode projection 可随 poll 刷新，且不展示内部对象 dump |
| TUI-TODO-026 | Blocked | 接线 `/exit` 与 `/clear` 会话动作 | TUI 详设 5.2、5.6、9.5.9、10 Phase 4 | foreground session lifecycle | L2 | `apps/tui/src/command/TuiSlashCommandParser.cpp`、`apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/model/TuiReducer.cpp` | `/exit` action、`/clear` action、`close_session()` | `TuiSessionLifecycleIntegrationTest` | `ctest --preset vscode-linux-ninja -R "TuiSessionLifecycle" --output-on-failure` | TUI-TODO-002、023 | BLK-TUI-007 | `/clear` 语义已冻结；剩余阻塞只在 daemon session seam | parser/data source/reducer changes + tests | `/exit` 必定触发 close 或可观测 close failure；`/clear` 行为与冻结决策一致 |
| TUI-TODO-027 | Not Started | 冻结 route catalog projection 字段 | TUI 详设 6.2~6.4、7.4、10 Phase 6 | Current route、disabled reason、verification/health/profile allowlist | L2 | `docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md`、`apps/tui/src/data/TuiProjectionTypes.h` | `TuiRouteCatalogView`、`TuiModelRouteProjection` | `TuiRouteCatalogProjectionTest` | `rg -n "current_provider_id|current_model_id|disabled_reasons|allowlist|verification_state|health" docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md` | TUI-TODO-004、015 | 无 | `TUI-TODO-004` 已冻结 carrier 与 fail-closed 规则；待 015 提供 selector 本地语义后继续细化 projection 字段 | projection 字段表 | 字段表能表达 Auto/PreferDepth/PinModel 所需展示信息，且不含 provider secret 或完整 profile 文件 |
| TUI-TODO-028 | Not Started | 接入 route catalog projection 消费 | TUI 详设 6.3、6.4、9.5.6、10 Phase 6 | daemon route catalog -> selector options | L2 | `apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/view/TuiModelSelector.cpp` | `route_catalog()`、`render_disabled_reason()` | `TuiRouteCatalogProjectionTest`、`TuiModelSelectorDaemonTest` | `ctest --preset vscode-linux-ninja -R "Tui(RouteCatalogProjection|ModelSelectorDaemon)" --output-on-failure` | TUI-TODO-027 | 无 | route catalog projection endpoint 可用 | selector daemon tests | selector 展示真实 projection 中的可用/禁用项，并保留 disabled reason |
| TUI-TODO-029 | Not Started | 验证 next preference 提交与回显 | TUI 详设 6.4~6.6、10 Phase 6 | Next preference draft -> submit turn -> effective route projection | L2 | `apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/view/TuiModelSelector.cpp` | `NextTurnPreference`、`submit_turn()`、`TuiModelRouteProjection` | `TuiNextPreferenceIntegrationTest` | `ctest --preset vscode-linux-ninja -R "TuiNextPreference" --output-on-failure` | TUI-TODO-004、028 | 无 | `TUI-TODO-004` 已冻结 carrier 与 fail-closed 语义；待 028 提供 daemon selector consumption 后验证提交回显 | integration test | Auto/PreferDepth/PinModel 提交后能回显 effective route 或明确 fail-closed reason，不绕过 ModelRouter |

### 6.4 命令迁移、测试门禁与交付证据

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TUI-TODO-030 | Blocked | 收敛 bare `dasall` 迁移门禁证据 | TUI 详设 5.1、附件 B.0、B.4.4；SystemIntegrationGateMatrix | TUI ready、权限模型、projection、selector、旧入口 inventory、packaging matrix | L1 | 无生产代码；新增 `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` | `TuiCommandReleaseGateEvidence`、`DasallCommandMigrationInventory` | design consistency | `rg -n "B.0|权限模型|projection|selector|packaging smoke|dasall-cli|/usr/bin/dasall|debian|scripts/packaging|inventory" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` | TUI-TODO-001、020、024、025、029 | BLK-TUI-008 | 小样评审、权限模型、daemon attach、status/route projection、packaging smoke matrix、旧 `dasall` 控制面调用 inventory 均有证据 | gate evidence 文档 + inventory | 文档逐项证明 B.0 前置门禁已满足；列出 Debian、scripts、manpage、postinst、autopkgtest、package smoke 中旧入口的迁移/保留策略；未满足项保持 Blocked |
| TUI-TODO-031 | Blocked | 释放 `dasall-cli` 产物名 | TUI 详设附件 B.4.1、B.5 CMD-REL-001 | CLI target 输出 `dasall-cli` | L2 | `apps/cli/CMakeLists.txt` | `dasall-cli` target `OUTPUT_NAME` | `CliControlPlaneCommandNameTest` | `cmake --build --preset vscode-linux-ninja --target dasall-cli` | TUI-TODO-030 | BLK-TUI-008 | 命令迁移 gate evidence 通过 | CMake change + command name test | build tree 生成 `dasall-cli`，且旧结构化 CLI 不再产出 bare `dasall` |
| TUI-TODO-032 | Blocked | 新增正式 TUI `dasall` target | TUI 详设 9.1、附件 B.4.1/B.4.2、B.5 CMD-REL-002 | `dasall-tui` target 输出 `dasall` 并安装 | L2 | `apps/tui/CMakeLists.txt`、`apps/tui/src/main.cpp`、`apps/CMakeLists.txt` | `dasall-tui` target、`OUTPUT_NAME dasall`、install rule | `DasallTuiEntrypointSmokeTest` | `cmake --build --preset vscode-linux-ninja --target dasall-tui` | TUI-TODO-023、024、030、031 | BLK-TUI-008 | 正式 daemon source 可用且 command release gate 通过 | formal target + smoke | `dasall-tui` 构建产物进入 TUI 主路径，安装规则指向 `/usr/bin/dasall` |
| TUI-TODO-033 | Blocked | 更新 Debian 命令迁移文件 | TUI 详设附件 B.4.2~B.4.5 | install、manpage、README.Debian、postinst、autopkgtest、packaging scripts | L2 | `debian/dasall-cli.install`、`debian/dasall.1`、`debian/README.Debian`、`debian/postinst`、`debian/tests/*`、`scripts/packaging/*` | Debian install/manpage/smoke entries | `DasallCommandRoutingTest`、package smoke | `rg -n "dasall (config|ping|readiness|run|status|cancel|diag|knowledge)" debian scripts` | TUI-TODO-030、031、032 | BLK-TUI-008 | TUI formal target、CLI command name、旧入口 inventory 均已落地 | Debian/script changes + tests | 控制面示例全部切到 `dasall-cli` 或在 inventory 中说明必须保留的 compatibility 路径；bare `dasall` 只描述 TUI，package smoke 覆盖双命令 |
| TUI-TODO-034 | Blocked | 增加命令分流测试 | TUI 详设附件 B.6；SystemIntegrationGateMatrix | `dasall` vs `dasall-cli` role split | L2 | `tests/integration/apps/tui/DasallCommandRoutingTest.cpp`、`tests/integration/apps/CMakeLists.txt` | `DasallCommandRoutingTest` | command routing integration | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` | TUI-TODO-032、033 | BLK-TUI-008 | 双命令 build/install 规则完成 | integration test | 测试证明 `dasall` 进入 TUI，`dasall-cli` 保留结构化控制面，旧 `dasall <subcommand>` fail-closed |
| TUI-TODO-035 | Not Started | 回写 TUI 交付证据 | TUI 详设 10 Phase 9；SystemIntegrationGateMatrix 5 | deliverables、worklog、manual review、gate status | L1 | `docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md`、`docs/worklog/DASALL_开发执行记录.md` | `TuiDeliveryEvidence` | evidence consistency | `rg -n "TUI-TODO-035|Gate-TUI|ctest|cmake --build|采纳|延后|回退" docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md docs/worklog/DASALL_开发执行记录.md` | TUI-TODO-020、030 或对应阶段任务 | 无 | 每阶段 focused command 结果可复验 | evidence + worklog | 证据文档记录任务号、命令、结果、残余风险、后继任务；未跑命令不得写通过 |

## 7. 执行顺序建议

### 7.1 分阶段顺序

| 阶段 | 执行任务 | 串并行建议 | 进入下一阶段条件 |
|---|---|---|---|
| Phase 0 补设计与测试拓扑 | TUI-TODO-001~006 | 001~005 可并行；006 可与 005 并行 | 权限、`/clear`、projection、preference、FTXUI 评审至少形成明确 Blocked/Ready 结论；即使 FTXUI 仍 Open，TUI 测试拓扑也必须可发现 |
| Phase 1 no-daemon 小样骨架 | TUI-TODO-007~012 | 007 仅依赖 006；FTXUI 未解锁时使用 mock/no-renderer；007 后，008/009/011 可并行；012 依赖 011 | prototype target 可构建，model/data source 可单测，且不 link daemon/runtime/provider |
| Phase 2 UI 组件和小样评审 | TUI-TODO-013~020 | 013/014/015/016/017/018 可在 010/012 后并行；019 依赖 FTXUI 评审；020 汇总 app | full-screen 小样可运行，snapshot/IME/CJK/resize 证据明确，且 no-daemon 边界成立 |
| Phase 3 daemon projection 与 session | TUI-TODO-021~026 | 021 是串行前置；022/023 串行；024/025/026 在 023 后按场景并行 | daemon attach、startup failure、status projection、session lifecycle  focused tests 通过 |
| Phase 4 route selector 真链路 | TUI-TODO-027~029 | 027 前置；028/029 串行 | next preference 提交和 effective route 回显可验证，或保持 Blocked 不迁移命令 |
| Phase 5 命令与打包迁移 | TUI-TODO-030~034 | 030 是硬门禁；031 -> 032 -> 033/034 | build/install/package smoke 证明 `dasall` 与 `dasall-cli` 角色分离 |
| Phase 6 证据收口 | TUI-TODO-035 | 每个阶段完成后滚动回写 | worklog 与 deliverables 记录 focused command、结果和残余风险 |

### 7.2 必过门禁表

| Gate ID | 触发时机 | 通过条件 | 对应任务 | 验收命令 | 回退动作 |
|---|---|---|---|---|---|
| Gate-TUI-01 | Phase 0 后 | 权限模型、`/clear`、projection、preference 的 Ready/Blocked 状态均有证据 | 001~004 | `rg -n "TUI-TODO-00[1-4]" docs/todos/tui/deliverables` | 不进入 daemon/command 实现，保持 Blocked |
| Gate-TUI-02 | prototype target 后 | `apps/tui` target 可构建且不安装、不改变 bare `dasall`；FTXUI 未解锁时允许 mock/no-renderer skeleton | 006~007 | `cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype` | 回退 CMake 接线或 mock renderer 边界 |
| Gate-TUI-03 | model/data source 后 | model/reducer/data source 不依赖 FTXUI/daemon/runtime/provider 私有头 | 008~012 | `ctest --preset vscode-linux-ninja -R "Tui(ScreenModel|Reducer|FakeDataSource|ProjectionTypes|ITuiDataSource)" --output-on-failure` | 阻断 UI 组件继续依赖私有实现 |
| Gate-TUI-04 | renderer/snapshot 后 | 80x24、120x36、narrow CJK、selector modal、busy draft 无重叠 | 016~020 | `ctest --preset vscode-linux-ninja -R "Tui(DesignTokens|MainLayoutSnapshot|RenderSnapshot|PrototypeSmoke)" --output-on-failure` | 回退 layout metrics 或输入降级策略 |
| Gate-TUI-05 | 小样评审后 | 样品采纳/延后/废弃清单回写，CJK/IME/resize 证据明确 | 020、035 | `rg -n "采纳|延后|废弃|CJK|IME|resize" docs/todos/tui/deliverables` | 不进入正式 daemon attach |
| Gate-TUI-06 | daemon attach 后 | open/submit/poll/status/route/close projection focused tests 通过 | 021~026 | `ctest --preset vscode-linux-ninja -R "Tui(DaemonDataSource|Ipc|StatusProjection|SessionLifecycle|AppStartupFailure)" --output-on-failure` | 回退到 fake-only，不迁移命令 |
| Gate-TUI-07 | selector 真链路后 | next preference 真提交和 effective route 回显可验证，不绕过 ModelRouter | 027~029 | `ctest --preset vscode-linux-ninja -R "Tui(NextPreference|RouteCatalogProjection|ModelSelectorDaemon)" --output-on-failure` | selector 保持 fake/local draft，不宣称真实 pin/depth 生效 |
| Gate-TUI-08 | 命令迁移前 | B.0 前置证据全部满足，packaging matrix 明确 | 030 | `rg -n "B.0|packaging smoke|dasall-cli|/usr/bin/dasall" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` | 延后 `dasall` 释放，保留 prototype/non-installed |
| Gate-TUI-09 | 命令迁移后 | `dasall` 与 `dasall-cli` 构建/安装/文档/smoke 角色分离 | 031~034 | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` | 回退整组 build + install + docs 迁移，不上线半截命令改动 |
| Gate-TUI-10 | 每阶段结束 | focused command、结果、残余风险、后继任务已回写 | 035 | `rg -n "Gate-TUI|ctest|cmake --build|回退|残余风险" docs/todos/tui/deliverables docs/worklog/DASALL_开发执行记录.md` | 不允许把任务标记 Done |

## 8. 阻塞项与解阻条件

| Blocker ID | 状态 | 阻塞内容 | 影响任务 | 证据来源 | 解阻条件 | 回退策略 |
|---|---|---|---|---|---|---|
| BLK-TUI-001 | Closed | TUI 默认启动身份与普通用户/root operator 口径已由 TUI-TODO-001 冻结 | 024、030~034 | TUI 详设 5.7、13 TUI-RISK-002/TUI-OQ-004；`docs/todos/tui/deliverables/TUI-TODO-001-启动身份与权限模型决策.md` | 已完成：冻结 daemon-backed root/sudo-only、ordinary-user fail-closed、user-level daemon future-only 与 permission denied 文案边界 | 命令迁移继续守住 non-installed/prototype 策略，直到 Gate-TUI-08 通过 |
| BLK-TUI-002 | Closed | `/clear` 已由 TUI-TODO-002 冻结为“新前台 session 语义、保留 input history、即时 daemon close/open 后置” | 002、013、026 | TUI 详设 5.2、5.6、13 TUI-OQ-001；`docs/todos/tui/deliverables/TUI-TODO-002-clear语义决策.md` | 已完成：Product + session owner 已冻结 `/clear` 行为和 close/open 边界 | parser 不再返回 blocked 占位；真实 daemon lifecycle 继续由 BLK-TUI-007 约束 |
| BLK-TUI-003 | Closed | daemon/access TUI projection endpoint、字段和错误码已由 TUI-TODO-003 冻结 | 003、021~025 | TUI 详设 7.4、9.5.3、9.5.4；AccessUnaryProductionPathV1；`docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` | 已完成：deliverable 已冻结 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`、五个 operation、DTO 字段边界与 reason code taxonomy | 021/022/024/025 不再因该 blocker 保持 Blocked；session lifecycle 剩余风险继续由 BLK-TUI-007 约束 |
| BLK-TUI-004 | Closed | `NextTurnPreference` 真链路承载位置已由 TUI-TODO-004 冻结为 access/runtime typed carrier，再映射到 llm-local route input | 004、015、027~029 | TUI 详设 6.6；RuntimePolicyConsumerMatrix；`docs/todos/tui/deliverables/TUI-TODO-004-next-turn-preference承载决策.md` | 已完成：deliverable 已拒绝 `request_context` / `client_capabilities` / profile override，并固定 `Auto` / `PreferDepth` / `PinModel` 的失败语义 | 027~029 继续落 route catalog projection、daemon selector consumption 与 submit echo；015 仍只交付 fake selector |
| BLK-TUI-005 | Open | FTXUI 来源、版本、offline cache、Debian policy 风险未验证 | 005、019、020 | TUI 详设 8.4；`cmake/DASALLThirdParty.cmake` | third-party 接入评审通过，private link/no-leak gate 可执行 | 使用 mock/no-renderer skeleton 维持 target、model、composer、fake tests，延后 full-screen renderer 与 snapshot |
| BLK-TUI-006 | Open | CJK/IME/resize/composer 终端行为需人工样品证据 | 014、019、020 | TUI 详设 8.3、12、附件 A.7 | 完成 80x24/120x36/CJK/IME/resize/manual review 证据 | 降级为行输入 + `/editor`，不承诺复杂 composer |
| BLK-TUI-007 | Open | runtime session open/close/query seam 不完整 | 023、026 | TUI 详设 4.4、5.2、10 Phase 4 | access/runtime 冻结前台 session projection/open/close/query 最小 seam | `/exit` 可本地退出并记录 close unavailable，`/clear` 保持本地视图行为 |
| BLK-TUI-008 | Open | bare `dasall` 命令迁移前置门禁未满足 | 030~034 | TUI 详设附件 B.0~B.7；SystemIntegrationGateMatrix | TUI ready、权限模型、projection、selector、Debian smoke、compat matrix 均通过 | 不改 `apps/cli` OUTPUT_NAME，不安装 TUI 为 `dasall` |

## 9. 验收与质量门

### 9.1 单元测试矩阵

| 测试入口 | 覆盖任务 | 验收命令 |
|---|---|---|
| `TuiProjectionTypesTest` | 008 | `ctest --preset vscode-linux-ninja -R "TuiProjectionTypes" --output-on-failure` |
| `TuiScreenModelTest` | 009 | `ctest --preset vscode-linux-ninja -R "TuiScreenModel" --output-on-failure` |
| `TuiReducerTransitionTest` | 010 | `ctest --preset vscode-linux-ninja -R "TuiReducer" --output-on-failure` |
| `ITuiDataSourceContractTest` | 011 | `ctest --preset vscode-linux-ninja -R "ITuiDataSource" --output-on-failure` |
| `FakeTuiDataSourceTest` / `TuiFakeScenarioCatalogTest` | 012 | `ctest --preset vscode-linux-ninja -R "Tui(FakeDataSource|FakeScenarioCatalog)" --output-on-failure` |
| `TuiSlashCommandParserTest` | 013 | `ctest --preset vscode-linux-ninja -R "TuiSlashCommandParser" --output-on-failure` |
| `TuiComposerTest` / `TuiComposerHistoryTest` | 014 | `ctest --preset vscode-linux-ninja -R "TuiComposer" --output-on-failure` |
| `TuiModelSelectorTest` / `TuiRouteCatalogFilterTest` | 015 | `ctest --preset vscode-linux-ninja -R "Tui(ModelSelector|RouteCatalog)" --output-on-failure` |
| `TuiTranscriptViewTest` | 016 | `ctest --preset vscode-linux-ninja -R "TuiTranscriptView" --output-on-failure` |
| `TuiStatusPanelTest` | 017 | `ctest --preset vscode-linux-ninja -R "TuiStatusPanel" --output-on-failure` |
| `TuiTerminalCapabilityProbeTest` | 018 | `ctest --preset vscode-linux-ninja -R "TuiTerminalCapability" --output-on-failure` |
| `TuiDesignTokensTest` / `TuiMainLayoutSnapshotTest` | 019 | `ctest --preset vscode-linux-ninja -R "Tui(DesignTokens|MainLayoutSnapshot)" --output-on-failure` |

### 9.2 集成与门禁测试矩阵

| 测试入口 | 覆盖任务 | 验收命令 | 证据层级 |
|---|---|---|---|
| `TuiAppStartupTest` / `TuiPrototypeSmokeTest` | 020 | `ctest --preset vscode-linux-ninja -R "Tui(AppStartup|PrototypeSmoke)" --output-on-failure` | prototype smoke |
| `TuiIpcControllerTest` / `TuiIpcPermissionDeniedTest` | 022 | `ctest --preset vscode-linux-ninja -R "TuiIpc" --output-on-failure` | focused integration |
| `DaemonTuiDataSourceContractTest` | 023 | `ctest --preset vscode-linux-ninja -R "TuiDaemonDataSource" --output-on-failure` | projection contract-like |
| `TuiAppStartupFailureTest` | 024 | `ctest --preset vscode-linux-ninja -R "TuiAppStartupFailure" --output-on-failure` | failure injection |
| `TuiStatusProjectionContractTest` / `TuiStatusPanelIntegrationTest` | 025 | `ctest --preset vscode-linux-ninja -R "Tui(StatusProjection|StatusPanelIntegration)" --output-on-failure` | projection integration |
| `TuiSessionLifecycleIntegrationTest` | 026 | `ctest --preset vscode-linux-ninja -R "TuiSessionLifecycle" --output-on-failure` | session integration |
| `TuiRouteCatalogProjectionTest` / `TuiModelSelectorDaemonTest` | 027~028 | `ctest --preset vscode-linux-ninja -R "Tui(RouteCatalogProjection|ModelSelectorDaemon)" --output-on-failure` | route projection integration |
| `TuiNextPreferenceIntegrationTest` | 029 | `ctest --preset vscode-linux-ninja -R "TuiNextPreference" --output-on-failure` | cross-owner integration |
| `DasallTuiEntrypointSmokeTest` | 032 | `cmake --build --preset vscode-linux-ninja --target dasall-tui` | app-binary smoke |
| `DasallCommandRoutingTest` | 034 | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` | release/app-binary gate |

### 9.3 文档与交付证据检查

| 检查项 | 覆盖任务 | 验收命令 |
|---|---|---|
| 补设计 deliverables 完整 | 001~005、021、027、030、035 | `rg -n "TUI-TODO-00[1-5]|TUI-TODO-021|TUI-TODO-027|TUI-TODO-030|TUI-TODO-035" docs/todos/tui/deliverables` |
| 禁止 stream-ready 误表述 | 全文 | `rg -n "stream-ready|streaming" docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md docs/architecture/DASALL_TUI客户端设计方案.md` |
| 命令迁移旧入口残留 | 031~034 | `rg -n "dasall (config|ping|readiness|run|status|cancel|diag|knowledge)" debian scripts docs` |
| worklog 回写 | 035 | `rg -n "TUI-TODO|Gate-TUI|ctest|cmake --build" docs/worklog/DASALL_开发执行记录.md docs/todos/tui/deliverables` |

### 9.4 统一验收命令建议

当前阶段不得用一个聚合命令冒充所有未来 gate 已可执行。按阶段建议如下：

| 阶段 | one-shot focused command |
|---|---|
| fake-only prototype | `cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype && ctest --preset vscode-linux-ninja -R "Tui(ProjectionTypes|ScreenModel|Reducer|FakeDataSource|SlashCommandParser|Composer|ModelSelector|TranscriptView|StatusPanel|TerminalCapability|MainLayoutSnapshot|PrototypeSmoke)" --output-on-failure` |
| daemon attach | `ctest --preset vscode-linux-ninja -R "Tui(Ipc|DaemonDataSource|AppStartupFailure|StatusProjection|SessionLifecycle)" --output-on-failure` |
| selector true chain | `ctest --preset vscode-linux-ninja -R "Tui(RouteCatalogProjection|ModelSelectorDaemon|NextPreference)" --output-on-failure` |
| command migration | `cmake --build --preset vscode-linux-ninja --target dasall-cli && cmake --build --preset vscode-linux-ninja --target dasall-tui && ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` |

## 10. 风险与回退策略

| Risk ID | 风险 | 影响 | 对应设计 Risk | 触发信号 | 回退策略 |
|---|---|---|---|---|---|
| TUI-RSK-001 | 普通用户默认启动只看到 permission denied | `dasall` 作为人机入口体验不可用 | 详设 TUI-RISK-002/TUI-OQ-004 | 忽略 TUI-TODO-001 冻结口径而提前迁移 bare `dasall` | 不迁移 bare `dasall`；保留 `dasall_tui_prototype` 或受控 root/sudo-only operator 文档 |
| TUI-RSK-002 | fake-only 小样被误写成 production ready | 误导 release / packaging gate | 详设 4.3、附件 A | 小样 smoke 通过但 daemon attach 未做 | 所有小样证据标注 prototype smoke，不进入 Gate-TUI-06/09 |
| TUI-RSK-003 | FTXUI CJK/IME/resize 不达标 | full-screen 小样与复杂 composer 体验不可用 | 详设 TUI-RISK-006 | manual gate 失败 | 保留 no-daemon skeleton、model/reducer/fake tests；降级为行输入 + `/editor`；延后复杂 composer |
| TUI-RSK-004 | FTXUI 依赖泄漏到核心模块 | 破坏架构边界 | 详设 TUI-RISK-008 | access/runtime/contracts include FTXUI | no-leak test 阻断；FTXUI 仅 private link `apps/tui` |
| TUI-RSK-005 | TUI selector 越权成为第二 ModelRouter | 破坏 profiles/llm owner | 详设 TUI-RISK-004/TUI-RISK-005 | TUI 直接修改 profile、`client_capabilities` 或 provider secret，或在 `PinModel` 失败后静默回落 | selector 只保留 next-turn draft；真实 carrier 已冻结但 build 未落地前不宣称生效 |
| TUI-RSK-006 | streaming supporting types 被误读为 ready | 错误承诺实时流式 | 详设 TUI-RISK-003 | 文档/CLI 帮助出现 stream-ready | 统一改回 unary/polling；bounded event feed 另立设计 |
| TUI-RSK-007 | daemon projection 字段泄漏 internal dump/secret | 安全与 contracts 污染 | 详设 7.2、9.6 | projection 含 raw tool output、secret、runtime internals | projection 字段表拒绝；只保留 summary/reason/ref/timestamp |
| TUI-RSK-008 | 命令迁移破坏脚本与 package smoke | operator 自动化失败 | 详设 TUI-RISK-007、附件 B | `rg` 仍发现旧 `dasall status/config` | 整体延后命令释放；不做运行时兼容分流半成品 |
| TUI-RSK-009 | `/clear` 实现偏离已冻结会话语义 | transcript/session 数据错乱 | 详设 TUI-OQ-001 | 继续复用旧 `session_id`、误清 input history，或把 `/clear` 写成即时 daemon close/open | 以 TUI-TODO-002 deliverable 为准；parser/reducer/integration tests 强制区分 `/clear` 与 `/exit` |
| TUI-RSK-010 | startup readiness 重新定义 app binary 状态 | 与 SSOT 冲突 | BinaryEntrypointReadinessV1 | TUI 显示 default-ready 但只来自 ping | header 只消费 daemon projection，不自行推导 readiness |

## 11. 可行性结论

| 结论项 | 结论 |
|---|---|
| 是否可以进入执行 | 可以分阶段进入执行，但不能全量一次性推进。Phase 0~1 的 no-daemon skeleton、module-local DTO/model/reducer/fake data source 可直接执行；Phase 2 的 full-screen renderer/snapshot 依赖 FTXUI 与终端样品 gate；Phase 3 的 daemon projection mapping / IPC 归一化已可继续推进，Phase 4~5 仍需 session seam 与 route/terminal 条件逐项解阻 |
| 当前可落到的最细粒度 | `TuiScreenModel`、`TuiReducer`、`ITuiDataSource`、`FakeTuiDataSource`、`TuiSlashCommandParser`、`TuiComposer`、`TuiModelSelector`、`TuiTerminalCapabilityProbe` 可落到 L3；`TuiIpcController` mapping/normalization、FTXUI renderer、daemon data source、route projection 可落到 L2；权限/命令迁移保持 L1/Blocked |
| 不能继续细化的证据缺口 | runtime session open/close/query seam 不完整；FTXUI CJK/IME/resize 与 Debian policy 需样品和打包证据 |
| 是否存在 breaking change 风险 | 存在。bare `dasall` 从结构化 CLI 切到 TUI 是公开命令 breaking change，必须经过 Gate-TUI-08 和 Gate-TUI-09，不得在早期 TODO 中默认推进 |
| 推荐执行策略 | 先执行 TUI-TODO-006~012，形成 no-daemon skeleton 与 module-local 数据/模型底座；并行推进 TUI-TODO-005 的剩余设计解阻。随后在 FTXUI 与终端 gate 明确后执行 TUI-TODO-013~020；再以 TUI-TODO-021~029 补 daemon/status/route 真链路；最后在 TUI-TODO-030 证据通过后执行命令迁移 |
| 当前专项 TODO 状态 | Build-ready subset：TUI-TODO-006~018，其中 TUI-TODO-007 仅要求 no-daemon/mock-renderer skeleton；Renderer-ready subset：TUI-TODO-019~020，依赖 TUI-TODO-005 与 BLK-TUI-006；Design/Review-ready subset：TUI-TODO-005、021、027、030、035；Not-started after blocker closure：TUI-TODO-028~029 继续按 027/028 串行推进；Blocked subset：TUI-TODO-023、026、030~034 中依赖 external owner 的 session/命令迁移任务 |

最终判定：TUI 客户端专项 TODO 已具备工程实施计划和可执行任务表，但执行时必须遵守“fake-only 小样先行、projection seam 再行、命令迁移最后”的顺序。任何跳过权限模型、daemon projection、selector 真链路或 packaging smoke 的实现，都不得标记为 Done 或 release-ready。

## 12. 未决问题处置表

| 未决问题 | 当前处置 | Owner / 评审方 | 对应任务 | 完成条件 |
|---|---|---|---|---|
| TUI 默认启动身份与普通用户权限 | 已冻结：daemon-backed 路径沿用 root/sudo-only operator backend；ordinary-user full-function 保持 future-only；命令迁移继续受 gate 约束 | Product / Security / Access | TUI-TODO-001、024、030 | TUI-TODO-001 已完成；024/030 后续按冻结口径实现 startup failure 与 command release gate |
| `/clear` 会话语义 | 已冻结：清空当前前台 transcript 与本地状态，留在当前进程并切到新前台 session 语义；input history 保留，daemon close/open 细节继续后置 | Product / Runtime / Access | TUI-TODO-002、013、026 | TUI-TODO-002 已完成；013 继续落 local action，026 在 BLK-TUI-007 解阻后实现真实 lifecycle |
| daemon projection 与 session seam | projection seam 已冻结；正式 data source 仍受 session seam 阻塞，fake data source 先行 | Access / Daemon / Runtime | TUI-TODO-003、021~026 | `TUI-TODO-003` 已完成；021/022 落盘 mapping/controller，023/026 在 `BLK-TUI-007` 解阻后补 session lifecycle |
| `NextTurnPreference` 真链路 | 已冻结：access/runtime typed request-scope carrier -> llm-local route input；`request_context` / `client_capabilities` / profile override 均被拒绝 | Access / Runtime / Profiles / LLM | TUI-TODO-004、015、027~029 | `TUI-TODO-004` 已完成；后续 027~029 继续落 route catalog projection、daemon selector consumption 与 submit echo |
| FTXUI third-party 与终端行为 | no-daemon skeleton 不等待 FTXUI；renderer/snapshot/full-screen 小样等待 spike | Build / Packaging / TUI | TUI-TODO-005、019、020 | 来源、版本/commit、offline fallback、Debian policy、CJK/IME/resize/manual review 证据通过 |
| LLM streaming 口径漂移 | 详设已修正为 LLM internal streaming 已有基础；TUI 首版仍不宣称 stream-ready | LLM / Access / Runtime / TUI | TUI-TC003、TUI-TODO-020、021 | 不再出现“LLMManager streaming 未实现”旧口径；也不把 supporting shape 当作端到端 stream-ready |
| 小样 TODO 与主 TODO 双账并行 | 本文作为唯一执行账本，小样 TODO 降为参考输入 | TUI | TUI-PP-07、TUI-TODO-006~020、035 | 状态推进、证据回写和 Done 判定只更新本文；小样内容只迁入本文任务或 deliverable |
| bare `dasall` 命令迁移影响面 | 保持 Blocked，先做旧入口 inventory 和 packaging matrix | TUI / CLI / Packaging | TUI-TODO-030~034 | Debian、scripts、manpage、postinst、autopkgtest、package smoke 的旧结构化控制面调用全部有迁移/保留策略 |