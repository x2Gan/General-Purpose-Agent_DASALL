# DASALL infrastructure 子系统评审结论与后续推进建议（2026-04-08）

最近更新时间：2026-04-08  
阶段：Review Summary -> Cross-Subsystem Coordination  
适用范围：infra/、tests/、scripts/ci/ 及与 platform/profiles/runtime 的协同接口  
当前结论：infrastructure 已形成可继续支撑其它子系统推进的稳定底座；剩余未完成项主要集中在 platform 桥接、profiles 键域冻结、ARC contract/gate 收口与少量 v2 外部依赖，不构成“其它子系统整体暂停”的前置条件。plugin 的真实 load/runtime bridge 属于 platform + plugin 联合任务，不需要等待 runtime 子系统整体完成后再回补。

## 1. 评审范围与方法

本次评审围绕以下三个问题展开：

1. infrastructure 子系统与各组件尚未完成的任务，是否真实依赖其它子系统推进。
2. 当前已交付的 infra 能力，是否足以继续支撑其它子系统推进。
3. 当前 Build 落地方案是否合理，覆盖是否存在明显缺口。

评审依据采用“文档台账 + 代码/测试/CMake 实体 + build-ci 实测”三线交叉核对：

1. 子系统专项 TODO 与各组件专项 TODO。
2. infra/include、infra/src、tests/unit、tests/contract、tests/integration、infra/CMakeLists.txt、tests/CMakeLists.txt、scripts/ci/infra_gate.sh。
3. build-ci 路径下的实际构建和 gate 执行结果。

## 2. 核心结论

### 2.1 交付状态结论

1. 不能得出“infra 全部任务和 Block 项已全部完成”的结论。
2. 可以得出“infra 主链已具备继续支撑其它子系统推进的工程底座”的结论。
3. 当前剩余问题更接近“协同收口项”和“v2 外部扩展项”，而不是“infra 主链不可用”。

### 2.2 代码与门禁复核结果

| 复核项 | 结果 | 说明 |
|---|---|---|
| `dasall_infra` 构建 | Pass | build-ci 下可稳定构建 |
| 仓库级 infra gate | Pass（审批窗口 `ALLOW_BLOCKED=1`） | 主链门禁可执行 |
| unit | 189/189 Pass | 基础对象、接口、主链骨架、失败路径均已进入回归面 |
| contract | 149/149 Pass | contracts 边界、错误码映射、对象边界整体稳定 |
| integration | 26/26 Pass | 已覆盖 config、diagnostics、health、logging、ota、policy、secret、tracing、watchdog、plugin 等主要组件 |
| failure | 14/14 Pass | 关键失败注入路径已具备基础门禁 |
| 覆盖缺口 1 | 未收口 | metrics 组件 integration/failure 仍未形成自身完整 gate，`MET-GATE-07` 仍为 Fail |
| 覆盖缺口 2 | 未收口 | tracing/metrics 的 planning stage ARC contract 与 gate 绑定仍未完成 |
| 覆盖缺口 3 | 未收口 | `infra_gate.sh` 当前只统计精确 `| Blocked |` 行，会低估残余 blocker 数量 |

### 2.3 依赖归属总判断

1. 真正偏“跨子系统硬前置”的剩余项，主要集中在 platform 与 profiles 两类。
2. 与 runtime 的关系，当前更多是“后续联调面”与“语义对齐面”，而不是“现在必须等待 runtime 主链完成”。
3. OTLP、KMS、持久化后端这类残余项，属于外部依赖或 v2 扩展，不应反向阻塞其它子系统继续推进。

## 3. 未完成项与跨子系统依赖归属

