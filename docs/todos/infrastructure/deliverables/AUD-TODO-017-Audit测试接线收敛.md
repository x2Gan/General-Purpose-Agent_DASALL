# AUD-TODO-017 Audit 测试接线收敛

日期：2026-04-03  
任务：AUD-TODO-017  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 将 `AUD-TODO-017` 定义为“注册 audit 的 unit 与 contract 测试入口”，完成标准是 audit 测试可被 `ctest -N` 发现并稳定执行。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 8.1/9.1 给出 audit 测试目录建议与测试矩阵，要求类型、接口、服务骨架和 contract/error mapping 都具备可追踪测试出口。
3. `AUD-TODO-016` 已完成 audit 源码构建接线，但当前 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)、[tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 仍存在 audit 测试混入 logging 分组、顶层聚合分散的问题。

## 2. 研究学习结果

### 2.1 本地证据

1. `AuditTypesTest` 与 `AuditInterfaceCompileTest` 仍沿用历史 `logging` 标签，`AuditServiceFallbackTest` 与 `AuditExportFilterTest` 只带 `unit` 标签，导致 audit discoverability 不一致。
2. [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt) 把 `dasall_audit_event_unit_test`、`dasall_audit_logger_interface_unit_test` 放进 `DASALL_LOGGING_UNIT_TEST_EXECUTABLE_TARGETS`，而另外两个 audit unit target 落在通用 unit 列表，顶层聚合边界不清晰。
3. [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 中 `AuditBoundaryContractTest` 和 `AuditLoggerInterfaceBoundaryContractTest` 仍经由 logging helper 注册，`AuditServiceBoundaryContractTest` 与 `InfraErrorCodeMappingContractTest` 则没有 audit discoverability 标签。

### 2.2 外部参考

1. CTest 的 label 过滤机制适合在不重排整体 target 拓扑的前提下，为模块建立稳定 discoverability 入口；只要标签边界清晰，模块级验证就可以和全量 `unit/contract` 门禁并存。

### 2.3 可落地启发

1. 017 的核心是显式收敛注册入口和标签语义，而不是强行搬迁现有测试源码目录。
2. 最小闭环是：为 audit unit/contract 建独立注册 helper，顶层 unit target 列表引入 audit 分组，并让 `ctest -L audit` 同时覆盖 4 个 unit 和 4 个 contract 测试。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 audit unit 测试注册语义 | tests/unit 现状；设计 9.1 | `dasall_register_audit_unit_test` 与 audit unit target 分组 | 4 个 audit unit 测试不再挂靠 logging 或散落通用列表 |
| D2 | 冻结 audit contract 测试注册语义 | tests/contract 现状；设计 9.1 | `dasall_register_audit_contract_test` | audit contract 测试不再通过 logging helper 暗接 |
| D3 | 锁定测试 discoverability 出口 | 编码规范 3.7；TODO 验收命令 | `ctest -N`、`ctest -L unit`、`ctest -L contract`、`ctest -L audit` 证据 | audit 测试既能被全局门禁发现，也能被模块标签直接运行 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| audit unit 测试从历史 logging 路径剥离 | 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，新增 `dasall_register_audit_unit_test` 并统一设置 `unit;audit` 标签 |
| 顶层 unit target 聚合要显式表达 audit 边界 | 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，新增 `DASALL_AUDIT_UNIT_TEST_EXECUTABLE_TARGETS` |
| audit contract 测试不再复用 logging helper | 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，新增 `dasall_register_audit_contract_test` 并统一设置 `contract;smoke;audit` 标签 |

### 4.2 Build 三件套

1. 代码目标：更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)、[tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，收敛 audit unit/contract 注册入口与顶层聚合边界。
2. 测试目标：确保 `AuditTypesTest`、`AuditInterfaceCompileTest`、`AuditServiceFallbackTest`、`AuditExportFilterTest`、`AuditBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`AuditServiceBoundaryContractTest`、`InfraErrorCodeMappingContractTest` 可被全局门禁与 audit 标签同时发现。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci -N -L audit`
   - `ctest --test-dir build-ci --output-on-failure -L audit`

说明：专项 TODO 基线命令写的是 Ninja，但当前 `build-ci` 已锁定为 Unix Makefiles，本轮沿用现有生成器验证，不构成 blocker。

### 4.3 D Gate

结论：PASS。

理由：

1. 017 的目标是注册与 discoverability 收敛，不依赖新增测试实现或目录搬迁。
2. audit 测试从 logging 历史路径中剥离后，既保留全局 unit/contract 门禁，也新增模块级标签出口。

## 5. Build 落地结果

1. 更新 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，新增 `dasall_register_audit_unit_test`，并将 `AuditTypesTest`、`AuditInterfaceCompileTest`、`AuditServiceFallbackTest`、`AuditExportFilterTest` 统一注册为 `unit;audit`。
2. 更新 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)，新增 `DASALL_AUDIT_UNIT_TEST_EXECUTABLE_TARGETS`，把 audit unit targets 从 logging 与通用列表的混合状态中抽出。
3. 更新 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt)，新增 `dasall_register_audit_contract_test`，将 `AuditBoundaryContractTest`、`AuditLoggerInterfaceBoundaryContractTest`、`AuditServiceBoundaryContractTest`、`InfraErrorCodeMappingContractTest` 收敛到 `contract;smoke;audit`。

## 6. 验证结果

1. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过。
2. `ctest --test-dir build-ci -N`：发现总计 254 个测试，其中 audit 相关测试均可见。
3. `ctest --test-dir build-ci --output-on-failure -L unit`：112/112 通过；标签摘要中 `audit = 4 tests`。
4. `ctest --test-dir build-ci --output-on-failure -L contract`：132/132 通过；标签摘要中 `audit = 4 tests`。
5. `ctest --test-dir build-ci -N -L audit`：发现 8 个测试，覆盖 4 个 unit + 4 个 contract。
6. `ctest --test-dir build-ci --output-on-failure -L audit`：8/8 通过。

## 7. 结论

1. `AUD-TODO-017` 已将 audit 测试从“可执行但分散在 logging/通用路径”推进到“拥有显式 audit 注册入口、顶层聚合分组和独立 discoverability 标签”。
2. 本轮未强行搬迁测试源码目录，符合设计 8.1 的“落盘建议”定位，也避免为接线任务引入无收益的路径级重构。