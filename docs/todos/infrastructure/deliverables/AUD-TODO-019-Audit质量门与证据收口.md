# AUD-TODO-019 Audit 质量门与证据收口

日期：2026-04-03
任务：AUD-TODO-019
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-019` 定义为“回写 audit 质量门与交付证据”，要求给出门禁结论、阻塞变化与回退证据。
2. `AUD-TODO-017` 已完成 unit/contract discoverability，`AUD-TODO-018` 已完成 integration discoverability，因此 audit 当前可执行测试入口已覆盖三层。
3. `AUD-BLK-001` 与 `AUD-BLK-002` 仍未解阻，因此本轮必须同时回写 PASS gate 与 BLOCKED gate，而不能伪造“全绿完成”。

## 2. 研究学习结果

### 2.1 本地证据

1. `ctest --test-dir build-ci -N -L audit` 当前可发现 9 个测试，覆盖 `AuditTypesTest`、`AuditInterfaceCompileTest`、`AuditServiceFallbackTest`、`AuditExportFilterTest`、`AuditBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`AuditServiceBoundaryContractTest`、`InfraErrorCodeMappingContractTest`、`InfraAuditHealthIntegrationTest`。
2. `ctest --test-dir build-ci --output-on-failure -L audit` 当前可一次性执行上述 9 个 audit 标签测试，天然适合作为 audit 子域统一质量门。
3. audit TODO 中 9.1 基线说明仍保留“integration 不纳入必过基线”的旧描述，已经与 `AUD-TODO-018` 完成后的实际状态不符，必须在本轮修正。

### 2.2 外部参考

1. 一个有效的工程 gate 文档既要写清已通过的可执行证据，也要写清未解阻项及其进入条件；否则后续推进会重复踩同一 blocker。该原则与仓库一任务一证据链的执行约束一致。

### 2.3 可落地启发

1. audit 当前最小稳定 gate 应直接复用 `ctest -L audit`，避免分别拼装 unit/contract/integration 的临时命令。
2. PASS gate 与 BLOCKED gate 必须拆开写成明确结论，后续推进时才能直接判断入口是在“继续实现”还是“先解阻”。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 收口 audit 当前专项 gate 基线 | TODO 9.1；017/018 完成状态 | 更新后的 TODO 9.1 | 基线命令可直接覆盖 audit 三层测试 |
| D2 | 收口 PASS/BLOCKED gate 结论 | TODO Gate 表；当前 blocker 状态 | TODO 9.3 gate 结论表 | 每个 gate 都有可执行或 blocker 证据 |
| D3 | 收口后续推进入口 | blocker 表；11.5 | deliverable / worklog / TODO 下一步 | 后续入口明确落到 AUD-BLK-001 / 002 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| audit 三层测试统一走 `ctest -L audit` | 更新 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 9.1 |
| PASS/BLOCKED gate 结论要显式表格化 | 更新同文件 9.3 |
| 当前轮收口结果要进入交付件与执行记录 | 新增本交付件，并更新 [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

### 4.2 Build 三件套

1. 代码目标：更新 TODO、deliverable、worklog，并同步修正文档中的 audit integration 新路径引用。
2. 测试目标：用 `ctest -L audit` 一次性验证 audit 当前 4 个 unit、4 个 contract、1 个 integration gate。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`

### 4.3 D Gate

结论：PASS。

理由：

1. 当前 audit 可执行测试范围已完整覆盖 unit/contract/integration 三层。
2. 仍未解阻的导出/retention 任务也已明确记录为 BLOCKED，不存在证据空档。

## 5. Build 落地结果

1. 更新 [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md)，将 `AUD-TODO-019` 标记为 Done，并补齐 9.1 audit 专项 gate 基线、9.3 gate 执行结论表与新的下一步建议。
2. 同步修正文档中 `InfraAuditHealthIntegrationTest` 的路径引用，使其与 018 已完成的 `tests/integration/infra/audit/` 实际落点保持一致。
3. 更新 [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md)，新增本轮 gate 收口记录。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -L audit`：发现 9 个测试。
4. `ctest --test-dir build-ci --output-on-failure -L audit`：9/9 通过；标签摘要为 `unit=4`、`contract=4`、`integration=1`。

## 7. 结论

1. `AUD-TODO-019` 已将 audit 当前轮从“任务逐个完成”推进到“拥有统一 audit gate 基线、PASS/BLOCKED 结论表和后续 blocker 入口”的可追溯收口状态。
2. audit 子域下一执行入口已明确转向 `AUD-BLK-001` 与 `AUD-BLK-002`，而不是继续在已完成的 health/metrics/integration 接线上反复补丁。
