# FULLINT-TODO-011 async / cancel / replay / recovery 因果链证据包

日期：2026-05-11  
任务：`FULLINT-TODO-011`  
阶段：System Integration -> Full Business Chain Verification  
状态：Done

## 1. 任务边界

本轮只推进 `FULLINT-TODO-011`，不合并 `FULLINT-TODO-012` 的知识/LLM/工具服务跨链回归，也不提前推进 `FULLINT-TODO-013` 的 fresh install package matrix。

验证目标不是复述既有 focused test 已完成，而是在当前真实代码上建立一条新的可二值判定链：同一 `request_id/session_id/trace_id` 必须能跨 Access async receipt、ownership、cancel forwarding、ResultReplayCache、Runtime recovery admission / resume plan、Memory writeback 持久化连续可追踪。

## 2. 本地证据

1. `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`：`FULLINT-TODO-011` 前置为 `006`，关联阻塞项为无，完成判定要求 receipt/cancel/replay 不只返回 envelope，还保留 ownership、trace/ref、失败语义和写回连续性。
2. `access/src/AsyncTaskRegistry.*`：`register_async_accept()` 为 `AcceptedAsync` 结果生成 `AsyncTaskReceipt`，保留 `request_id`、`session_id`、`actor_ref`、`task_ref`、`ownership_token` 与 TTL。
3. `access/src/ResultReplayCache.*`：bounded LRU + TTL，可缓存 `PublishEnvelope`，其中包含 `request_id/session_id/trace_id` 与可选 `AsyncTaskReceipt`。
4. `access/src/RuntimeBridge.*`：`dispatch()` 对 `AgentRequest` 做 guard，并把 `request_id/session_id/trace_id` 补入 `response_context`；`cancel()` 仅转发 `request_id + actor_ref`，不承担最终业务裁定。
5. `runtime/src/recovery/RecoveryManager.*`：`evaluate()` 必须组合 `ReflectionDecision`、`Checkpoint`、`BudgetSnapshot` 与 `IdempotencyAndSideEffectReport` 后裁定 retry/replan/degrade；`execute()` 产出 `RecoveryOutcome`，`apply()` 落应用结果。
6. `memory/src/writeback/WritebackCoordinator.*` 与 `memory/include/writeback/*`：writeback 保留 `session_id`、`turn_id`，以 `WritebackResult` 返回 `persisted_turn_id`、`summary_id`、`fact_ids`、`warnings`、`partial` 与 `retryable_storage_failure`，不越权执行 recovery。
7. 安装态探测：`sudo -n dasall run ... --async --json` 当前返回 `disposition=completed` 且 `receipt_ref=null`；`status --request-id` 返回 CLI invalid arguments；`status/cancel --receipt missing` 返回 `status_missing` / `cancel_missing`。这证明 installed package 控制面可运行，但本轮不能把 installed async receipt 断言写成已闭合。

## 3. 外部参考

1. W3C Trace Context：规范要求跨服务传播统一 `traceparent` / `tracestate`，使分布式系统中的独立请求可以被关联，并强调未知或不合法 trace 字段不能被随意解释。参考：https://www.w3.org/TR/trace-context/
2. OpenTelemetry Traces：异步 producer/consumer 场景可通过 span links 表达因果关系；同一 trace 中的 spans 通过 `trace_id` 和 `parent_id` 形成层级，异步后续工作也需要显式关联。参考：https://opentelemetry.io/docs/concepts/signals/traces/

对本任务的落地启发：

1. `trace_id` 不能只存在于入口 JSON；replay envelope 和 runtime response context 都要保留。
2. 异步 receipt 的 ownership token 是访问控制事实，不应泄漏或被 cancel 跳过。
3. cancel 的成功只代表转发/受理，不等于 Runtime 已完成业务终止裁定。
4. recovery retry 必须复用 idempotency / resume 证据；预算耗尽必须走 degrade/reject，不应静默继续。
5. memory writeback 只能沉淀 turn/fact/experience 结果，不应拥有 retry / replan / abort_safe 执行权。

