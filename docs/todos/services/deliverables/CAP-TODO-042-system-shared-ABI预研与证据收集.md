# CAP-TODO-042 system shared ABI 预研与证据收集

日期：2026-04-10
任务：CAP-TODO-042
状态：D Gate PASS / 预研结论：当前 shared ABI No-Go，保持 internal-only；health 路径复用 infra ABI

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 6.2、6.6、7.2 与 12.1 已把 `SystemSnapshotLane`、`ServiceHealthProbe` 定义为 internal-only；CAP-TODO-042 的目标不是直接生成 `ISystemService`，而是收敛“当前是否已经具备 shared ABI admission 条件”。
2. [services/src/system/SystemSnapshotLane.h](../../../../services/src/system/SystemSnapshotLane.h) 直接在 `services/src/system/` 下定义 `InternalSnapshotQuery` 与 `InternalSystemSnapshot`，并未进入 `services/include/`。这说明 system supporting objects 当前仍是实现内头文件，而不是稳定公共头。
3. 对 `InternalSystemSnapshot` 的符号引用检索结果只落在 [services/src/system/SystemSnapshotLane.h](../../../../services/src/system/SystemSnapshotLane.h) 与 [services/src/system/SystemSnapshotLane.cpp](../../../../services/src/system/SystemSnapshotLane.cpp) 内，共 7 处；`SystemSnapshotLane` 的直接引用面也只覆盖自身实现和 [tests/unit/services/system/SystemSnapshotLaneTest.cpp](../../../../tests/unit/services/system/SystemSnapshotLaneTest.cpp)，未发现 runtime、tools、apps 侧的非测试直接消费者。
4. [services/src/ops/ServiceHealthProbe.h](../../../../services/src/ops/ServiceHealthProbe.h) 将 `ServiceCircuitState`、`ServiceQueueSnapshot`、`ServiceHealthSample` 定义在 `services/src/ops/` 内部头中，同时明确 `ServiceHealthProbe final : public infra::IHealthProbe`，`snapshot()` 返回 `infra::HealthSnapshot`。这说明 health 路径当前复用的是 infra 既有 ABI，而不是 services 自有 shared ABI。
5. 对 `ServiceHealthSample` 的符号引用检索结果只覆盖 [services/src/ops/ServiceHealthProbe.h](../../../../services/src/ops/ServiceHealthProbe.h)、[services/src/ops/ServiceHealthProbe.cpp](../../../../services/src/ops/ServiceHealthProbe.cpp)、[tests/unit/services/ops/ServiceHealthProbeTest.cpp](../../../../tests/unit/services/ops/ServiceHealthProbeTest.cpp) 与 [tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp](../../../../tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp)。当前没有 runtime、tools、apps 对这些 supporting objects 的直接 include/use 证据。
6. [infra/include/health/IHealthProbe.h](../../../../infra/include/health/IHealthProbe.h) 与 [infra/include/health/HealthStateTypes.h](../../../../infra/include/health/HealthStateTypes.h) 已提供稳定的 health ABI；[infra/src/health/HealthMonitorFacade.h](../../../../infra/src/health/HealthMonitorFacade.h) / [infra/src/health/HealthMonitorFacade.cpp](../../../../infra/src/health/HealthMonitorFacade.cpp) 也明确由 infra 负责 probe 注册、快照生成与 health monitor 生命周期。这意味着若 future 只是想把 services health 纳入跨模块健康聚合，应继续复用 infra 边界，而不是额外定义 services-owned `ISystemService` / `IHealthService`。
7. [tests/unit/services/system/SystemSnapshotLaneTest.cpp](../../../../tests/unit/services/system/SystemSnapshotLaneTest.cpp) 当前明确验证“internal snapshot for health usage”；[tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp](../../../../tests/integration/services/CapabilityServicesHealthIntegrationTest.cpp) 也是通过 internal `ServiceHealthSample` 驱动 `ServiceHealthProbe`，再观察 `infra::HealthSnapshot` 输出。现有测试验证的是 internal-only + infra aggregation，不是 shared ABI。

## 2. 消费者与 supporting object 矩阵

