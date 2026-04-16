# TOOL-TODO-012 ToolConfigAdapter 与 ToolPolicyView 派生设计收敛

日期：2026-04-16  
任务：TOOL-TODO-012  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-012 的验收条件要求：ToolConfigAdapter 必须实现 `build_policy_view()`、`build_timeout_view()`、`is_snapshot_current()`，并能对 desktop / edge profile 差异与 hot-update 的 invoke-scoped snapshot 一致性给出自动断言。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.6 明确 ToolConfigAdapter 是 projection-only 组件，只能把既有 `RuntimePolicySnapshot` 投影成 `ToolPolicyView`、`ToolTimeoutView` 和 lane 预算视图，不能变成新的 policy brain。
3. docs/architecture/DASALL_tools子系统详细设计.md 6.12.6 同时要求 projection 组件采用 per-snapshot-version 缓存与 invoke-scoped 一致性语义；当前 profiles 实际对象使用 `generation()` 表示快照版本，因此本轮以 `generation + effective_profile_id` 的 fingerprint 实现缓存命中与失效判断。
4. profiles/include/RuntimePolicySnapshot.h 只提供 tools 可消费的既有键：`runtime_budget.max_tool_calls`、`prompt_policy.tool_visibility_rules`、`capability_cache_policy`、`timeout_policy`、`execution_policy.allowed_tool_domains` 等；enabled_modules 不在 snapshot 内，因此 lane 开关必须从 `BuildProfileManifest` 的既有模块矩阵投影，而不是新增 tools 私有 schema。

## 2. Design 结论

1. `ToolPolicyView` 继续保持现有 public 形状，只承载 PolicyGate 直接需要的 profile id、safe mode、confirmation、audit level、allowed domains、visibility rules，不为 012 额外扩 public ABI。
2. lane 开关、timeout budget、capability cache freshness 策略统一收敛到内部 `ToolTimeoutView`：包含 tool / mcp / workflow 三类 timeout budget、`max_tool_calls`、builtin / mcp / multi-agent lane enablement、stale-read 与 cache refresh/backoff 配置。
3. `build_policy_view()` 与 `build_timeout_view()` 都采用 per-fingerprint 缓存；若 snapshot fingerprint 未变化，重复调用直接返回缓存投影，避免每次 invoke 重复解析 profile 视图。
4. `is_snapshot_current()` 只判断 invoke 入口绑定的 fingerprint 是否仍匹配当前 active snapshot，从而保证进行中的 tool invoke 使用入口时刻的一致视图；热更新只影响下一次 invoke。
5. 对 snapshot 或 manifest 不一致的输入，adapter 返回 deny-oriented 投影视图：policy 侧收敛为空 domain / visibility，timeout 侧收敛为零预算与全部 lane disabled，不做隐式放宽。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| ToolConfigAdapter 内部实现 | tools/src/config/ToolConfigAdapter.h；tools/src/config/ToolConfigAdapter.cpp |
| config projection 单测 | tests/unit/tools/ToolConfigAdapterTest.cpp；tests/unit/tools/ToolConfigAdapterHotUpdateTest.cpp |
| tools / tests include 接线 | tools/CMakeLists.txt；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolConfigAdapter 源文件，并移除 config placeholder 编译入口。
2. 测试目标：通过 profile diff 与 hot-update 两条 unit tests 覆盖 policy/timeout projection 差异、lane enablement、max_tool_calls、stale-read 与 snapshot fingerprint invalidation。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - RunCtest_CMakeTools 运行 `ToolConfigAdapterTest`、`ToolConfigAdapterHotUpdateTest`

## 5. 风险与回退

1. `enabled_modules` 当前通过 `BuildProfileManifest` 投影，这是因为 `RuntimePolicySnapshot` 本身不携带 lane 开关；后续如果 profiles 层为 active runtime 暴露了统一的模块可见视图，应优先切换到正式上游投影，而不是在 tools 内长期并存双来源。
2. 当前 fingerprint 以 `generation + effective_profile_id` 为准，满足现有 invoke-scoped 一致性要求；若后续 profiles 层引入更强版本向量，应直接复用上游版本语义而不是在 tools 内独立进化。
3. deny-oriented fallback 只意味着 projection 不放宽，不意味着 RouteSelector/PolicyGate 已经具备完整决策能力；后续 013、014 仍需把这些投影视图真正接入 gate 与 route 选择链。