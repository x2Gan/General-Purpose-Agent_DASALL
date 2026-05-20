# MEM-GAP-003 production observability closeout

来源任务：MEM-GAP-003
完成日期：2026-05-20
关联修复：MEM-FIX-003

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-003`，不合并 `MEM-GAP-004` 的 qemu / soak 证据，也不把 installed maintenance 议题混入本轮。
2. authoritative 问题定义固定为：Memory 的 logger / audit / metrics / trace 事件是否已经通过 `MemoryRuntimeDependencies` 与 `MemoryObservability` bridge 接入 production-composed path，而不是停留在 module-local no-op sink。
3. 若本轮复验通过，则 `MEM-GAP-003` 保持已闭合；若复验失败，才回到 `MemoryRuntimeDependencies`、`MemoryObservability` 与 `RuntimeLiveDependencyComposition` 的 wiring 面。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| public seam | `memory/include/MemoryDependencies.h` 已新增 `MemoryRuntimeDependencies`，`create_memory_manager()` 已接受该窄注入面 | Memory 不必在 public ABI 暴露 infra concrete 类型，也不再缺失 runtime observability 输入 |
| internal bridge | `memory/src/observability/MemoryObservability.*` 已统一适配 logger / audit / metrics / trace emit | Memory 事件不再散落在 owner 代码中各自直连 provider |
| production wiring | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已先组合 live observability bundle，再创建 memory manager | runtime_support production path 不再把 Memory 落到 no-op sink |
| focused validation | `MEM-FIX-003` 已记录 `MemoryObservabilityBridgeTest`、`MemoryWritebackIntegrationTest`、`MemoryMaintenanceIntegrationTest` 与 `RuntimeProductionHealthCompositionTest` 全绿 | 本轮只需要做最小当前树复验并补齐 gap-level 独立交付件 |

## 3. 外部参考

1. OpenTelemetry Signals 文档将 observability signals 划分为 traces、metrics、logs 等相关但独立的信号，并强调这些信号需要从同一系统行为中协同观测。Memory 当前通过 `MemoryObservability` bridge 同时发出 log / metrics / trace / audit 语义事件，与这一“多 signal 协同而非单一 warning 字段替代 observability”的口径一致。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| Memory 必须以窄注入面接收 runtime observability 依赖，而不是在 public ABI 直连 infra concrete 类型 | 测试目标：`MemoryObservabilityBridgeTest` |
| context / writeback / conflict / compression / maintenance 事件必须能落到 concrete provider | 测试目标：`MemoryObservabilityBridgeTest`、`MemoryWritebackIntegrationTest`、`MemoryMaintenanceIntegrationTest` |
| runtime_support live composition 必须先组合 observability bundle 再创建 memory manager | 测试目标：`RuntimeProductionHealthCompositionTest` |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-003`。
2. 依赖方向不变：Memory 接收 infra runtime bundle，不反向夺取 runtime / agent / tool owner 权。
3. 本轮不修改产品代码；若验证失败，才回到 wiring 实现面补修。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_observability_bridge_integration_test dasall_memory_writeback_integration_test dasall_memory_maintenance_integration_test dasall_access_runtime_production_health_composition_integration_test -j4`
	- 结果：通过；4 个 focused targets 均构建成功。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(MemoryObservabilityBridgeTest|MemoryWritebackIntegrationTest|MemoryMaintenanceIntegrationTest|RuntimeProductionHealthCompositionTest)$'`
	- 结果：通过；`100% tests passed, 0 tests failed out of 4`。

## 7. 完成判定

1. `MEM-GAP-003` 已关闭。
2. Memory observability bridge、owner emit 与 runtime_support production wiring 在当前树上复验通过，production path 不再回落到 no-op sink。
3. 本结论不外推到 qemu / soak / installed maintenance；这些不属于 `MEM-GAP-003` 的 owner 边界。