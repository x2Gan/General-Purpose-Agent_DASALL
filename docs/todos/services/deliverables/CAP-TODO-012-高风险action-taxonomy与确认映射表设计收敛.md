# CAP-TODO-012 高风险 action taxonomy 与确认映射表设计收敛

日期：2026-04-09  
任务：CAP-TODO-012  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.1.4、6.1.5 已明确 Tool Policy Gate 是权限、风险等级与确认门控的唯一 owner，services 只能做 action class、caller domain 与 proof 的 recheck，不能放宽上游治理结论。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6、6.6.1 已明确 `safe_mode.enter` / `safe_mode.exit` 必须经 `IExecutionService.execute` 的受限 action taxonomy 到达，不能膨胀为新的顶层接口。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8 已把 `PolicyDenied` 归因到 `require_confirmation` 未满足或 caller domain / proof 与 action class 不一致，说明命令链必须在实现前冻结确认映射与 recheck 规则。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.9.1 已给出 `execution_policy.requires_high_risk_confirmation`、`execution_policy.safe_mode_enabled`、`execution_policy.allowed_tool_domains` 到 `ServicePolicyView` 的既有消费关系，确认 services 只能复用而不能自造新的 policy 逻辑域。
5. docs/architecture/DASALL_capability_services子系统详细设计.md 8.3 与 9.4 把“高风险动作语义清单未评审”列为 Phase 2 / 4 blocker，并要求后续高风险动作请求与结果具备 audit 证据，因此本轮必须把 action taxonomy、确认要求和 audit gate 一起落表。

## 2. 外部参考

1. OWASP Authorization Cheat Sheet 强调 least privilege、deny by default 与 validate permissions on every request；这支持本任务把高风险动作的 `caller_domain` recheck 固定为“复用 `execution_policy.allowed_tool_domains`，且逐请求复核与上游决策一致”，而不是依赖单点默认放行。
2. OWASP Authentication Cheat Sheet 明确要求对敏感操作做 re-authentication / step-up verification，并建议把高风险动作与 proof freshness、context-aware decisions 绑定；这支持本任务把 require_confirmation 动作集合显式成表，并要求 proof 必须绑定同一 action class、target 与 caller domain。

## 3. Design 结论

1. V1 不新增 confirmation proof 公共 ABI 字段；Tool Policy Gate 的决策与 proof 保持 internal-only sideband，services 只消费并 recheck，不反向扩张 ServiceTypes。
2. `require_confirmation` 动作集合固定为：`safe_mode.enter`、`safe_mode.exit`，以及 capability snapshot 明确标记 `requires_confirmation=true` 或 `risk_tier=high` 的副作用动作。
3. `safe_mode.enter` / `safe_mode.exit` 属于固定高风险 action，不允许通过新的顶层接口暴露，也不允许在语义不等价 fallback 上自动降级执行。
4. services 对高风险动作只做三类 recheck：action class 一致性、caller domain 与 allowlist 一致性、proof 的绑定关系与新鲜度；任一条件失败都统一归为 `PolicyDenied`。
5. 所有 require_confirmation 动作都必须能够把 request/result 关联到 `decision_ref` 与 action class；缺失 audit 关联时，Policy Alignment Gate 或 Ops Gate 直接失败。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结高风险 action taxonomy 与确认映射 | ExecutionCommandLane 的 action class 判定与拒绝分支 |
| 冻结 caller_domain / proof recheck 规则 | ServiceFacade / ExecutionCommandLane 的 admission recheck 入口 |
| 固定 `safe_mode.enter` / `safe_mode.exit` 走 `execute` | 不新增 `set_safe_mode()` 等顶层接口，保持 IExecutionService ABI 不扩张 |
| 将 audit 关联纳入 gate | ServiceAuditBridge、CapabilityServices smoke / failure integration 的审计断言 |

## 5. Build 三件套

1. 代码目标：更新 docs/architecture/DASALL_capability_services子系统详细设计.md，补齐 6.6.1 的高风险 action taxonomy 与确认 / recheck 映射表，并在 9.4 新增 Policy Alignment Gate。
2. 测试目标：以文档审阅和关键字校验确认 `safe_mode.enter` / `safe_mode.exit`、`require_confirmation` 动作集合、`allowed_tool_domains` 与 audit gate 已回链到 6.6.1 / 9.4 与上位架构 5.5.2 / 5.5.3。
3. 验收命令：
   - `rg -n "safe_mode|require_confirmation|allowed_tool_domains" docs/architecture/DASALL_capability_services子系统详细设计.md docs/architecture/DASSALL_Agent_architecture.md docs/architecture/DASALL_Engineering_Blueprint.md`

## 6. 风险与回退

1. confirmation proof 仍保持 internal-only；在 route / receipt supporting objects 未冻结前，不应把 proof 结构提前升格为 ServiceTypes 公共字段。
2. 若后续新增高风险 action family，必须先回写本表并与 Tool Policy Gate、ServiceAuditBridge 的审计口径一起评审，不能直接在命令车道中“顺手添加”。
3. 若 profile / policy 未来修改 `execution_policy.allowed_tool_domains` 或 `requires_high_risk_confirmation` 语义，必须同步复核本表与 9.4 的 Policy Alignment Gate，避免 design 与 policy 漂移。