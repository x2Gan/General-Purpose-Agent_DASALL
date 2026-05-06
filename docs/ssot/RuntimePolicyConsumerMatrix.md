# RuntimePolicyConsumerMatrix (Single Source of Truth)

关联任务：INT-TODO-006  
关联后续任务：INT-TODO-017  
关联来源：集成评审 §7.1；profiles 详设 6.5.1；runtime 详设 RT-C009

## 1. 目标

本文件冻结 `RuntimePolicySnapshot` 的系统级 consumer matrix，统一以下口径：

1. 哪些配置域的语义 owner 是 profiles。
2. 哪些模块是 primary consumer，消费方式是完整快照还是 typed projection。
3. deployment override / runtime override 的允许范围。
4. hot-reload 在系统级上的生效边界，避免各模块私自重解释 `runtime_budget`、`timeout_policy`、`degrade_policy`、`execution_policy`、`ops_policy`。

## 2. 全局 ownership 规则

### 2.1 semantic owner

1. `RuntimePolicySnapshot` 顶层域和键语义只由 profiles 定义。
2. consumer 文档只允许说明“如何消费/投影”，不允许重新定义同名键的业务含义。
3. 任一模块若需要新字段，必须先扩展 profiles schema / validator / matrix，而不是在本模块私有配置里隐式发明新的 profile 语义。

### 2.2 snapshot lifecycle owner

1. runtime 是完整 `RuntimePolicySnapshot` 的 lifecycle owner，负责加载、激活、generation、原子替换、LKG 回退与下游分发。
2. 除 runtime 以外的模块都不是完整快照 owner，只能消费经批准的 typed projection 或只读快照引用。
3. 没有任何 consumer 可以把派生值反写为新的 profile 语义。

### 2.3 override owner

1. profiles 定义 override 白名单与来源约束。
2. `ProfileOverlayComposer` / `RuntimePolicyProvider` 负责合并 `deployment_override` 与 `runtime_override`。
3. consumer 不得直接解析 YAML、HTTP 参数、CLI 环境变量或私有热更输入来绕过 override 管线。

## 3. override 与 hot-reload 总规则

### 3.1 override 白名单

1. `deployment_override` 允许覆盖 `runtime_budget.*`、`timeout_policy.*`、`ops_policy.*`、`model_profile.*.fallback_route`、`capability_cache_policy.*`、`degrade_policy.*` 等环境适配型策略域。
2. `runtime_override` 仅允许覆盖 `runtime_budget.*`、`timeout_policy.*`、`ops_policy.log_level`、`ops_policy.trace_sample_ratio`、`ops_policy.remote_diagnostics_enabled`、`capability_cache_policy.*` 等明确定义为运行期可调的键。
3. 两类 override 都禁止修改 `schema_version`、`profile_meta.*`、`enabled_modules.*`。
4. `execution_policy.requires_high_risk_confirmation`、`execution_policy.safe_mode_enabled`、`execution_policy.audit_level` 只允许等价收紧，不允许放宽。

### 3.2 hot-reload 生效规则

1. `enabled_modules.*` 与 build-time adapter 选择不支持 runtime hot-reload，只能通过 profile 基线 / deployment 级收口。
2. `runtime_budget.*`、`timeout_policy.*` 默认按“下一次新请求 / 下一轮 turn”生效，不允许 mid-turn 改写已经绑定的 deadline / counters。
3. `degrade_policy.*` 可以按 runtime 暴露的只读 atomic view 热生效，但只改变后续降级裁定，不回写正在执行中的历史结果。
4. `ops_policy.*` 中明确允许运行期调整的日志 / trace / 远程诊断键可热生效；其余 ops 键保持当前 active snapshot 到下一次激活。
5. 任一 consumer 若需要更细粒度 hot-reload，必须先在本矩阵中声明，而不是在实现里私自生效。

## 4. Consumer Matrix

