# KNO-TODO-026 KnowledgeHealthProbe 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-026 | 实现 KnowledgeHealthProbe 健康快照`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.4 `KnowledgeHealthProbe` 卡片与 7 KNO-D06。
3. 前置满足：`KNO-TODO-006` 已冻结 `IKnowledgeService::health_snapshot()` 签名，`KNO-TODO-017` 已提供 `FreshnessSnapshot` supporting shape，`KNO-TODO-025` 已提供 `KnowledgeTelemetryStatus`；`KNO-TODO-018/019` 尚未落盘，因此本轮必须通过 provider seam 保持 probe 的只读聚合边界与可测试性。
4. 本轮目标：补齐 `KnowledgeHealthSnapshot` public shape 与 `KnowledgeHealthProbe` supporting layer，验证依赖缺失不误判 `Healthy`，同时区分 lexical-only degrade 与真正不可用状态，不提前实现 facade/index writer 或恢复逻辑。

## 2. 设计结论

### 2.1 边界与职责

1. `KnowledgeHealthProbe` 负责：
   - 聚合 lifecycle、active manifest、新鲜度、vector backend、last-known-good、telemetry 和最近退化事实；
   - 输出 Runtime 可消费的 `KnowledgeHealthSnapshot`；
   - 基于聚合事实分类 `HealthState`。
2. `KnowledgeHealthProbe` 不负责：
   - 触发 rebuild、snapshot swap 或恢复动作；
   - 直接访问语料源、索引写路径或 facade 编排；
   - 代替 Runtime 做熔断、replan 或恢复裁定。

### 2.2 数据与接口

1. 在 `knowledge/include/KnowledgeTypes.h` 中补齐：
   - `HealthState`
   - `KnowledgeHealthSnapshot`
2. 新增 `knowledge/include/health/KnowledgeHealthProbe.h`：
   - `HealthProbeDeps`
   - `KnowledgeHealthProbe::collect()`
   - `classify_state()`
3. `HealthProbeDeps` 固定为 provider seam：
   - 由 facade / index / telemetry 在后续任务注入真实依赖；
   - 026 本轮只依赖函数对象，不把 `VersionLedger` / `IndexReader` concrete owner 提前锁死。

### 2.3 状态分类规则

1. 以下情况固定为 `Unknown`：
   - lifecycle 或 freshness 的关键 provider 缺失；
   - active manifest provider 缺失或 manifest 自身不一致；
   - vector backend 状态在 manifest 需要 dense 能力时缺失。
2. 以下情况固定为 `Unhealthy`：
   - active snapshot 丢失且无 last-known-good；
   - freshness 为 `StaleRejected` 且无 last-known-good。
3. 以下情况固定为 `Degraded`：
   - active snapshot 丢失但 last-known-good 仍可用；
   - vector backend 不可用或 profile 仅允许 lexical-only；
   - freshness 为 `StaleAllowed`；
   - telemetry 已退化、存在 `degraded_return_count` 或 recent reason code。
4. 只有在 active snapshot、新鲜度、vector backend、degraded 计数和 reason code 全部满足一致性时，才允许返回 `Healthy`。

### 2.4 评审补充

1. `KnowledgeHealthSnapshot` 仍保持 knowledge-owned public shape，不把 probe 直接耦合到恢复/监控框架；后续 032 再由 facade/runtime 做跨层映射。
2. `HealthProbeDeps` 使 026 可以先于 018/019 落地，但真实 `VersionLedger` / `IndexReader` 接线仍由 032 在 facade 完整编排阶段收口，不允许在 026 内偷渡 index owner。
3. `KnowledgeHealthSnapshot::has_consistent_values()` 对 `Healthy` 施加最严格约束：必须具备 active snapshot、fresh freshness、可用 vector backend、零 degraded count 与空 reason code。

## 3. Design -> Build 映射

1. `knowledge/include/KnowledgeTypes.h`
   - 补齐 `HealthState` 与 `KnowledgeHealthSnapshot` public ABI。
2. `knowledge/include/health/KnowledgeHealthProbe.h`
   - 定义 provider seam 与 `KnowledgeHealthProbe` 公共接口。
3. `knowledge/src/health/KnowledgeHealthProbe.cpp`
   - 实现只读聚合、reason code 去重和 `HealthState` 分类规则。
4. `tests/unit/knowledge/KnowledgeHealthProbeTest.cpp`
   - 验证 healthy 路径与 active snapshot 丢失的 `Unhealthy` 路径。
5. `tests/unit/knowledge/KnowledgeHealthProbeDegradedStateTest.cpp`
   - 验证 lexical-only degrade 语义不会误伤为 `Unhealthy`。
6. `tests/unit/knowledge/KnowledgeHealthProbeUnknownDependencyTest.cpp`
   - 验证关键 provider 缺失时不会误判为 `Healthy`。
7. `tests/unit/knowledge/KnowledgeInterfaceSurfaceSkeletonTest.cpp`
   - 回归 `IKnowledgeService::health_snapshot()` 的 public ABI 与 `KnowledgeHealthSnapshot` 形状。

## 4. 验证计划

1. build-ci configure：`cmake -S . -B build-ci -G "Unix Makefiles"`
2. 定向构建：
   - `cmake --build build-ci --target dasall_knowledge dasall_knowledge_interface_surface_unit_test dasall_knowledge_health_probe_unit_test dasall_knowledge_health_probe_degraded_state_unit_test dasall_knowledge_health_probe_unknown_dependency_unit_test`
3. 定向 `ctest`：
   - `ctest --test-dir build-ci -R "(dasall_knowledge_interface_surface_unit_test|KnowledgeHealthProbe.*Test)" --output-on-failure`

## 5. 完成判定

1. `KnowledgeHealthSnapshot` public ABI 已冻结并通过 interface surface unit gate。
2. `KnowledgeHealthProbe` 在依赖缺失时不会误判 `Healthy`。
3. lexical-only degrade 与真正不可用状态可由 unit test 二值区分。
4. 026 交付后，032 可直接把 facade/index/telemetry 的真实依赖接入 `HealthProbeDeps`，而不需要重写健康快照语义。