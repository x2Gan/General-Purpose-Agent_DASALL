# DiagnosticsRetainedSnapshotContract (Single Source of Truth)

状态：Frozen
Owner：infra diagnostics
关联任务：INT-TODO-004
关联阻塞：INT-BLK-04
关联 Gate：Gate-INT-01、Gate-INT-05

## 1. 目的

本文件冻结 infra diagnostics retained snapshot 的 execute -> store -> get_snapshot -> export round-trip 契约与测试拓扑，避免 facade 已存在但回读语义与 Gate 断言继续漂移。

## 2. 范围与术语

适用范围：

1. `IDiagnosticsService::execute()`、`get_snapshot()`、`export_snapshot()` 三条公开路径。
2. `DiagnosticsSnapshot` 的 retained 语义、`snapshot_id` 稳定性、retention 窗口和 redaction 后回读约束。
3. `InfraDiagnosticsSmokeTest`、`DiagnosticsSnapshotStoreContractTest`、`DiagnosticsFixtureSurfaceTest`、`InfraDiagnosticsIntegrationTest` 的分层职责。

术语约定：

1. `retained snapshot`：经过 RedactionEngine、SnapshotAssembler、SnapshotStore 落盘后的 diagnostics snapshot；不是 executor 原始输出。
2. `round-trip`：同一个 `snapshot_id` 在 retention 窗口内完成 execute、get_snapshot、export_snapshot 三段链路，并保持关键 redacted 字段稳定。
3. `fixture topology`：用于验证 retained snapshot 行为的测试分层和依赖注入形状，不等于真实导出 backend 的全部实现细节。

## 3. Round-Trip 契约

| 操作 | 成功前置条件 | 成功后置条件 | 明确禁止 |
|---|---|---|---|
| `execute(command)` | facade 已 start；命令命中白名单；参数合法；redaction 与 store 可用 | 返回 `ok=true`、合法 `snapshot_id`，且返回前已把 redacted snapshot 持久化到 SnapshotStore | 不得只返回内存对象而不落盘；不得把未脱敏原始输出暴露给调用方 |
| `get_snapshot(snapshot_id)` | `snapshot_id` 在 retention 窗口内仍可用 | 返回与 execute 同源的 retained snapshot；`summary`、`command.actor_ref`、`evidence_refs` 等 redacted 字段在回读时保持稳定 | 不得重新从 executor 原始输出生成新 snapshot；不得回读未脱敏 actor ref |
| `export_snapshot(local json)` | `snapshot_id` 可回读；target=`LocalFile`；format=`Json`；target_ref 命中受控本地规则 | export 必须基于已存储的 redacted snapshot，返回 `ok=true`、`checksum=sha256:<64hex>`，并固定导出为 UTF-8 JSON Lines | 不得直接导出 executor 原始输出；不得为 `TextArchive` 或任意 JSON blob 放宽语义 |
| `export_snapshot(remote)` | 显式启用 remote export 且 target 命中 allow-list | 在当前 v1 默认配置下必须拒绝，返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED` 或等价 contracts 映射 | 不得在 remote 默认关闭时继续做网络 IO 或伪装 success |

## 4. `snapshot_id` / retention / failure 规则

1. `snapshot_id` 必须在 execute 成功时一次生成，并作为 get/export 的唯一定位锚点；在 retention 窗口内不得被重写或复用到其他 snapshot。
2. retention 窗口受 `infra.diagnostics.snapshot.retention_days` 控制；只有处于该窗口内的 retained snapshot 才承诺可被 `get_snapshot` / `export_snapshot` 成功消费。
3. 超出 retention、store 不可用或索引缺失时，只允许返回结构化 diagnostics 错误；不得回退到原始输出、不完整快照或空 success。
4. RedactionEngine 失败时不得继续 store/export；必须返回 `INF_E_DIAG_REDACTION_FAIL` 或等价 contracts 映射，并阻断 retained snapshot 产生。

## 5. 测试拓扑冻结

| 测试层 | 用例 | 冻结职责 | 不承担 |
|---|---|---|---|
| smoke | `InfraDiagnosticsSmokeTest` | 最小 retained snapshot round-trip：execute -> get_snapshot -> local export -> remote-disabled reject | 不承担 metrics/audit bridge 细节，也不覆盖 store failure 注入矩阵 |
| unit / fixture | `DiagnosticsSnapshotStoreContractTest`、`DiagnosticsFixtureSurfaceTest` | 固定 store/get seam、fixture schema、`snapshot_id` 与 redaction surface | 不直接宣称系统 Gate 已绿 |
| integration | `InfraDiagnosticsIntegrationTest` | 验证 retained snapshot 与 export/audit bridge 的协同行为 | 不替代 smoke 的最小 round-trip 断言 |
| failure injection | `DiagnosticsSnapshotStoreTest` 等 failure cases | 验证 timeout、store_fail、export_fail、redaction_fail 的结构化错误与观测证据 | 不重写 success contract |

## 6. 当前 true integration 基线

`InfraDiagnosticsSmokeTest` 当前冻结以下最小成功断言：

1. `execute()` 返回合法 snapshot，并能通过同一 `snapshot_id` 被 `get_snapshot()` 回读。
2. 回读后的 `summary` 必须保持 `diagnostics redacted health snapshot`，`command.actor_ref` 必须保持 `actor://redacted`。
3. `export_snapshot(LocalFile, Json)` 必须返回 `sha256:` 前缀 checksum。
4. `export_snapshot(RemoteUpload, Json)` 在默认配置下必须失败，并映射到 remote-disabled diagnostics 错误。

## 7. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| 固定 retained snapshot store/get seam 与 fixture topology | `INT-TODO-011` | `infra/src/diagnostics/DiagnosticsServiceFacade.cpp`、`tests/fixtures/infra/DiagnosticsSnapshotFixture.h` | `DiagnosticsSnapshotStoreContractTest`、`DiagnosticsFixtureSurfaceTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R "DiagnosticsSnapshotStoreContractTest|DiagnosticsFixtureSurfaceTest" --output-on-failure` |
| 修复 execute/store/get/export retained snapshot round-trip | `INT-TODO-015` | `infra/src/diagnostics/DiagnosticsServiceFacade.cpp`、相关 snapshot store 源码 | `InfraDiagnosticsSmokeTest`、`DiagnosticsSnapshotStoreTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "InfraDiagnosticsSmokeTest|DiagnosticsSnapshotStoreTest" --output-on-failure` |
| 固化 diagnostics retained snapshot Gate discoverability | `INT-TODO-020` | `tests/integration/infra/InfraDiagnosticsSmokeTest.cpp`、`tests/integration/infra/CMakeLists.txt` | `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "InfraDiagnosticsSmokeTest|InfraDiagnosticsIntegrationTest" --output-on-failure` |

## 8. 验证锚点

```bash
rg -n "retained snapshot|execute|get_snapshot|export|retention|snapshot_id" \
  docs/ssot/DiagnosticsRetainedSnapshotContract.md \
  docs/architecture/DASALL_infra_diagnostics模块详细设计.md \
  docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md
```

## 9. 结论

1. `INT-BLK-04` 的设计出口已经固定：retained snapshot 的输入、持久化、回读、导出与拒绝语义全部由同一份契约定义。
2. 004 完成后，后续 Build 只需围绕 store/get seam、smoke 回归与 integration gate 落地，不再需要反复讨论 `snapshot_id`、retention 与 redaction 后回读的基础含义。