# TOOL-TODO-013 ToolPolicyGate 设计收敛

日期：2026-04-16  
任务：TOOL-TODO-013  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-013 的验收条件要求：ToolPolicyGate 必须实现 `evaluate()`、`check_allowed_domain()`、`check_visibility()`、`check_confirmation()`，并对 PolicyDenied、缺 profile、缺 confirmation、safe mode 拒绝给出自动断言。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 与 6.12.6 明确 PolicyGate 只能消费 validator / config adapter 已经产出的轻量视图，且在缺关键输入时必须 fail-closed，不能替 runtime 做恢复裁定或 route 选择。
3. TOOL-TODO-012 已提供稳定 `ToolPolicyView`，因此 013 的职责边界只包括 allowed domains、visibility rules、high-risk confirmation 与 safe-mode route proof 的准入裁定，不再重新读取 profile snapshot。
4. public interface `tools/include/IPolicyGate.h` 已冻结 `ToolAdmissionRequest` / `ToolAdmissionDecision` 形状，因此本轮在不扩 shared contract 与 public ABI 的前提下完成真实 gate 实现。

## 2. Design 结论

1. ToolPolicyGate 采用严格 fail-closed 顺序：先验证 policy view 是否完整，再检查 safe mode route proof、allowed domain、visibility、confirmation，任一步失败立即返回 deny decision。
2. 缺 profile 的判定保持保守：`effective_profile_id`、`audit_level`、`allowed_tool_domains`、`tool_visibility_rules` 任一为空，都返回 `policy.profile_missing`，避免拿半残 profile 继续准入。
3. safe mode 与 route proof 分离：当 `safe_mode_enabled == true` 且 request 未提供 `route_proven` 时，gate 直接返回 `policy.safe_mode_route_unproven`，并标记 retryable，表示后续只能在更完整的 route 证据出现后重试。
4. visibility 规则保持最小语义集：`domain:all` 全开，`domain:trusted` 只允许 route 已证明的请求，其余 selector 只能通过 `tool_name` 或 `required_scopes` 显式命中，不做模糊匹配。
5. confirmation 只负责“是否缺确认”这一 admission 事实；当 high-risk 请求缺确认时返回 `policy.confirmation_required`，设置 `confirmation_required=true` 和 `retryable=true`，但不在 tools 内直接触发交互流程。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| ToolPolicyGate 内部实现 | tools/src/policy/ToolPolicyGate.h；tools/src/policy/ToolPolicyGate.cpp |
| fail-closed gate 单测 | tests/unit/tools/ToolPolicyGateTest.cpp |
| profile-diff gate 单测 | tests/unit/tools/ToolPolicyProfileDiffTest.cpp |
| tools unit discoverability 接线 | tools/CMakeLists.txt；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolPolicyGate 源文件，并移除 policy placeholder 编译入口。
2. 测试目标：通过 gate 行为测试覆盖缺 profile、缺 confirmation、safe mode reject、domain/visibility deny，并通过 profile-diff 测试证明 012 的 profile projection 会改变准入结果。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - RunCtest_CMakeTools 运行 `ToolPolicyGateTest`、`ToolPolicyProfileDiffTest`

## 5. 风险与回退

1. 当前 gate 只根据 `ToolAdmissionRequest` 里的 `caller_domain`、`required_scopes`、`route_proven` 做裁定；如果后续 RouteSelector 或 runtime caller fixture 提供了更正式的 route proof / scope 证据对象，应优先替换为正式上游输入，而不是在 gate 内继续扩解释逻辑。
2. `domain:trusted` 目前等价为“必须 route proven”，这是为 013 提供最小可信度语义；后续如果 profiles/route 层引入更细的 trust level，应在 RouteSelector 或 CapabilityDiscovery 提供更明确事实，而不是让 gate 自行猜测。
3. 当前 gate 只输出 deny/allow 与 reason code，不做审计和 metrics；后续 ToolManager 主链接入时必须保留这些 reason code，避免在上层重新映射导致 gate 事实漂移。