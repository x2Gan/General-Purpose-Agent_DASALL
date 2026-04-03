# AUD-TODO-013 IAuditRetention 接口冻结

日期：2026-04-03
任务：AUD-TODO-013
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-013` 定义为“定义 IAuditRetention 接口与 RetentionOutcome 对象”，验收出口为 `AuditInterfaceCompileTest` 与 `InfraErrorCodeMappingContractTest`。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.6.2 已冻结 `RetentionOutcome` 的 completed/error_code 二值判定、`AuditArchiveAction` 的结构化 archive 引用与 checksum、`AuditCleanupEvidence` 的 Manual/Scheduled trigger 与 archive-ref 绑定。
3. [docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome设计收敛.md](docs/todos/infrastructure/deliverables/AUD-BLK-002-RetentionOutcome%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已完成解阻，因此本轮不再缺 retention 输出对象与 cleanup trace 语义，剩余工作是把公共 header 和现有测试出口真正落盘。

## 2. 研究学习结果

### 2.1 本地证据

1. [infra/include/audit/IAuditLogger.h](infra/include/audit/IAuditLogger.h) 与 [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h) 已提供 audit public interface 的样式先例：对象定义放在 `dasall::infra`，接口类放在 `dasall::infra::audit`，并保持 header-only 边界。
2. [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp) 已是 `AuditInterfaceCompileTest` 的统一接口冻结出口，适合直接补 `IAuditRetention::apply_retention(now_ts)` 签名、虚析构与 success/failure retention outcome 断言。
3. [tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp) 已冻结 audit private error 到 `contracts::ResultCode` 的映射，适合继续验证 retention success/failure object 不新增跨-contract 错误码。

### 2.2 外部/上游约束

1. 本轮直接复用 `AUD-BLK-002` 已冻结的 OWASP/NIST 约束，不新增新的外部假设；实现只把已定稿的 cleanup trace 与 archive evidence 规则转成可编译 header。

### 2.3 可落地启发

1. 013 应保持 header-only，不引入新的 `.cpp` 或 manager 调度实现。
2. success/failure outcome 的一致性检查应直接收敛到 `RetentionOutcome` 方法中，避免未来 manager 实现绕过统一边界。
3. cleanup 证据的负例最适合放在 `AuditInterfaceCompileTest`，因为它直接验证公共对象的最小自校验语义。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 retention public header 边界 | audit 设计 6.6/6.6.2 | `IAuditRetention.h` | `apply_retention(now_ts)` 与 retention 对象可编译 |
| D2 | 冻结 retention outcome 自校验语义 | audit 设计 6.6.2 | `RetentionOutcome::has_consistent_state()` | completed/error_code、archive action、cleanup evidence 可二值验证 |
| D3 | 把 retention 边界接入现有 unit/contract 出口 | TODO 验收要求 | 测试扩展 | 定向 + audit gate 均通过 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| retention public boundary 保持 header-only | 新增 [infra/include/audit/IAuditRetention.h](infra/include/audit/IAuditRetention.h) |
| retention compile 出口继续复用现有 audit interface test | 扩展 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp) |
| retention error-code contract 继续复用现有 audit error mapping test | 扩展 [tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp) |

### 4.2 Build 三件套

1. 代码目标：新增 `IAuditRetention.h`，冻结 `AuditCleanupTrigger`、`AuditArchiveAction`、`AuditCleanupEvidence`、`RetentionOutcome` 与 `IAuditRetention::apply_retention(now_ts)`。
2. 测试目标：扩展 `AuditInterfaceCompileTest` 验证 retention interface 签名、虚析构、success/failure outcome 与 cleanup trace 负例；扩展 `InfraErrorCodeMappingContractTest` 验证 retention success/failure object 仍只映射既有 `contracts::ResultCode`；补跑完整 `ctest -L audit`。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_contract_infra_error_code_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`
   - `cmake --build build-ci --target dasall_audit_event_unit_test dasall_audit_logger_interface_unit_test dasall_audit_service_fallback_unit_test dasall_audit_export_filter_unit_test dasall_contract_audit_event_boundary_test dasall_contract_audit_logger_interface_boundary_test dasall_contract_audit_service_boundary_test dasall_contract_infra_error_code_boundary_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`

### 4.3 D Gate

结论：PASS。

理由：

1. `AUD-BLK-002` 已清除，retention 输出对象与 cleanup trace 语义都已固定。
2. 当前轮只补 public header 和现有测试出口，不越界进入 retention manager 执行层。

## 5. Build 落地结果

1. 新增 [infra/include/audit/IAuditRetention.h](infra/include/audit/IAuditRetention.h)，冻结 `AuditCleanupTrigger`、`AuditArchiveAction`、`AuditCleanupEvidence`、`RetentionOutcome` 与 `IAuditRetention::apply_retention(now_ts)` 边界，并将 completed/error_code、archive action、cleanup evidence 的一致性检查收敛到 header-only 对象方法。
2. 更新 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)，新增 retention interface compile/success/failure 断言，验证 single-entry boundary 与 cleanup trace 负例。
3. 更新 [tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp](tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp)，新增 retention success/failure object 仍只映射既有 `contracts::ResultCode` 的 contract 守卫。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_contract_infra_error_code_boundary_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`：发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraErrorCodeMappingContractTest"`：2/2 通过。
5. `ctest --test-dir build-ci -N -L audit`：发现 9 个测试。
6. `ctest --test-dir build-ci --output-on-failure -L audit`：9/9 通过。

## 7. 结论

1. `AUD-TODO-013` 已把 retention 公共接口从“设计冻结”推进到“header + unit/contract + audit gate”落盘完成。
2. 当前 audit 组件专项 TODO 列表内的原子任务已经全部完成；若继续推进，应另起 retention manager / archive backend / 自动清理调度的新任务范围。