| 项目 | 当前状态 | 真实前置依赖 | 是否必须等待其它子系统整体推进 | 评审结论 |
|---|---|---|---|---|
| `PLG-BLK-04` / PluginRuntimeBridge 与真实装载链 | Open | platform 动态库装载抽象 + plugin 侧桥接契约 | 否 | 这是 platform + plugin 的联合冻结任务，不是“先等 runtime 全部完成”的任务。plugin 当前的 validation、lifecycle skeleton、failure injection 已不再受阻。 |
| `HLT-TODO-009` / ProbeScheduler | Blocked | health 对 `IThread` / `ITimer` 的消费回链 | 否 | platform 的 `IThread`、`ITimer` 已落盘并通过测试，当前问题已从“平台能力缺失”转成“health 尚未接入平台抽象”。 |
| `HLT-TODO-012` / HealthEventPublisher | Blocked | 最小事件发布接口冻结 | 不一定 | 若坚持统一全局 Event Bus，则需要 runtime 协同；但若采用最小 sink/adapter，则可以先在 infra 范围内收口，不必把 runtime Event Bus 变成停工前置。 |
| `HLT-TODO-014` / HealthConfigPolicy | Blocked | profiles 冻结 `infra.health.*` 键域与覆盖优先级 | 否 | 这是 profiles + health 的联合收口任务，不需要等待 runtime。 |
| metrics integration/failure 收口（`MET-GATE-07`） | Fail | metrics 自身 integration/failure 用例落盘与 CMake 聚合接线 | 否 | 主要是 infra/tests 自身收口问题，不是其它子系统的硬前置。 |
| `MET-TODO-021` / `MET-TODO-022` | Not Started | metrics 自身 contract/gate 任务；planning stage 语义与上层对齐 | 否 | 可以先在 infra 侧完成 contract/gate，再由 runtime/cognition 等上层消费这些约束。 |
| `TRC-TODO-020` / `TRC-TODO-021` | Not Started | tracing 自身 contract/gate 任务；planning stage 语义与上层对齐 | 否 | 与 metrics 的 ARC 收口逻辑一致，不需要等待 runtime 主链开发完成。 |
| `TRC-BLK-004` / OTLP exporter | Open | third_party 依赖策略与部署策略冻结 | 否 | 这是外部依赖问题，不是其它子系统前置；当前 `noop/file` 已足以支撑其它子系统接入 tracing。 |
| `CFG-BLK-003` / SecretRefResolver | Open | config + secret 联合冻结 `secret://` 解析契约 | 否 | 需要 config 与 secret 协同，但 secret 主链已足够稳定，可直接进入 resolver 契约收口。 |
| `CFG-BLK-005` / 快照持久化后端 | Open | config 内部持久化策略；可选消费 platform 文件能力 | 否 | 属于 config v2 扩展，不应阻塞其它子系统继续依赖现有 config 主链。 |
| `SEC-BLK-003` / KMS 真实接入 | Blocked | 外部 KMS 身份、限流、超时、测试夹具冻结 | 否 | 这是外部集成项，不是 infra 内部跨子系统前置；file/mock 主链已足以支撑上层继续开发。 |

### 3.1 对“plugin runtime 桥接是否要等 runtime 子系统”的明确回答

结论：不需要。

原因如下：

1. PluginRuntimeBridge 在架构和 plugin 详细设计中的职责，本质上是“隔离平台动态装载细节”，归属在 platform + plugin 的桥接层，而不是 runtime 主控层。
2. 当前 plugin 生命周期骨架已经通过可注入 callback / mock bridge 绕开了真实平台装载实现，说明 plugin 当前继续推进并不依赖 runtime 完整体。
3. runtime 真正需要介入的，是后续“插件被主控启停、调度、回退”的 end-to-end 联调阶段，而不是桥接接口本身的冻结阶段。

因此，推荐口径应改为：

1. 先由 platform + plugin 联合冻结 PluginRuntimeBridge 最小契约。
2. 再由 runtime 在后续联调中消费该桥接能力。

## 4. 当前 infra 是否足以继续支撑其它子系统推进

### 4.1 总结判断

结论：足以。

但这个“足以”指的是：

1. 足以支撑其它子系统继续推进接口接入、主链实现、单元测试、契约测试和局部集成。
2. 不等于已经足以宣称“所有跨子系统 end-to-end 联调项全部收口”。

### 4.2 已可直接复用的 infra 能力

当前其它子系统已经可以稳定依赖以下能力：

1. config：IConfigCenter、四层合并、运行时 patch、回滚和基础 observability。
2. logging / audit：结构化日志、审计写入、fallback 和基础导出边界。
3. secret：ISecretManager、file/mock backend 主链、访问控制、轮换、审计、健康。
4. policy：策略对象、快照、patch、decision 边界和最小治理主链。
5. diagnostics：快照、导出、白名单命令、redaction、metrics/audit bridge 骨架。
6. health：registry / executor / evaluator / recovery hint / wiring smoke 主链。
7. metrics：provider / meter / aggregation / exporter / recovery / bridge 骨架。
8. tracing：provider / tracer / span / propagation / sampling / buffer / exporter / bridge reachability 主链。
9. plugin：manifest / signature / compatibility / validation pipeline / lifecycle skeleton / failure injection。
10. CMake 与测试拓扑：unit / contract / integration / failure 的基础门禁图和仓库级 gate。

### 4.3 对其它子系统的实际支撑边界

| 下游方向 | 当前是否可继续推进 | 已可直接依赖的 infra 能力 | 仍需协同补齐的面 |
|---|---|---|---|
| services / llm / tools / memory / knowledge / cognition | 可以 | config、logging、audit、secret、policy、diagnostics、metrics、tracing、health 基础能力 | 若要把 planning 阶段预算与延迟纳入严格 contract，需要后续补 `MET-TODO-021`、`TRC-TODO-020` 与 `INF-TODO-020` |
| runtime 主控 / 恢复链 | 可以 | health 事实、policy 决策、diagnostics 快照、audit 证据、tracing/metrics 可观测底座 | plugin 真实装载桥接、event bus 统一策略、planning-stage observability contract 仍需协同收口 |
| apps / multi_agent | 可以 | config、profile、logging/audit、tracing/metrics、secret、policy 的稳定边界 | 如需真实动态插件装载、全局事件总线统一、OTLP/KMS 等扩展能力，需后续按专项任务补齐 |

