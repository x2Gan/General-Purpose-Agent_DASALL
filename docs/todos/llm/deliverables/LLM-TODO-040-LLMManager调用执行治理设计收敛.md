# LLM-TODO-040 LLMManager 调用执行治理设计收敛

日期：2026-04-13
任务：LLM-TODO-040
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.10.1 已把 `timeout_policy` 的 `timeout_ms`、`retry_budget` 与 `circuit_breaker_threshold` 明确冻结为“由 LLMManager 内部调用执行治理逻辑执行”，因此 040 的 owner 只能落在 `LLMManager.cpp`，不能继续挂在 ModelRouter 或 AdapterRegistry 上。
2. 同一设计文档的 6.11 已把 unary 首轮 Build 的并发策略收敛为“同步 unary + caller-owned thread”，同时明确要求 `active llm calls` 使用 `bounded semaphore + reject`，并禁止通过 `detach` 线程逃避超时治理。因此 040 不能靠隐藏队列、后台线程或 block-then-wait 模式实现，而必须显式做 fail-fast 并发闸门。
3. 6.9.2 已冻结恢复原则：仅传输类失败按 `timeout_policy` 的 retry budget 在同 route 内重试，Prompt 资产与治理失败不触发 provider failover，也不允许 llm 越权持有最终恢复裁定。这意味着 040 只能实现“同 route 限次重试 + 失败分类提示”，不能把 024 的 fallback 链编排提前揉进来。
4. 6.15.6 已把 v1.1 P2-1 的闭环写死为“调用执行治理合并入 LLMManager，健康聚合合并入 AdapterRegistry”。因此 040 的正确接缝不是新起一个独立 breaker/health owner，而是直接复用 021 已落盘的 `record_call_failure()` / `record_call_success()` 与 blocked route 状态。
5. [docs/todos/llm/deliverables/LLM-TODO-021-AdapterRegistry生命周期与健康快照设计收敛.md](./LLM-TODO-021-AdapterRegistry生命周期与健康快照设计收敛.md) 已明确：`record_call_failure()` 只提供最小 failure counter / blocked threshold 入口，真正的 timeout / retry / circuit breaker 执行 owner 留给 040/024。因此 040 本轮没有前置 blocker，只需在调用执行路径消费 021 的状态入口，而不是回改 registry owner。

## 2. 外部参考

