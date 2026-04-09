# CAP-TODO-014 AdapterReceipt 与结果映射契约设计收敛

日期：2026-04-09  
任务：CAP-TODO-014  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 已明确 `AdapterBridge` 的输出是 `AdapterReceipt`、`ResultMapper` 的输入是 `AdapterReceipt + compensation hints`，但在本轮之前缺少 receipt 字段和 mapper 约束的成表定义。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 已明确 `AdapterReceipt` 不进入 contracts，只能作为 `ErrorInfo`、`ToolResult` 与 `Observation` 的上游事实来源，因此本轮必须补齐“只记录事实、不重定义 ErrorInfo”的字段边界。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8 已给出 `ServiceErrorClass` 九分类与 `PartialSideEffect` 的 evidence 需求，但仍需要把 `ErrorInfo.failure_type`、`source_ref`、`details` 与公共 result 的关系冻结到可执行粒度。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 9.4 现已加入 Receipt Mapping Gate，要求 receipt 字段、错误映射和 `side_effects` / `compensation_hints` 约束在进入 D1 前全部冻结。

## 2. 外部参考

1. Azure Architecture Center 的 Compensating Transaction pattern 强调系统必须记录每个已完成步骤的 undo 信息，并把补偿步骤设计成幂等命令；这支持本任务要求 `PartialSideEffect` 必须保留 `evidence_refs`、`side_effects` 与可重复执行的 `compensation_hints`，而不是只返回笼统失败。
2. 同一模式还强调补偿本身也可能失败，系统必须保留足够上下文以便恢复或人工介入；这支持本任务把主 evidence ref 固定写入 `ErrorInfo.source_ref`，并把剩余 evidence refs 进入 `ErrorInfo.details`。
3. OWASP Authorization Cheat Sheet 强调 deny by default、fail safely 和 appropriate logging；这支持本任务把非 `PartialSideEffect` 错误固定为“不得伪造 side_effects / compensation_hints”，并要求错误与证据输出采用一致、可审计的字段口径。

## 3. Design 结论

1. `AdapterReceipt` 保持 internal-only；它只记录 `receipt_ref`、`adapter_id`、`route_kind`、`target_id`、`transport_outcome`、`provider_status_code`、`payload_json`、`latency_ms`、`side_effects`、`evidence_refs` 等 receipt facts，不直接携带 `ErrorInfo`、`PolicyDecision` 或补偿执行结果。
2. `ResultMapper` 是唯一允许把 `AdapterReceipt` 归类为 `ServiceErrorClass` 并映射到 `ErrorInfo.failure_type` 的组件；Execution/Data lanes 不得自行重写 `ErrorInfo` 语义。
3. `ExecutionCommandResult` 是唯一允许携带非空 `side_effects` 与 `compensation_hints` 的公共 result；query / diagnose / data / subscription / catalog 路径只能通过 `ErrorInfo` 报告失败事实。
4. 仅当 `ServiceErrorClass=PartialSideEffect` 或显式 `compensate()` 返回可执行建议时，`ResultMapper` 才能输出 `compensation_hints`；验证、策略、route 与 adapter 可达性错误不得捏造补偿建议。
5. 若 `side_effects` 非空，则 `evidence_refs` 必须至少提供 1 个 durable ref；`ErrorInfo.source_ref` 使用主 evidence ref 或 `receipt_ref`，其余 evidence refs 进入 `ErrorInfo.details`。
6. `PartialSideEffect` 以外的错误必须保持 `side_effects` 与 `compensation_hints` 为空，防止把超时、不可达或权限失败伪装成部分成功。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `AdapterReceipt` 字段 | AdapterBridge 的 receipt 组装与 unit fixture |
| 冻结 `ServiceErrorClass -> ErrorInfo.failure_type` 映射 | ResultMapper 的分类分支与 contract 回归 |
| 固定 `evidence_refs` / `source_ref` 关系 | ExecutionCommandLane、ResultMapper 与 failure integration 的 partial side effect 断言 |
| 固定 `side_effects` / `compensation_hints` 约束 | ExecutionCommandLane / ResultMapper 的公共 result 组装逻辑 |
| 增加 Receipt Mapping Gate | CAP-TODO-014、036、040 的设计评审与后续 unit 验证入口 |

## 5. Build 三件套

1. 代码目标：更新 docs/architecture/DASALL_capability_services子系统详细设计.md，补齐 `AdapterReceipt` 字段、`ServiceErrorClass -> ErrorInfo.failure_type` 映射、`ErrorInfo.source_ref` / `details` 规则，以及 `side_effects` / `compensation_hints` / `evidence_refs` 约束；同步回写 services 专项 TODO。
2. 测试目标：以文档审阅和关键字校验确认 `AdapterReceipt`、`ServiceErrorClass`、`ErrorInfo`、`side_effects`、`compensation_hints` 与 Receipt Mapping Gate 已回链到 6.3 / 6.5 / 6.8 / 9.4。
3. 验收命令：
   - `rg -n "AdapterReceipt|ServiceErrorClass|ErrorInfo|side_effects|compensation_hints" docs/architecture/DASALL_capability_services子系统详细设计.md`

## 6. 风险与回退

1. `AdapterReceipt`、`evidence_refs` 与 mapper 细节保持 internal-only；在 D1 代码落盘前，不应把这些字段提前暴露为 public ABI 或 shared contracts。
2. 若后续新增 `ServiceErrorClass`、provider transport outcome 或 partial side effect 类型，必须先回写本表与 Receipt Mapping Gate，再修改 ResultMapper / failure integration。
3. 若补偿语义未来引入更强的 saga / recovery 编排，也必须保持 services 只输出事实和 hints，不越权代替 Runtime / RecoveryManager 做最终裁定。