## 4. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 冻结因果锚点 | Access/Runtime/Memory 代码和架构文档 | `request_id/session_id/trace_id/actor_ref/checkpoint_id/turn_id` 单链模型 | 每个后续断言都能映射到同一锚点 | 若某字段当前实现不承载，则记录缺口，不伪造 |
| D2 | 锁定新增测试出口 | TODO §6.3 与真实代码 | `FullIntAsyncRecoveryCausalityTest` | 新测试不依赖旧测试是否通过 | 若跨目录链接不可行，降级为单独 target 但仍验证真实类 |
| D3 | 锁定 package 证据边界 | 实际 `/usr/bin/dasall` probe | installed async/status/cancel 运行结果 | 记录真实返回，不把 `completed` 外推为 receipt ready | 若 package 不产 receipt，保持 L4 partial |
| D4 | 锁定验收命令 | CMake Tools / installed package / 文档一致性 | Build + CTest + package probe + `rg` | 每条命令有结果 | CMake Tools 若环境失败，按 blocker 处理 |

## 5. Design -> Build 映射

| 设计项 | Build 改动 | 测试断言 | 验收命令 |
|---|---|---|---|
| D1 | 新增 `tests/integration/full_business_chain/FullIntAsyncRecoveryCausalityTest.cpp` | Access receipt、replay、cancel、Runtime recovery、Memory writeback 共享同一因果锚点 | `Build_CMakeTools(buildTargets=["dasall_fullint_011_async_recovery_causality"])` |
| D2 | 新增 `tests/integration/full_business_chain/CMakeLists.txt` 并接入 `tests/integration/CMakeLists.txt` / `tests/CMakeLists.txt` | CTest 可发现 `FullIntAsyncRecoveryCausalityTest`，并带 `fullint-011` label | `RunCtest_CMakeTools(tests=["FullIntAsyncRecoveryCausalityTest"])` |
| D3 | 不修改 installed package；只执行真实命令并记录结果 | `run --async` 当前 completed/no receipt；missing receipt status/cancel fail-closed | `sudo -n dasall run ... --async --json` 与 `sudo -n dasall status/cancel --receipt ...` |
| D4 | 回写本 TODO、worklog 与本证据包 | TODO 状态、交付物、验收命令与结果可检索 | `rg -n "FULLINT-TODO-011|FullIntAsyncRecoveryCausalityTest|fullint-011|status_missing|cancel_missing" docs tests` |

## 6. Build 三件套

代码目标：

1. 新增 `tests/integration/full_business_chain/FullIntAsyncRecoveryCausalityTest.cpp`。
2. 新增 `tests/integration/full_business_chain/CMakeLists.txt`。
3. 更新 `tests/integration/CMakeLists.txt` 与 `tests/CMakeLists.txt`，注册 target、CTest 与 discoverability。

测试目标：

1. 正例：同一因果锚点通过 async accept、replay cache、owner cancel、recovery retry admission、memory writeback。
2. 负例：owner mismatch 不转发 cancel；budget exhausted recovery 进入 degrade；installed missing receipt status/cancel fail-closed。

验收命令：

1. `Build_CMakeTools(buildTargets=["dasall_fullint_011_async_recovery_causality","dasall_gate_int_08"])`
2. `RunCtest_CMakeTools(tests=["FullIntAsyncRecoveryCausalityTest"])`
3. `sudo -n dasall run '{"prompt":"FULLINT-011 async package probe"}' --async --request-id <id> --trace-id <trace> --json --timeout-ms 120000`
4. `sudo -n dasall status --receipt receipt:fullint-011-missing --ownership-token token-missing --json --timeout-ms 120000`
5. `sudo -n dasall cancel --receipt receipt:fullint-011-missing --ownership-token token-missing --json --timeout-ms 120000`
6. `rg -n "FULLINT-TODO-011|FullIntAsyncRecoveryCausalityTest|fullint-011|status_missing|cancel_missing" docs tests`

## 7. D Gate

Gate：PASS。

