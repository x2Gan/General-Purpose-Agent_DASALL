# LLM-TODO-021 AdapterRegistry 生命周期与健康快照设计收敛

日期：2026-04-13
任务：LLM-TODO-021
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.11 已把 `adapter health snapshot` 冻结为 copy-on-write snapshot，并明确要求“每次 health probe 回写时拷贝当前 snapshot、修改、再 atomic swap；ModelRouter 读取不加锁”。因此 021 的核心不是补一个普通 map，而是补一个可原子发布、读路径不持锁的 snapshot owner。
2. 同一设计文档的组件职责表把 AdapterRegistry 定义为“管理 Cloud/LAN/Local adapters 生命周期、可用性、capability 标签与健康状态聚合”的唯一 owner，并明确它的输出是 `adapter handle / adapter list / health snapshot`，同时禁止它承担策略选择和重试裁定。这直接限制了 021 的范围：只能做 registry 与 health aggregation，不得提前混入 040 的 timeout/retry/circuit breaker 执行治理，也不得复制 020 的路由评分逻辑。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.6 已把 LLMManager 调用顺序冻结为“ModelRouter -> AdapterRegistry -> 调用执行 -> ResponseNormalizer -> UsageAggregator -> observability -> fallback”。因此 021 必须输出一个可被 024 直接消费的 concrete route key -> adapter handle 映射面，而不是继续停留在抽象 provider family 或 profile route 名层面。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 021 的完成判定冻结为“registry 读路径不持锁做 I/O、health snapshot 可原子发布”，测试门要求至少覆盖 `AdapterHealthProbeTest` 与 `LLMManagerFallbackTest` 两条单测。
5. [docs/todos/llm/deliverables/LLM-TODO-020-ModelRouter路由选择设计收敛.md](./LLM-TODO-020-ModelRouter路由选择设计收敛.md) 已在上一轮把 `ModelRouterHealthSnapshot` 明确保留为 module-local 输入对象，并显式要求 021 只在 llm 内部扩展 snapshot owner，不把 health shape 推入 shared contracts。
6. 021 开始前暴露出一个直接 blocker：`ILLMAdapter::health_check()` 与 `ILLMManager::health_check()` 只前向声明了 `HealthStatus`，而当前仓库中唯一 concrete 定义位于 test-local 的 [tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h)。这意味着 AdapterRegistry 一旦真正 materialize `health_check()` 返回值，就会缺少可复用的正式类型定义。021 因此需要做一个最小 blocker fix：把 `HealthStatus` 提升为 llm 公共 leaf type，但不改动既有 SPI 形状。

## 2. 外部参考