### 4.4 不建议的判断口径

当前不建议采用以下判断：

1. “PLG-BLK-04 没完成，所以 runtime 不能继续推进。”
2. “OTLP 没冻结，所以 tracing/metrics 还不能给上层使用。”
3. “KMS 没接，所以 secret 还不能作为其它子系统依赖。”

这三种判断都会把“扩展项未收口”误判成“主链不可用”。

## 5. Build 方案与覆盖性复核

### 5.1 当前方案是否合理

结论：总体合理，且已经具备继续迭代的工程基础。

主要理由：

1. build-ci 路径已经能稳定承担仓库级复核职责。
2. `dasall_infra`、unit、contract、integration、failure 的分层门禁已经存在。
3. 多数组件已经把代码、测试和 CMake 接线收口到同一张构建图中。
4. 仓库级 `infra_gate.sh` 已把“三件套检查 + 分类测试执行”纳入统一入口。

### 5.2 当前方案仍有的缺口

| 项目 | 当前结论 | 具体问题 |
|---|---|---|
| metrics integration 覆盖 | 不足 | `tests/integration/infra/` 当前无 metrics 子拓扑，导致 `MET-GATE-07` 仍为 Fail |
| ARC contract 覆盖 | 不足 | tracing / metrics 的 planning stage、budget、delay 约束还未补到 contract/gate |
| gate 诚实性 | 不足 | `infra_gate.sh` 只统计精确 `| Blocked |`，对 `BLOCKED`、`Open`、blocker 表残余项不敏感 |
| 文档 SSOT 一致性 | 不足 | 子系统总 TODO 与组件 TODO 之间仍有状态漂移，容易让“已实现”与“已收口”混淆 |

### 5.3 Build 方案下一步修正建议

1. 保持 build-ci 为当前评审标准路径，直到 IDE/CMake Tools 侧 target/test discoverability 恢复稳定。
2. 为 metrics 增补 integration/failure 子拓扑，把 `MET-GATE-07` 从显式 Fail 推进到可验证 Pass。
3. 扩展 `infra_gate.sh` 的 blocker 检测逻辑，至少覆盖 `Blocked`、`BLOCKED`、`Open`、blocker 表残余项。
4. 在子系统 TODO 中增加定期“总表对组件表回链校准”的动作，避免总表长期滞后于组件实际交付状态。

## 6. 下一步推进建议

### 6.1 P0：优先做的收口项

1. platform + plugin：冻结 PluginRuntimeBridge 最小契约，明确 `load_symbol` / `unload` / handle 生命周期 / sandbox hint 的 v1 边界；不要等待 runtime 主链完成后再启动。
2. profiles + health / metrics / watchdog：冻结 `infra.health.*`、`infra.metrics.*`、`infra.watchdog.*` 键域与覆盖优先级，消掉配置类残余 blocker。
3. metrics + tracing：补齐 `MET-TODO-021`、`MET-TODO-022`、`TRC-TODO-020`、`TRC-TODO-021`，并与 `INF-TODO-020` 一起完成 ARC contract/gate 收口。
4. metrics：补齐 integration/failure 用例和 CMake 聚合接线，消掉 `MET-GATE-07`。

### 6.2 P1：与其它子系统开发并行补齐的协同项

1. health：基于现有 `IThread` / `ITimer` 回补 `HLT-TODO-009`，不再把它视为“等待 platform 从 0 到 1”的 blocker。
2. health：对 `HLT-TODO-012` 优先采用最小 sink/adapter 方案；只有在确定统一全局 Event Bus 方案时，才与 runtime 联动收口。
3. config + secret：优先定义 `secret://` 解析契约，补齐 `CFG-BLK-003`，再决定是否进入 config v2。
4. 文档与 gate：同步修正 infra 子系统总 TODO 的残余 open 状态，并增强仓库级 gate 的 blocker 识别诚实性。

### 6.3 P2：建议明确为 v2/外部扩展项

以下事项建议继续明确归为 v2 或外部扩展项，不要反向阻塞上层子系统当前推进：

1. `SEC-BLK-003`：KMS 真实接入。
2. `CFG-BLK-005`：config 持久化快照后端。
3. `MET-BLK-005` / `TRC-BLK-004`：OTLP exporter 与 collector 级联动。

## 7. 最终判断口径

建议本轮评审后的统一口径如下：

1. infrastructure 不是“全部收口完成”，但已经“足以继续支撑其它子系统推进”。
2. 剩余项中，最需要跨子系统协同的是 platform + plugin、profiles + health/metrics/watchdog，以及 runtime/cognition + tracing/metrics 的 ARC 语义对齐。
3. plugin 的真实 runtime bridge 不应表述为“等待 runtime 子系统推进后再补”，而应表述为“platform + plugin 先冻结桥接，再由 runtime 做后续联调消费”。
4. 在后续推进中，应把“主链可用”和“扩展项未收口”严格区分，避免以残余 v2 事项反向否定 infra 当前对其它子系统的支撑能力。