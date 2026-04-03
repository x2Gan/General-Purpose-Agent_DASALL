# AUD-TODO-011 AuditServiceFacade 骨架收敛

日期：2026-04-03  
任务：AUD-TODO-011  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-011` 定义为“实现 AuditServiceFacade 入口骨架”，完成标准是 `write_audit` 主链路能够串起 validator/pipeline/fallback，并保持返回结果可二值判定。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.4/6.7 已冻结 `AuditServiceFacade` 的职责：作为审计入口，负责生命周期管理、统一错误映射和对子组件的串接，但不越权进入 exporter、retention、metrics、health 或 runtime 恢复裁定。
3. `AUD-TODO-008`、`AUD-TODO-009`、`AUD-TODO-010` 已分别完成 validator、主写 pipeline、fallback pipeline 骨架，011 的最小缺口集中在“统一入口和生命周期管理仍堆在 `AuditService` 自身实现上”。

## 2. 研究学习结果

### 2.1 本地证据

1. audit 设计的依赖顺序已经固定为 `AuditServiceFacade -> AuditValidator -> AuditPipeline -> AuditFallbackPipeline`，011 应只负责把这条链收敛成统一入口，而不是继续扩到 exporter/retention。
2. `AuditServiceBoundaryContractTest` 已验证 `AuditService` 仍然是对外边界；011 必须在 facade 化后保持 public API 稳定，不把 internal helper 泄露到 public header。
3. `InfraErrorCodeMappingContractTest` 已冻结 audit 私有码域与 contracts 映射，因此 facade 化不能破坏既有 `AuditErrorCode` 映射结果。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 建议实现应用范围内可复用、可测试的统一日志处理模块，并要求日志故障不应被静默吞掉或阻塞应用的其他运行路径；这支持把 audit 的 orchestrate 逻辑集中到一个内部 facade，并继续保留结构化失败返回。

### 2.3 可落地启发

1. 对外类 `AuditService` 可以保持不变，但内部状态和 orchestrate 逻辑应收敛到 internal `AuditServiceFacade`，这样 public API 稳定，内部职责清晰。
2. facade 化不应引入新的 public method 或破坏既有构造/拷贝调用方式；若引入 pimpl，需要补回深拷贝语义以避免无意义的表面 breaking change。
3. 生命周期状态与写入前置失败应通过同一入口返回可判定结果，便于后续 build/test 接线和边界测试继续复用。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 internal facade 的状态与生命周期边界 | audit 设计 6.2/6.4/6.7 | internal `AuditServiceFacade` | facade 统一管理 config、lifecycle、record store 与 degraded 状态 |
| D2 | 收敛 write/export 的统一入口和错误映射 | audit 设计 6.3/6.7；AuditErrors | facade orchestrate 流程 | `write_audit` 串起 validator/pipeline/fallback，返回结果仍可二值判定 |
| D3 | 保持 public API 稳定并锁定 Build 三件套 | public `AuditService` 头文件；现有 unit/contract 测试面 | 代码目标、测试目标、验收命令 | facade 化后 public header 不扩张，且 build/test 出口明确 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| 对外仍保留 `AuditService`，内部状态和 orchestration 收到 facade | 更新 [infra/include/audit/AuditService.h](infra/include/audit/AuditService.h) 与 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，让 `AuditService` 变成 thin wrapper，internal `AuditServiceFacade` 持有状态 |
| lifecycle / write gate / export selection 都通过 internal facade 统一处理 | `AuditService.cpp` 内新增 `AuditServiceFacade`，集中 `init/start/stop/write_audit/export_audit` |
| facade 化后保持 contracts 映射和 service 边界稳定 | 继续运行 `InfraErrorCodeMappingContractTest` 与 `AuditServiceBoundaryContractTest` |
| 011 要补 lifecycle 管理证据 | 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，新增 lifecycle/write gate 回归测试 |

### 4.2 Build 三件套

1. 代码目标：在 `AuditService.cpp` 中新增 internal `AuditServiceFacade` 并让 `AuditService` 委托其处理生命周期、write/export 链路和错误映射；同时更新 public `AuditService.h` 以持有 facade 指针并保留深拷贝语义。
2. 测试目标：扩展 `AuditServiceFallbackTest` 覆盖 lifecycle state 与 pre-start write gate；执行 `InfraErrorCodeMappingContractTest` 与 `AuditServiceBoundaryContractTest` 回归，验证 facade 化未破坏错误映射和 service 边界。
3. 验收命令：
   - `cmake --build build-ci --target dasall_infra dasall_audit_service_fallback_unit_test dasall_contract_infra_error_code_boundary_test dasall_contract_audit_service_boundary_test`
   - `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest"`
   - `ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" --output-on-failure`

### 4.3 D Gate

结论：PASS。

理由：

1. facade 的 internal 边界、生命周期职责和 build/test 出口都已明确，不依赖额外 blocker。
2. 本轮只收口统一入口，不扩张到 exporter/retention/metrics/health。
3. public `AuditService` 继续保持原有方法集合，且深拷贝语义已补回，避免表面 breaking change。

## 5. Build 合规提醒

1. `AuditServiceFacade` 只允许存在于 `AuditService.cpp` 内部，不得泄露为新的 public contract。
2. 测试至少覆盖 1 个正例和 1 个负例；本轮正例为生命周期状态推进，负例为 pre-start write gate 和既有 fallback/exhaustion/error-mapping 回归。
3. public header 若引入 pimpl，必须保留现有调用面并避免无意义 breaking change；本轮通过 deep-copy clone 保持拷贝语义。

## 6. Build 落地结果

1. 更新 [infra/include/audit/AuditService.h](infra/include/audit/AuditService.h)，将 `AuditService` 收敛为 thin wrapper，持有 internal `AuditServiceFacade` 指针，并显式提供构造、析构、拷贝、移动语义。
2. 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，新增 internal `AuditServiceFacade`，统一持有 config、lifecycle、primary/fallback record store、degraded 状态与 validator，并集中处理 `init/start/stop/write_audit/export_audit`。
3. `AuditServiceFacade::write_audit()` 已明确串起 `AuditValidator -> AuditPipeline -> AuditFallbackPipeline`，并复用既有 `AuditErrorCode` 映射；`export_audit()` 继续通过统一入口处理 query 校验与 record 选择。
4. 更新 [tests/unit/infra/AuditServiceFallbackTest.cpp](tests/unit/infra/AuditServiceFallbackTest.cpp)，新增 lifecycle state 与 pre-start write gate 回归测试。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest"`：发现 3 个定向测试。
2. `ctest --test-dir build-ci -R "AuditServiceFallbackTest|InfraErrorCodeMappingContractTest|AuditServiceBoundaryContractTest" --output-on-failure`：3/3 通过。
3. `dasall_infra`、`dasall_audit_service_fallback_unit_test`、`dasall_contract_infra_error_code_boundary_test`、`dasall_contract_audit_service_boundary_test` 定向构建通过。

## 8. 结论

1. `AUD-TODO-011` 已从“service 自身直接背负全部状态和 orchestration”推进到“internal AuditServiceFacade 统一入口骨架已落盘并可回归验证”。
2. public `AuditService` API 没有扩张，且通过 clone 保留了拷贝语义；unit/contract 回归表明 facade 化未破坏 service 边界或错误映射。
3. `AUD-TODO-008` 到 `AUD-TODO-011` 的主链路骨架已全部完成；下一阶段应进入 `AUD-TODO-016` 与 `AUD-TODO-017`，把 audit 源码接线与测试发现性正式收口。