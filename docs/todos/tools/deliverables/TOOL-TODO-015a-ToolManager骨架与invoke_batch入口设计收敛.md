# TOOL-TODO-015a ToolManager 骨架与 invoke_batch 入口设计收敛

日期：2026-04-16  
任务：TOOL-TODO-015a  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-015a 的验收条件要求：ToolManager 必须可实例化，`invoke()`、`invoke_batch()`、`compensate()` 入口可编译，且 `ToolManagerBatchInvokeTest.cpp` 能证明 request 级隔离签名成立。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.12.1 明确 ToolManager 是 tools 子系统唯一公共入口，本轮只需要落地入口骨架与批量入口，不提前接入 015b 才负责的完整治理链和 runtime 生产主链。
3. 同一设计文档 6.12.1 同时规定：`invoke_batch()` 首版允许内部串行迭代调用单请求入口，但必须保持每个 request 独立产出 envelope，且单请求失败不影响同批其他请求。
4. 当前 016~022 的 executor / projector / observability 组件尚未落地，因此 015a 只能收敛 module-local 内部依赖注入点与 fail-closed stub，不能提前伪造完整执行链能力。

## 2. Design 结论

1. `ToolManager` 采用“public 接口 + src 内部实现头”模式实现，具体类与 `CompensationRequest`、`ToolExecutionContext`、executor/projector/audit hooks 等 supporting object 保持在 tools/src 内部，不回灌到 public ABI 或 shared contracts。
2. `invoke_batch()` 首版直接复用 `invoke()` 串行迭代，但必须保留输入顺序、一请求一 envelope、request_id / tool_call_id 不串扰三条最小语义，以便后续 015b 和 executor lane 真正接入时不改 public 行为。
3. 在完整治理链尚未接通前，ToolManager 骨架必须 fail-closed，统一返回 `tool.manager.pipeline_unconfigured` 与 `tool.manager.compensation_unconfigured` 两个 stub reason code，而不是静默成功或返回空结果。
4. `BuildProfileManifest` 作为 ToolConfigAdapter 所需的 build-time lane enablement 视图，先由 ToolManager 内部依赖注入持有，不塞进 `ToolInvocationContext`，避免在 023 之前扩张 runtime caller surface。

## 3. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| ToolManager 内部实现头与 supporting object | tools/src/ToolManager.h |
| ToolManager 骨架实现与 fail-closed stub | tools/src/ToolManager.cpp |
| ToolManager 可实例化 / 入口骨架单测 | tests/unit/tools/ToolManagerSkeletonTest.cpp |
| ToolManager batch 隔离单测 | tests/unit/tools/ToolManagerBatchInvokeTest.cpp |
| tools/unit 测试注册 | tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |

## 4. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 ToolManager 内部实现头与骨架实现，具备默认依赖、fail-closed stub、`invoke_batch()` 串行入口与 compensation 入口。
2. 测试目标：通过 `ToolManagerSkeletonTest` 和 `ToolManagerBatchInvokeTest` 覆盖可实例化、fail-closed stub、batch 输入顺序与 request/tool_call identity 隔离。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_tools dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -R "ToolManager(Skeleton|BatchInvoke)Test"`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

## 5. 风险与回退

1. 当前 ToolManager 只把 executor / projector / audit 定义为 module-local 注入点，还没有接入 015b 的完整治理管线；后续必须在不改变 015a 批量入口语义的前提下把 Registry -> Validator -> PolicyGate -> RouteSelector -> Executor -> Audit -> Digest 串起来。
2. `tool.manager.pipeline_unconfigured` 与 `tool.manager.compensation_unconfigured` 只用于 015a 的 fail-closed 骨架，不应被后续完整治理链长期保留为最终产品 reason code。
3. `BuildProfileManifest` 目前由 ToolManager 依赖注入持有；023 回写 caller fixture 文档时需要明确这是 tests/fixture 范围内的最小收敛，而不是 runtime 生产上下文已经稳定冻结。