| Consumer | 允许消费的域 / 键 | owner / projection | override / hot-reload 语义 | 禁止事项 |
|---|---|---|---|---|
| profiles | 全部顶层域 | semantic owner；`ProfileCatalog` / `ProfileOverlayComposer` / `RuntimePolicyProvider` | 定义 override 白名单、来源约束、validator 与 LKG 规则；自身不做业务执行裁定 | 不把运行时局部派生值倒灌回 schema 定义 |
| runtime | 全量快照；重点消费 `runtime_budget.*`、`timeout_policy.*`、`degrade_policy.*`、`execution_policy.*`、`ops_policy.*` | 完整快照 lifecycle owner；`RuntimeDependencySet` / `AtomicPtr<RuntimePolicySnapshot>` / runtime typed views | `runtime_budget` / `timeout_policy` 默认下一请求或下一 turn 生效；`degrade_policy` 与部分 `execution_policy` / `ops_policy` 标志可通过只读 atomic view 热读 | 不新增 schema v1 顶层治理域；不把 hot-reload 扩展为 mid-turn 计数器重写 |
| cognition | `model_profile.stage_routes`、与 cognition 相关的 `prompt_policy.*`、阶段级 `timeout_policy.*` 默认值、允许的 `degrade_policy.*` | `CognitionConfigProjector -> CognitionConfig`，请求期再由 `StagePolicyResolver` 形成 `StageExecutionPlan` | 只在初始化或新调用开始时消费；不在 stage 内重写 profile；hot-reload 影响下一次 decide / reflect / build_response 调用 | 不直接持有完整快照；不读取 YAML；不自行解释 runtime 预算计数器或恢复阈值 |
| llm | `model_profile.*`、`prompt_policy.*`、`timeout_policy.llm.*`、`degrade_policy.*`、与 llm 相关的 `ops_policy.*` | `LLMSubsystemConfig` | timeout / fallback / degrade 只影响新发起的 LLM 调用；不回写当前已发出的 provider 请求 | 不解释 tools/knowledge/memory 专属键；不把 provider 部署细节写成新 profile 语义 |
| tools | `enabled_modules.tools_builtin/tools_mcp/multi_agent`、`runtime_budget.max_tool_calls`、`prompt_policy.tool_visibility_rules`、`capability_cache_policy.*`、`timeout_policy.tool.*` / `timeout_policy.mcp.*` / `timeout_policy.workflow.*`、`execution_policy.*`、`ops_policy.*` | `ToolConfigAdapter -> ToolPolicyView / ToolTimeoutView` | hot-reload 仅影响新 ticket / 新 lane 选择；已发起的 tool/MCP/workflow ticket 继续按创建时视图执行 | 不自行扩写新的 tool policy schema；不从 `prompt_policy` 推导授权语义 |
| knowledge | `enabled_modules.knowledge`、`enabled_modules.memory_vector`、`token_budget_policy.*`、`capability_cache_policy.*`、`runtime_budget.max_latency_ms / worker_threads`、`degrade_policy.allow_budget_degrade` | `KnowledgeConfigProjector -> KnowledgeConfigSnapshot` | hot-reload 仅影响新检索请求、catalog freshness 与预算视图；不改写 in-flight retrieval | 不新增顶层 knowledge 域；不把 cache 策略重解释为 provider/MCP 私有策略 |
| memory | `enabled_modules.memory_vector`、`token_budget_policy.*`、`runtime_budget.max_latency_ms`、`degrade_policy.allow_budget_degrade` | `MemoryConfig + BudgetPolicy + request-level token_budget_hint` | 只影响新的 context assemble / writeback / recall 请求；storage / maintenance 本地默认值仍按 memory 本地配置，直到 profile 单独冻结对应键 | 不把 retention/checkpoint/WAL 等本地默认值擅自升级为 profile 语义 |
| infra / observability | `ops_policy.*`、override 来源约束、远程诊断相关开关 | infra module-local config views | `ops_policy.log_level`、`trace_sample_ratio`、`remote_diagnostics_enabled` 可热生效；其余键遵循 active snapshot 替换 | 不越权定义 runtime/cognition/tools/llm 的业务策略含义 |

## 5. 关键字段级规则

1. `runtime_budget.max_tool_calls` 的 semantic owner 是 profiles；runtime 负责全局扣减与拒绝，tools 只能把它投影为 lane / invoke budget view，cognition 不能直接消费原始计数。
2. `timeout_policy.*` 的 semantic owner 是 profiles；runtime 负责 session / step deadline 绑定，llm/tools/cognition 只消费各自子域 timeout 视图，hot-reload 不得回改已绑定 deadline。
3. `degrade_policy.*` 的 semantic owner 是 profiles；runtime / llm / knowledge / memory 只能按允许的 fallback/degrade 开关执行，不得发明新的 degrade 语义。
4. `execution_policy.*` 的 semantic owner 是 profiles；runtime / tools 负责执行时门禁，但 override 只能等价收紧，不能放宽高风险确认或 safe mode。
5. `ops_policy.*` 的 semantic owner 是 profiles；infra / observability 负责日志、指标、trace 与远程诊断投影，但不能据此修改业务决策。

## 6. 与相邻 SSOT 的分工

1. 本文件只解决 profile 键的 consumer / owner / override / hot-reload 语义。
2. 跨模块数据字段的 shared vs module-local 投影，继续由 `CrossModuleDataProjectionMatrix` 定义。
3. retry budget、idempotency、circuit、deadline 等恢复上下文边界，继续由 `RecoveryContextBoundary` 定义。

## 7. Design -> Build 映射

1. `INT-TODO-017` 负责将 runtime 执行点对 `RuntimePolicySnapshot` 与 `RecoveryContextBoundary` 的消费对齐到此矩阵。
2. profiles/runtime/cognition/tools/knowledge/memory/infra 后续若新增 projection tests，必须回链本矩阵而不是各写一套键语义。

## 8. 完成判定

当且仅当以下条件成立时，才允许将 `RuntimePolicySnapshot` consumer matrix 视为系统级 SSOT：

1. semantic owner、snapshot lifecycle owner、override owner 三类 ownership 已明确。
2. runtime、cognition、llm、tools、knowledge、memory、infra 的 allowed consumer 范围与 typed projection 已明确。
3. `runtime_budget`、`timeout_policy`、`degrade_policy`、`execution_policy`、`ops_policy` 的 override / hot-reload 规则已固定。
4. 不再允许多个模块私自重解释共享配置键。