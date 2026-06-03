# WP-MEM-GAP-008 production logging assert closeout

来源任务：WP-MEM-GAP-008
关联缺口：GAP-P1-D
完成日期：2026-06-03

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-008 / GAP-P1-D`，不把 installed gate、跨 session FactQuery、vector recall 质量演进或后续 P2/P3 任务混入同一轮。
2. authoritative 问题定义固定为：`MemoryProductionLoggingIntegrationTest` 是否已经把 `writeback_partial`、`vector_unavailable`、`maintenance_tick`、`summarizer_fallback`、`schema_mismatch` 五类场景的 metric / audit / trace 字段断言锁定下来，而不只是证明事件“存在”。
3. owner 边界保持不变：本轮不修改 memory 实现代码，不扩 observability public surface；只增强 focused integration evidence，并按真实 owner 路径收口断言。

## 2. 本轮代码结果

| 目标 | 落盘结果 | 对 closeout 的意义 |
|---|---|---|
| partial / degraded 字段面补强 | 更新 [tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp](../../../tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp)，固定 `writeback.degraded` 的 `partial` / `warning_codes` log、metric、audit、trace 断言 | production logging evidence 不再停留在 “事件发生了”，而是锁定 partial writeback 的 owner-safe telemetry 字段 |
| summarizer fallback 与 vector unavailable | 同一测试现增加第二轮正常 writeback 作为 compression seed，稳定触发 `prepare_context()` compression path，并对 `compression_note_count=2`、`compression_strategy=template`、`warning_codes=vector_unavailable` 做断言 | `summarizer_fallback` 不再依赖偶发 token/turn 条件；fallback 证据已可重复复现 |
| maintenance tick requested fields | 同一测试现锁定 `checkpoint_requested`、`retention_requested`、`quarantine_requested`、`vector_rebuild_requested` 与 `warning_codes=vector_rebuild_skipped` | maintenance degraded telemetry 现有 focused field coverage，不再只看单个 warning |
| schema mismatch lifecycle fields | 同一测试现按实际 manager preopen 路径锁定 `result_code`、`failure_reason=store_preopen_failed`、`storage_backend=sqlite` 的 metric / audit / trace 断言 | schema mismatch 断言改为贴合真实 owner 路径，避免把错误的 `store_open_failed` 预期固化成假阴性 |

## 3. 设计与关联依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-008` 固定为“只增强 `MemoryProductionLoggingIntegrationTest` 证据，不改实现代码”。
2. [docs/ssot/KeySubsystemLoggingFieldMatrix.md](../../../docs/ssot/KeySubsystemLoggingFieldMatrix.md) 已冻结 memory allowlist：`failure_reason`、`partial`、`storage_backend`、maintenance 请求字段、`compression_strategy` 等均属于 owner-safe logging attrs。
3. [docs/architecture/DASALL_infra_logging模块详细设计.md](../../../docs/architecture/DASALL_infra_logging模块详细设计.md) 已把 `MemoryProductionLoggingIntegrationTest` 固定为 memory build-tree production logging focused evidence；本轮只补字段面，不改变 sink / allowlist 设计。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| partial writeback 必须保留 degraded telemetry fields | `MemoryProductionLoggingIntegrationTest` |
| summarizer fallback 必须在 context degraded 路径留下 owner-safe field evidence | `MemoryProductionLoggingIntegrationTest` |
| maintenance tick 请求位必须进入 audit / trace 字段 | `MemoryProductionLoggingIntegrationTest` |
| schema mismatch lifecycle telemetry 必须按真实 owner 路径断言 | `MemoryProductionLoggingIntegrationTest` |

## 5. D Gate

1. 不改实现：本轮所有变更都位于 [tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp](../../../tests/integration/memory/MemoryProductionLoggingIntegrationTest.cpp) 与文档回写。
2. focused acceptance：验收锁定单条 `MemoryProductionLoggingIntegrationTest`，避免把不属于本任务的更宽 logging/package/qemu gate 混入 blocker 判定。
3. owner-safe 口径：schema mismatch 的 `failure_reason` 以真实 emit 值 `store_preopen_failed` 为准，不为了迁就旧预期改写 runtime/memory 实现。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_memory_production_logging_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryProductionLoggingIntegrationTest"])`
   - 结果：通过，1/1。

## 7. 完成判定

1. `WP-MEM-GAP-008 / GAP-P1-D` 已闭合。
2. `MemoryProductionLoggingIntegrationTest` 现已锁定 `writeback_partial`、`vector_unavailable`、`maintenance_tick`、`summarizer_fallback`、`schema_mismatch` 的 metric / audit / trace 字段证据。
3. Memory 当前 P1 entry tasks 已全部闭合；剩余高优先级焦点回到 installed gate / GA 绿色记录与后续 V2 质量演进项。