1. cppreference 的 [std::atomic<std::shared_ptr>](https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2) 说明确认了 C++20 可以对 `shared_ptr` 做原子 `load/store/exchange`，并保证读者拿到的是同一份 ownership-consistent 指针副本。这直接支持 021 采用“`std::shared_ptr<const Snapshot>` + atomic load/store”来发布 immutable registry snapshot，而不是手写裸指针或双锁复制方案。
2. Kubernetes 官方 [Liveness, Readiness, and Startup Probes](https://kubernetes.io/docs/concepts/configuration/liveness-readiness-startup-probes/) 文档把 `readiness` 定义为“是否还能接收流量”，`liveness` 定义为“是否已经卡死需要重启”。021 借鉴的是这一语义分层：`ready=false` 的 adapter 应进入 hard block，不再参与 ModelRouter 候选；`ready=true && degraded=true` 的 adapter 则保留可见，但通过 failure counter 让 020 的 route scoring 自动降权，而不是立即从 fallback 链中彻底剔除。

## 3. Design 结论

1. 021 在 [llm/src/route/AdapterRegistry.h](../../../../llm/src/route/AdapterRegistry.h) 与 [llm/src/route/AdapterRegistry.cpp](../../../../llm/src/route/AdapterRegistry.cpp) 中新增 module-local `AdapterRegistry` concrete owner，并把内部状态收敛为 immutable `AdapterRegistrySnapshot`。snapshot 按 concrete route key（`provider_id/model_id`）持有 adapter handle、deployment type、capability tags、streaming 标记、最近一次 health 事实与连续失败计数。这样 024 后续可以直接按 020 产出的 route key 取 handle，而不需要再做第二次 route 解析。
2. snapshot 发布策略固定为“写路径串行 + 读路径 lock-free”：`register_adapter()`、`unregister_adapter()`、`probe_health()`、`record_call_failure()`、`record_call_success()` 在短写锁内复制当前 snapshot 并重新发布；`snapshot()`、`resolve_route()` 与 `health_snapshot()` 只做 atomic load，不持锁、不做 I/O。所有实际 `adapter->health_check()` 调用都发生在写锁外，满足 6.11 的“不持 L2 锁执行 I/O”。
3. 注册维度固定为 concrete route key，而不是 adapter family 或 provider 级别。单个 adapter 实例允许被多个 `provider_id/model_id` route 复用；重复注册同一路由时，021 选择“替换 handle/元数据但保留 failure counter 与最近 health 事实”，这样后续 041 若因为 mutable overlay 或 provider instance refresh 重建 adapter handle，不会无意中把健康历史全部抹掉。
4. 健康映射在 021 中固定为两层：
   - `HealthStatus.ready=false`：route 直接 `blocked=true`，ModelRouter 通过 `health_blocked` 硬过滤该候选。
   - `HealthStatus.ready=true && HealthStatus.degraded=true`：route 保持 `blocked=false`，但 failure counter 增加，020 会通过 `health_failure_penalty` 对其降权。
   - `record_call_failure()`：在未进入 040 前，先把失败计数与简单熔断阈值收敛进 registry。默认首个失败就进入 degraded；连续失败达到阈值后 route 转为 blocked。`record_call_success()` 则清零计数并恢复 healthy。
5. 021 的对外可消费输出面只保留三类：`snapshot()` 提供 immutable registry 视图，`resolve_route()` 提供单一路由的 adapter handle 与 capability metadata，`health_snapshot()` 把 registry 内部 richer state 投影为 020 已冻结的 `ModelRouterHealthSnapshot`。这样 021 不会反向扩张 020/024 的 public ABI。
6. 直接 blocker fix 仅限于新增 [llm/include/HealthStatus.h](../../../../llm/include/HealthStatus.h)，并让 [tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h) 复用该定义。021 不改 `ILLMAdapter` / `ILLMManager` 的方法签名，也不把 HealthStatus 扩写成 infra 健康子系统的通用模型；它只是 llm 在真正调用 `health_check()` 之前必须补齐的最小 leaf type。
7. 021 不提前实现 041 的 provider config projection，也不在 registry 内触发 adapter `init()`。adapter init config、auth ref、header refs、base_url alias 与 activation flag 的真实投影仍留给 041；当前轮次只冻结 registration owner、handle lookup 与 health snapshot owner。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 引入 HealthStatus 的最小公共 leaf type，解除 registry materialize `health_check()` 的 blocker | [llm/include/HealthStatus.h](../../../../llm/include/HealthStatus.h)、[tests/mocks/include/MockLLMAdapter.h](../../../../tests/mocks/include/MockLLMAdapter.h) |
| 落地 AdapterRegistry concrete owner、immutable snapshot、route handle 解析与 health snapshot 投影 | [llm/src/route/AdapterRegistry.h](../../../../llm/src/route/AdapterRegistry.h)、[llm/src/route/AdapterRegistry.cpp](../../../../llm/src/route/AdapterRegistry.cpp) |
| 覆盖健康探针正反例、degraded/block 映射、registration metadata 与 snapshot 读取 | [tests/unit/llm/AdapterHealthProbeTest.cpp](../../../../tests/unit/llm/AdapterHealthProbeTest.cpp) |
| 覆盖 registry health snapshot 被 020 消费后的 fallback 结果 | [tests/unit/llm/LLMManagerFallbackTest.cpp](../../../../tests/unit/llm/LLMManagerFallbackTest.cpp) |
| 将 021 实现与两条新用例接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `AdapterRegistry`，提供 concrete route key 注册、adapter handle 读取、lock-free health snapshot 读取、copy-on-write health 发布，以及最小 failure counter / blocked 映射。
2. 测试目标：
   - `AdapterHealthProbeTest`：覆盖 healthy probe、degraded probe、not-ready probe、handle metadata 读取与 route unregister 的 fail-closed 行为。
   - `LLMManagerFallbackTest`：覆盖 registry failure counter 触发 primary route block，且 `ModelRouter` 能消费 registry 的 `health_snapshot()` 自动选择 fallback route。
3. 验证动作：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `AdapterHealthProbeTest`
   - `RunCtest_CMakeTools` 运行 `LLMManagerFallbackTest`

## 6. 风险与回退

1. 021 当前把失败计数和最小 blocked 阈值收敛进 registry，但没有落地 040 的 timeout/retry/circuit breaker 执行路径。因此 `record_call_failure()` 只是一条 module-local 状态入口，不应被误解为完整调用治理 owner。
2. 021 选择 concrete route key 作为 registry 维度，会让同一 adapter handle 可能被多个模型路由复用。这符合 020/024 当前需要；若后续 provider instance 生命周期需要独立于 model route 管理，应在 041 的 provider projection 中补 instance 层对象，而不是回退 021 的 route key 查找面。
3. HealthStatus 本轮仅提升为 llm leaf type；若后续 infra 健康域希望统一模型，需要单独做 shared admission 评审，而不是在 021 中把它直接推出 llm 边界。