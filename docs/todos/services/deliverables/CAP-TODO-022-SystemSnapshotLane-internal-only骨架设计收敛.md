# CAP-TODO-022 SystemSnapshotLane internal-only 骨架设计收敛

日期：2026-04-09
任务：CAP-TODO-022
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.2 / 6.3 已冻结 `SystemSnapshotLane` 的职责是汇聚系统与本地服务状态快照，仅供内部编排与 health 使用。
2. 同一设计文档 6.4 / 6.5 / 6.6.1 / 12.1 已明确 system snapshot 当前没有稳定跨模块消费者，因此必须保持 internal-only，不能扩张 `ISystemService` 或 shared contracts。
3. 设计文档 7.1 / 10.2 已把 system snapshot 列为 V1.1 的低风险只读路径之一，因此本轮实现应优先验证 internal snapshot facts，而不是构造新的公共控制接口。
4. CAP-TODO-010 / 011 已提供组合根与 unit discoverability，CAP-TODO-017 / 019 / 021 已为 query/diagnose/data 路径建立只读组件模式，因此 022 可以按 internal snapshot lane 最小骨架继续推进。

## 2. 外部参考

1. Azure Bulkhead pattern 对控制面和只读观察面的隔离建议，支持本轮把 system snapshot 做成独立 internal lane，而不是挤进 execution/data 公共 ABI。
2. 内部 health/reporting 常见模式要求 strict 与 degraded 两类语义并存；这支持本轮把 strict health 缺失做成 fail-closed，而在非 strict 模式下允许返回 degraded snapshot。
3. 由于当前没有稳定跨模块消费者，internal snapshot 只需服务 health/orchestration facts，不应被包装成新的外部服务接口。

## 3. Design 结论

1. `SystemSnapshotLane` 作为 internal-only 组件新增于 `services/src/system/`，聚合 infra health、platform snapshot、resource summary 与本地 service registry。
2. lane 输出 `InternalSystemSnapshot`，其中显式携带 `snapshot_json`、`captured_at_ms`、`service_instances`、`infra_health_ok`、`degraded` 与 `error_message`；这些对象全部停留在 src/system 内部，不进入 public include 面。
3. 当 `strict_health=true` 且 infra health 快照缺失时，lane 立即 fail-closed 返回内部错误；当非 strict 模式下某些源缺失时，lane 仍返回 degraded snapshot，并把缺失值序列化为 `null`。
4. `include_service_registry=false` 时，lane 不调用 service registry loader，避免无意义的内部依赖和额外聚合成本。
5. 当前 lane 只提供 internal snapshot query，不接入 facade 公共接口，也不尝试升格为新的 shared ABI。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `SystemSnapshotLane`、`InternalSnapshotQuery`、`InternalSystemSnapshot` | services/src/system/SystemSnapshotLane.h、services/src/system/SystemSnapshotLane.cpp |
| strict health fail-closed 与 degraded snapshot 语义 | services/src/system/SystemSnapshotLane.cpp |
| 覆盖 success、strict health failure、degraded snapshot、service registry omission 四类 unit 场景 | tests/unit/services/system/SystemSnapshotLaneTest.cpp |
| 新增 system 单测子目录并接入 services/top-level unit 聚合 | tests/unit/services/system/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/CMakeLists.txt |
| 将 system snapshot 组件纳入 services 构建图 | services/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/system/SystemSnapshotLane.h/.cpp`，实现 internal system snapshot query、strict/degraded 语义与 service registry 可选聚合。
2. 测试目标：新增 `tests/unit/services/system/SystemSnapshotLaneTest.cpp`，覆盖 success、strict health failure、degraded snapshot、service registry omission 场景。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 lane 仍是 internal-only 聚合器，未接入 facade 公共面或独立 health probe；若未来需要跨模块消费，必须先补设计证据并发起新的 interface admission review。
2. 022 不引入统一错误对象映射，而是返回内部 `error_message`；若后续要把 system snapshot 接到外部公共链路，必须先定义稳定 supporting object。
3. 当前 snapshot JSON 假定各个 loader 返回已序列化 JSON 片段；若未来需要结构化 schema 校验，应另开任务收敛内部 schema。 