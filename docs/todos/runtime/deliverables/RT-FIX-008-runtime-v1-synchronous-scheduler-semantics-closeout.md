# RT-FIX-008 runtime v1 synchronous scheduler semantics closeout

来源任务：RT-FIX-008
完成日期：2026-05-21
关联缺口：RT-GAP-007
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/todos/runtime/deliverables/RT-TODO-019-Scheduler设计收敛.md`、`docs/todos/runtime/deliverables/RT-FIX-005-runtime-production-observability-health-closeout.md`

## 1. 任务边界

1. 本轮不实现真正后台 worker pool、maintenance worker 或 recovery thread，只把 runtime 当前真实存在的 v1 同步执行模型显式固定出来。
2. authoritative 问题定义固定为：当前 `Scheduler` 已有 queue / backpressure surface，但 runtime 主链仍以单 worker inline dispatch 执行；如果不把这点做成可审计事实，total ledger 会持续误把类型存在感外推成后台线程模型已经就位。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree focused unit / integration tests 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| orchestrator result owner | `runtime/src/AgentOrchestrator.cpp` 现在会在统一 `make_result(...)` 出口追加 `runtime_execution_model:v1_sync_inline` 与 `runtime_scheduler_effective_max_workers:1` | direct path、tool round、recovery round 现在都会显式声明 v1 同步 runtime 语义，而不是靠阅读实现猜测 |
| checkpoint owner | `runtime/src/AgentOrchestrator.cpp` 现在会在 `build_and_save_checkpoint(...)` 统一追加 `runtime_execution_model=v1_sync_inline` 与 `runtime_scheduler_effective_max_workers=1` | checkpoint / resume 证据不会再把 queue surface 误写成后台 worker 已存在 |
| orchestrator regression | `tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp` 现在锁定 direct path 与 tool round failure-safe path 都带有同步执行模型标签 | runtime owner 层面不再允许 direct/tool 两条路径对 worker 模型给出不同口径 |
| facade regression | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` 现在锁定 `AgentFacade.handle()` 会把同步执行模型标签透传到最终 `AgentResult` | facade 出口与 orchestrator owner 口径保持一致，不会在对外 surface 丢失这一事实 |

## 3. 设计结论

1. 当前 runtime 的 `Scheduler` 是同步 gate，不是后台线程池：tool round 会 enqueue、acquire 单个 lease、在同一调用栈内完成 dispatch，再 release；direct LLM path 则保持 inline，不进入 scheduler。
2. 因为实际执行时仍固定使用 `WorkerLeaseBudget{max_workers=1, busy_workers=0}`，且 profile `worker_threads` 还没有驱动真实 worker pool，所以 v1 的权威口径必须是 `v1_sync_inline`，而不是“已支持后台 worker”。
3. backpressure / maintenance / recovery 的现有 surface 继续保留为治理与证据接口，但在真正 async/bulkhead 进入主链之前，不能把这些 surface 外推为 background worker 语义已完成。
4. 若未来要建立真正 worker/bulkhead 执行模型，应另立新任务，让 `worker_threads`、maintenance dispatch 与 recovery thread 进入实际调度路径；本轮不越权宣称这些能力已经交付。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 为 `AgentResult` 显式固定 v1 同步执行模型 | `runtime/src/AgentOrchestrator.cpp` |
| 为 checkpoint / resume 证据显式固定相同语义 | `runtime/src/AgentOrchestrator.cpp` |
| 锁定 direct path 与 tool round 的同步模型标签 | `tests/unit/runtime/AgentOrchestratorControllerAssemblyTest.cpp` |
| 锁定 facade 对同步模型标签的透传 | `tests/integration/agent_loop/RuntimeUnaryFixtureIntegrationTest.cpp` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-008` / `RT-GAP-007`。
2. 本轮不扩张到真正后台 worker / bulkhead 执行模型，也不把当前结果外推为 maintenance worker、recovery thread 或更高层 release-ready 已完成。
3. 本轮不使用 qemu / kvm；runtime_support installed positive knowledge probe 与 release-runner / soak evidence 继续留给后续任务。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_runtime_agent_orchestrator_controller_assembly_unit_test dasall_runtime_unary_fixture_integration_test dasall_runtime_unary_integration_test`：通过。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(AgentOrchestratorControllerAssemblyTest|RuntimeUnaryFixtureIntegrationTest)$'`：通过。

## 7. 完成判定

1. `RT-GAP-007` 已在当前树关闭。
2. runtime owner 现在会稳定把当前执行模型写成 `v1_sync_inline + effective_max_workers=1`，scheduler 不再被误读为后台 worker 已就位。
3. runtime 章节剩余优先级已收敛为 runtime_support 的 installed positive knowledge probe，再继续推进 packaging / release 环境中的更高层 runtime evidence。