1. AWS Builders' Library 的 [Timeouts, retries, and backoff with jitter](https://aws.amazon.com/builders-library/timeouts-retries-and-backoff-with-jitter/) 明确指出：timeout 的作用首先是避免调用方长时间占住线程、连接等有限资源；retry 是“selfish”的，必须限次并在单一层实施，否则会放大下游负载。040 因此只在 LLMManager 这一层实现有限重试，不额外引入 sleep/backoff 或多层重试链。
2. Microsoft Azure Architecture Center 的 [Circuit Breaker pattern](https://learn.microsoft.com/en-us/azure/architecture/patterns/circuit-breaker) 强调：当失败达到阈值时，请求应 fail-fast，而不是继续把线程阻塞到 timeout 结束；circuit breaker 与 retry 应协同工作，一旦 breaker 判定故障非瞬时，就应停止继续重试。040 据此采用“registry blocked route 代理 open breaker”策略：一旦 021 的 failure counter 达到阈值，后续同 route 尝试立即拒绝。

## 3. Design 结论

1. 040 在 [llm/src/LLMManager.h](../../../../llm/src/LLMManager.h) 与 [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 中新增 module-local `LLMCallExecutor`、`LLMCallExecutionResult` 与 `LLMCallExecutionFailureReason`，只负责 unary 调用治理本身，不承担 024 的 PromptPipeline/ModelRouter/ResponseNormalizer/UsageAggregator 编排。
2. `LLMCallExecutor::init()` 只消费 [llm/include/LLMSubsystemConfig.h](../../../../llm/include/LLMSubsystemConfig.h) 中已经冻结的 `timeout_policy` 与 `worker_threads`，把并发上限固定为 `worker_threads`，重试预算固定为 `retry_budget`。040 不新增第二套 llm 调用治理配置，也不反向依赖完整 `RuntimePolicySnapshot`。
3. 每次 attempt 前，040 都先从 [llm/src/route/AdapterRegistry.h](../../../../llm/src/route/AdapterRegistry.h) 读取 concrete route state：若 route 不存在则 fail-closed；若 route 已 blocked，则把它视为“breaker open”并立即返回，不再发起 adapter 调用。
4. 由于当前 `ILLMAdapter::generate()` 仍是同步 SPI，040 的 timeout 实现采用“cooperative deadline propagation + post-return timeout classification”：先把 `LLMRequest.timeout_ms` 收敛到 `min(request.timeout_ms, timeout_policy.timeout_ms)`，并把最终 `model_route` 写回 request；然后测量一次实际执行耗时。若 adapter 返回时间超出预算，即使 payload 看似成功，也按超时失败收敛。这保证了 040 不通过 `detach` 线程、隐藏 worker 或锁内等待去伪造超时治理。
5. 同 route 重试只对 retryable 的 adapter transport failure 与 synthetic timeout 生效，且尝试次数严格受 `retry_budget + 1` 上限约束。040 不在 caller-owned 线程内引入额外 sleep/backoff，因为当前设计只冻结了“限次重试”，没有冻结额外等待策略；在同步 unary 模式下把 caller 线程继续睡眠只会扩大资源占用。
6. 每次失败后，040 只调用 021 的 `record_call_failure()` 回写 failure counter；成功后调用 `record_call_success()` 清零计数。若回写后 route 进入 blocked，则 040 立即返回 `RouteBlocked`，而不是再做一次盲目重试。这样 `circuit_breaker_threshold` 的执行语义继续保持“阈值判定在 AdapterRegistry，执行 owner 在 LLMManager”。
7. 并发治理采用无等待的 atomic slot acquisition：当 `active_call_count >= worker_threads` 时立即拒绝新请求，并返回显式的 runtime failure；拒绝不会写入 registry failure counter，因为这是本地调用面过载而非某个 provider route 的健康恶化。
8. 040 明确不做以下职责：
   - 不做 024 的 route chain 展开、fallback 结果装配和 `LLMManagerResult` 映射。
   - 不做 022 的语义归一化与 provider-private payload 剥离。
   - 不做 023 的 usage/cost 归并。
   - 不做 041 的 provider config projection、adapter init 或 auth/base_url overlay。
9. 本轮无新增 blocker。唯一需要保持的对齐条件是：`AdapterRegistryConfig.blocked_failure_threshold` 应由 024 在真正装配 manager/registry 时从 `LLMSubsystemConfig.timeout_policy.circuit_breaker_threshold` 派生；040 的单测夹具直接手工对齐这两个值即可。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地 LLMManager 内部 unary 调用治理 owner | [llm/src/LLMManager.h](../../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) |
| 覆盖 deadline 传播与后验 timeout 判定 | [tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp](../../../../tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp) |
| 覆盖 retry budget 与 blocked threshold 接缝 | [tests/unit/llm/LLMManagerRetryBudgetTest.cpp](../../../../tests/unit/llm/LLMManagerRetryBudgetTest.cpp) |
| 覆盖 bounded semaphore + reject 与 RAII 释放 | [tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp](../../../../tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp) |
| 将 040 的实现与测试接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `LLMCallExecutor`，覆盖 effective timeout 计算、同 route retry budget、registry blocked fast-fail、bounded semaphore reject 和 failure counter 回写。
2. 测试目标：
   - `LLMManagerTimeoutPolicyTest`：覆盖 request timeout clamp、late success timeout 化、成功路径与 timeout failure counter 回写。
   - `LLMManagerRetryBudgetTest`：覆盖 retryable 失败在预算内成功、达到 blocked threshold 后 fail-fast。
   - `LLMManagerConcurrencyGuardTest`：覆盖并发上限拒绝与 slot 在成功后自动释放。
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `LLMManagerTimeoutPolicyTest`
   - `RunCtest_CMakeTools` 运行 `LLMManagerRetryBudgetTest`
   - `RunCtest_CMakeTools` 运行 `LLMManagerConcurrencyGuardTest`

## 6. 风险与回退

1. 040 当前无法在同步 adapter SPI 上做抢占式取消，因此 timeout 采用 cooperative deadline + 后验超时判定。若后续 025/026/027 引入真正可取消的 transport，040 的 timeout owner 仍可保留，但具体 deadline enforcement 可以下沉到 transport/adapters。
2. 040 当前把 “breaker open” 代理为 021 的 blocked route，而没有实现带 half-open timer 的完整状态机。这满足当前 detailed design 的最小验收，但若后续需要更细粒度的恢复探测，应继续沿 `AdapterRegistry + health_probe` 路径扩展，而不是再起第三个 breaker owner。
3. bounded semaphore 上限当前直接取自 `worker_threads`。若后续 runtime 需要把“llm worker 数”和“inflight 上限”拆成两个独立策略字段，应通过 `LLMSubsystemConfig` 投影新增字段来演进，而不是在 040 内部硬编码第二套配置源。