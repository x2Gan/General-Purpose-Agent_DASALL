# TOOL-TODO-015b ToolManager 完整治理管线收敛

日期：2026-04-16  
任务：TOOL-TODO-015b  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.7 与 6.12.1 已冻结 ToolManager 主链顺序为 `resolve -> validate -> admit -> route -> execute -> project`，并要求 `invoke_batch()` 保持 request 级隔离与失败互不阻断。
2. TOOL-TODO-009 ~ 014 已分别落地 ToolRegistry、ToolValidator、ToolConfigAdapter、ToolPolicyGate、ToolRouteSelector；TOOL-TODO-015a 已提供可实例化的 ToolManager 骨架与 `invoke_batch()` 入口，因此 015b 的核心就是把这些既有组件真正串起来。
3. TOOL-TODO-023 已把 caller fixture 收敛为 `ToolRequest` + `ToolInvocationContext` + ToolManager 本地依赖三段式，因此 015b 可以在不接 runtime 生产主链的前提下，用 tests/design gate fixture 验证完整治理链。

## 2. Design 结论

1. `ToolManager::run_invoke_pipeline()` 现已按 `Registry -> Validator -> PolicyGate -> RouteSelector -> Executor -> Audit -> Digest` 串联完整主链：descriptor 缺失、validation 失败、policy deny、route unavailable 都在对应阶段 fail-closed，并保留稳定 reason code。
2. `ToolInvocationContext.caller_domain` 不再直接喂给 PolicyGate。ToolManager 在 validation 后基于 `ToolIR.route` / descriptor category 推导 requested execution domain（`builtin` / `workflow` / `mcp`），作为 module-local `ToolAdmissionRequest` 输入。
3. 在 `BuiltinExecutorLane` 与 `ResultProjector` 正式落地前，ToolManager 提供 module-local 默认 executor/projector 作为最小闭环：`DryRun` / `ValidateOnly` 不进入真实 executor，而是生成 non-executing `ToolResult`；成功路径统一补 Observation / ObservationDigest / route facts / evidence refs。
4. `invoke_batch()` 继续保持内部串行但 request 级隔离：单请求 deny 不阻断后续请求；executor 只会命中被 admission 和 route 同时放行的请求。
5. compensation 主链仍保持 stub；015b 只完成 invoke 主链，不越权提前实现 6.8/6.12.2 的补偿执行细节。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| ToolManager 主链串联 | tools/src/ToolManager.cpp |
| 默认 executor / projector 最小闭环 | tools/src/ToolManager.cpp |
| 单请求成功 / 失败路径 | tests/unit/tools/ToolManagerPipelineTest.cpp、tests/unit/tools/ToolManagerFailurePathTest.cpp |
| batch request 隔离 | tests/unit/tools/ToolManagerBatchInvokeTest.cpp |
| TODO / 交付证据回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/ToolManager.cpp
   - tests/unit/tools/ToolManagerSkeletonTest.cpp
   - tests/unit/tools/ToolManagerBatchInvokeTest.cpp
   - tests/unit/tools/ToolManagerPipelineTest.cpp
   - tests/unit/tools/ToolManagerFailurePathTest.cpp
2. 测试目标：
   - `ToolManagerSkeletonTest`
   - `ToolManagerBatchInvokeTest`
   - `ToolManagerPipelineTest`
   - `ToolManagerFailurePathTest`
   - full unit regression
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 5. 风险与回退

1. 默认 executor / projector 只是 015b 阶段的 module-local 最小闭环，占位真实 `BuiltinExecutorLane` / `ResultProjector`；后续 016、017、018 完成后应把这些默认 fallback 收敛为更薄的 wiring 层，而不是长期保留双轨实现。
2. 015b 仍未把 runtime 生产 caller adapter 接到 ToolManager，也未实现 compensation 主链；后续提交必须继续维持“invoke 主链已通、production caller / compensation 仍分阶段推进”的边界。