理由：范围清楚、前置已满足、Build 三件套已锁定、不会跨入 `FULLINT-TODO-012/013`；安装态无法产生 receipt 的事实被记录为边界，不作为阻塞当前 L2 因果链测试的理由。

## 8. Build 结果

结果：PASS。

代码落点：

1. 新增 `tests/integration/full_business_chain/FullIntAsyncRecoveryCausalityTest.cpp`：使用真实 `AsyncTaskRegistry`、`ResultReplayCache`、`RuntimeBridge`、`RecoveryManager`、`CancellationToken`、SQLite memory manager，验证同一 `request_id/session_id/trace_id` 的 async receipt、replay、owner cancel、recovery retry/degrade、writeback persisted turn 连续性。
2. 新增 `tests/integration/full_business_chain/CMakeLists.txt`：注册 target `dasall_fullint_011_async_recovery_causality` 与 CTest `FullIntAsyncRecoveryCausalityTest`，labels 为 `integration;full-business-chain;fullint-011`。
3. 更新 `tests/integration/CMakeLists.txt` 与 `tests/CMakeLists.txt`：纳入 integration target 集合和 full business chain discoverability。
4. 更新 `tests/integration/agent_loop/RuntimeResumeIntegrationTest.cpp`：保持 runtime-local stub waiting path，在 resume 前注入 memory fixture，使旧测试继续验证 checkpoint/resume 绑定和 refreshed context，而不是因 readiness fixture 漂移失败。

验证命令：

1. `Build_CMakeTools(buildTargets=["dasall_fullint_011_async_recovery_causality"])`：PASS。
2. `RunCtest_CMakeTools(tests=["FullIntAsyncRecoveryCausalityTest"])`：PASS，1/1 passed。
3. `Build_CMakeTools(buildTargets=["dasall_runtime_resume_integration_test","dasall_fullint_011_async_recovery_causality"])`：PASS。
4. `RunCtest_CMakeTools(tests=["RuntimeResumeIntegrationTest","FullIntAsyncRecoveryCausalityTest","AccessAsyncReceiptQueryCancelIntegrationTest","RuntimeRecoveryContextIntegrationTest","MemoryWritebackIntegrationTest","MemoryFailureInjectionTest"])`：PASS，6 条 focused / cross-chain 测试均通过。
5. `Build_CMakeTools(buildTargets=["dasall_gate_int_08","dasall_full_business_chain_discoverability"])`：PASS；`FullIntAsyncRecoveryCausalityTest` 已进入全业务链 discoverability，Gate-INT-08 acceptance passed。
6. `sudo -n dasall run '{"prompt":"FULLINT-011 async package probe"}' --async --request-id fullint-011-installed-1778487483 --trace-id trace-fullint-011-installed-1778487483 --json --timeout-ms 120000`：PASS；返回 `disposition=completed`、`receipt_ref=null`、`exit_code=0`、`llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`。
7. `sudo -n dasall status --request-id fullint-011-installed-1778487483 --json --timeout-ms 120000`：当前安装态 CLI 返回 `invalid arguments`；作为 package CLI parser 边界记录，不在本轮修复。
8. `sudo -n dasall status --receipt receipt:fullint-011-missing --ownership-token token-missing --json --timeout-ms 120000`：PASS；返回 `disposition=rejected`、`reason=status_missing`、`access_error_domain=receipt`、`exit_code=5`。
9. `sudo -n dasall cancel --receipt receipt:fullint-011-missing --ownership-token token-missing --json --timeout-ms 120000`：PASS；返回 `disposition=rejected`、`reason=cancel_missing`、`access_error_domain=receipt`、`exit_code=5`。

## 9. 风险与残余缺口

1. installed package 当前 `run --async` 走 completed direct path，未产生真实 receipt；本轮只能记录为 L4 partial，不能宣称 installed async ready。
2. `status --request-id` 当前安装态 CLI 解析返回 invalid arguments；本轮不扩大修复，避免越过 `FULLINT-TODO-013` package matrix 范围。
3. 本轮新增的 cross-chain test 验证真实类组合与同锚点连续性，不替代后续 qemu、fresh install 或 production runner 证据。