| 候选对象 | 当前定义位置 | 真实消费者 | 稳定性判定 | shared ABI 评估 |
|---|---|---|---|---|
| `InternalSnapshotQuery` / `InternalSystemSnapshot` | [services/src/system/SystemSnapshotLane.h](../../../../services/src/system/SystemSnapshotLane.h) | `SystemSnapshotLane` 自身实现与 unit test | 直接耦合 infra health / platform snapshot / service registry 聚合细节，且仍位于 src 内部头 | 不是当前 shared-contract supporting object 候选 |
| `SystemSnapshotLane` | [services/src/system/SystemSnapshotLane.h](../../../../services/src/system/SystemSnapshotLane.h)、[services/src/system/SystemSnapshotLane.cpp](../../../../services/src/system/SystemSnapshotLane.cpp) | 自身实现与 [tests/unit/services/system/SystemSnapshotLaneTest.cpp](../../../../tests/unit/services/system/SystemSnapshotLaneTest.cpp) | 当前没有 runtime/tools/apps 非测试直接调用点 | 不具备升格 `ISystemService` 的 consumer evidence |
| `ServiceCircuitState` / `ServiceQueueSnapshot` / `ServiceHealthSample` | [services/src/ops/ServiceHealthProbe.h](../../../../services/src/ops/ServiceHealthProbe.h) | `ServiceHealthProbe` 自身实现与 services tests | 是内部运行态聚合对象，字段直接绑定 queue/circuit/adapter readiness/trace degraded 等实现事实 | 不是稳定公共 supporting object 候选 |
| `ServiceHealthProbe` | [services/src/ops/ServiceHealthProbe.h](../../../../services/src/ops/ServiceHealthProbe.h)、[services/src/ops/ServiceHealthProbe.cpp](../../../../services/src/ops/ServiceHealthProbe.cpp) | 自身实现、unit test、integration test | health 输出已经归一到 `infra::IHealthProbe` / `infra::HealthSnapshot`；services 不拥有独立 health ABI | 不应再平行定义 services-owned shared ABI |
| health 聚合边界 | [infra/include/health/IHealthProbe.h](../../../../infra/include/health/IHealthProbe.h)、[infra/include/health/HealthStateTypes.h](../../../../infra/include/health/HealthStateTypes.h) | infra health monitor、probe registry、health evaluator | 已是稳定、可复用的跨模块 ABI owner | future 若只是健康聚合，应继续走 infra ABI，而不是新建 services ABI |

## 3. 预研结论

1. 当前直接新增 `ISystemService` 或其他 services-owned system shared ABI：No-Go。原因不是 system 能力不存在，而是缺少真实跨模块消费者，且 supporting objects 仍停留在 `services/src/**` 内部头。
2. `SystemSnapshotLane` 继续保持 internal-only。当前 production 代码图上不存在足以支撑 shared ABI 的非测试直接消费者；把 `InternalSnapshotQuery` / `InternalSystemSnapshot` 提前冻结出去，只会把内部聚合细节过早写进公共边界。
3. `ServiceHealthProbe` 继续保持 internal-only，但 health 聚合路径不是“无边界”，而是明确复用 infra 已有 ABI：`infra::IHealthProbe` + `infra::HealthSnapshot`。因此 042 的结论不是“需要新的 services health ABI”，而是“health owner 已经是 infra，无需重复造面”。
4. future 若重新评估 system shared ABI，必须把“system snapshot 只读查询”与“health 聚合”拆成两个问题，不再打包成单一 `ISystemService`：
   - snapshot 路径只有在出现至少一个 runtime/tools/apps 的非测试直接消费者时，才有资格评估新的 read-only shared ABI；
   - health 路径若只是接入跨模块健康监控，应直接复用 infra ABI，不构成 services 新增 shared ABI 的理由。
5. future 仅允许 Phase-Go，不允许默认 Go。重开该议题前，至少需要同时满足以下条件：
   - 出现明确的非测试跨模块消费者，并能说明为什么现有 internal lane 或 infra health ABI 不足；
   - supporting objects 先从 `services/src/**` 内部头迁移为稳定公共头，并完成字段收敛与 owner 边界澄清；
   - 设计文档、TODO、InterfaceCatalog 与相关 contract/tests 通过新的原子任务同步更新；
   - 证明拟新增 ABI 不会复制 `infra::IHealthProbe` / `infra::HealthSnapshot` 已经承担的职责。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| system shared ABI 预研结论 | 本文件 |
| 保持 `SystemSnapshotLane` internal-only，并明确 health 复用 infra ABI | docs/architecture/DASALL_capability_services子系统详细设计.md、docs/todos/services/DASALL_capability_services子系统专项TODO.md |
| 记录 042 的任务选择、验证命令与 future reopen 条件 | docs/worklog/DASALL_开发执行记录.md |

## 5. Build 三件套

1. 代码目标：完成 `SystemSnapshotLane` / `ServiceHealthProbe` 的 shared ABI 预研，明确真实跨模块消费者、supporting object 候选与 current Go/No-Go 结论；本轮不新增 `ISystemService`，不调整任何生产头文件。
2. 测试目标：保持 `SystemSnapshotLaneTest` 与 `CapabilityServicesHealthIntegrationTest` 基线不回退，证明 042 只是 evidence-collection/docs 收口任务，不改变 system/health 产品语义。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_services dasall_unit_tests dasall_integration_tests`
   - `ctest --test-dir build-ci --output-on-failure -R SystemSnapshotLaneTest`
   - `ctest --test-dir build-ci --output-on-failure -R CapabilityServicesHealthIntegrationTest`

## 6. 风险与回退

1. 当前最大的风险不是“不做 shared ABI”，而是把 system snapshot 与 health aggregation 混成一个 services-owned `ISystemService`，从而同时复制 infra health 边界并冻结尚未稳定的 internal supporting objects。
2. 若 future 真的出现新的 snapshot 跨模块消费者，也必须先新建原子任务再评审，不允许在当前 No-Go 结论上直接追加 public header 或 contract metadata。
3. 本轮不修改任何生产代码、公共头文件或 InterfaceCatalog metadata；若 future reopen 042，只能基于新增 consumer evidence 重开，而不是回滚当前 internal-only 结论去做无证据扩边。