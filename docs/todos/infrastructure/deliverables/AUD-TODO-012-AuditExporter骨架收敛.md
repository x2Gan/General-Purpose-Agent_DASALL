# AUD-TODO-012 AuditExporter 骨架收敛

日期：2026-04-03
任务：AUD-TODO-012
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-012` 定义为“实现 AuditExporter 导出与脱敏骨架”，验收出口为 `AuditExportFilterTest` 与 `AuditBoundaryContractTest`。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.5.1 已冻结 ExportQuery v1 的窗口+actor+action 主过滤、target/outcome 扩展规则、稳定 resume token 与 AuditEvent-only 导出边界。
3. `AUD-BLK-001` 已完成解阻，因此本轮不再缺少导出过滤与边界语义，上游对象/接口/错误码/主写骨架也已全部落盘。

## 2. 研究学习结果

### 2.1 本地证据

1. [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 在本轮前仍把导出筛选逻辑内联在 facade 中，说明 012 的根任务是把 export 职责收口成独立 internal `AuditExporter`，而不是继续在 service 内累积细节。
2. [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp) 已冻结 `ExportQuery` / `ExportResult` 的对象边界，适合作为 exporter 过滤/分页语义的 unit 验收出口。
3. [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp) 已能承载导出边界的 contract 守卫，适合继续固定“不引入 `target_pattern`/`outcome_reason`，不把 `AuditContext` 并入导出载荷”的约束。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 要求审计/安全日志导出时避免暴露 access token、session id、password、原始文件路径等高敏感数据，并强调导出/查看也属于审计链路的一部分。
2. OpenTelemetry Logs Data Model 强调稳定高频字段应保持结构化和类型固定，这支持 audit v1 继续沿用固定的 `ExportQuery` 形状，而不是在 exporter 首轮实现时引入 pattern/free-text 过滤。

### 2.3 可落地启发

1. exporter 首版应保持 internal 边界，不新增 public header 或新的 contracts 形状。
2. 分页语义可以先以内建 page size 骨架落地，再由后续配置/策略任务决定是否外提为 profile 参数。
3. 脱敏边界在 v1 更适合做“导出面约束”，即只导出 `AuditEvent`、不外带 `AuditContext`，而不是在当前冻结对象上引入复杂字段改写协议。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 收口 internal exporter 过滤职责 | audit 设计 6.2/6.3/6.5.1 | `AuditExporter.h/.cpp` | 过滤逻辑不再内联在 service 中 |
| D2 | 收口稳定分页语义 | audit 设计 6.5.1 | opaque resume token + unit tests | truncated/next_page_token/恢复位置可二值验证 |
| D3 | 收口导出边界与 contract 守卫 | audit 设计 6.5.1；AuditBoundaryContractTest | unit/contract 测试更新 | 不引入 pattern/free-text/context 泄露 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| export 过滤/分页收口为 internal exporter | 新增 [infra/src/audit/AuditExporter.h](infra/src/audit/AuditExporter.h) 与 [infra/src/audit/AuditExporter.cpp](infra/src/audit/AuditExporter.cpp) |
| service 不再内联筛选导出 | 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) |
| exporter unit 测试需要直读 internal header | 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 并扩展 [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp) |
| 导出边界不能退化为 pattern/query/context 泄露 | 扩展 [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp) |

### 4.2 Build 三件套

1. 代码目标：新增 internal `AuditExporter`，更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 接入 `dasall_infra` 构建图，并让 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 委托 exporter 导出。
2. 测试目标：扩展 `AuditExportFilterTest` 覆盖主过滤、分页 token 与 token 失配负例；扩展 `AuditBoundaryContractTest` 覆盖 exact-match/filter-shape 边界；补跑 `AuditServiceFallbackTest` 与完整 `ctest -L audit`。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`

### 4.3 D Gate

结论：PASS。

理由：

1. `AUD-BLK-001` 已清除，exporter 的过滤/分页/边界语义都已固定。
2. 当前轮只补 internal exporter、service 委托与 unit/contract 证据，不提前进入 retention 设计。

## 5. Build 落地结果

1. 新增 [infra/src/audit/AuditExporter.h](infra/src/audit/AuditExporter.h) 与 [infra/src/audit/AuditExporter.cpp](infra/src/audit/AuditExporter.cpp)，实现窗口+actor+action 主过滤、target/outcome exact-match 扩展、稳定排序、opaque resume token 和 AuditEvent-only 导出边界。
2. 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将导出逻辑从 service 内联筛选收口为委托 `AuditExporter::export_records()`。
3. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `AuditExporter.cpp` 纳入 `DASALL_INFRA_AUDIT_SOURCES`。
4. 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 与 [tests/unit/infra/AuditExportFilterTest.cpp](tests/unit/infra/AuditExportFilterTest.cpp)，为 exporter unit 测试补 `infra/src` include path，并新增主过滤、分页 token 与 token 失配负例覆盖。
5. 更新 [tests/contract/smoke/AuditBoundaryContractTest.cpp](tests/contract/smoke/AuditBoundaryContractTest.cpp)，新增 exact-match 过滤边界与 AuditEvent-only 导出载荷约束。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_audit_export_filter_unit_test dasall_audit_service_fallback_unit_test dasall_contract_audit_event_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`：发现 3 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditExportFilterTest|AuditBoundaryContractTest|AuditServiceFallbackTest"`：3/3 通过。
5. `ctest --test-dir build-ci -N -L audit`：发现 9 个测试。
6. `ctest --test-dir build-ci --output-on-failure -L audit`：9/9 通过。

## 7. 结论

1. `AUD-TODO-012` 已把导出逻辑从 `AuditService` 内联筛选推进为独立 internal exporter，并落盘了 v1 的过滤、分页和 AuditEvent-only 导出边界。
2. audit 子域当前唯一剩余硬阻塞已切回 `AUD-BLK-002`，后续应专注 RetentionOutcome 与归档/清理动作对象的设计解阻。