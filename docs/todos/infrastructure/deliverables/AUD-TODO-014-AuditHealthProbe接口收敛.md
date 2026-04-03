# AUD-TODO-014 AuditHealthProbe 接口收敛

日期：2026-04-03  
任务：AUD-TODO-014  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-014` 定义为“定义 IAuditHealthProbe 接口与 AuditHealthStatus 对象”，验收要求是 `AuditInterfaceCompileTest` 与 `InfraAuditHealthIntegrationTest` 能共同验证接口边界和最小协同语义。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.5/6.6/6.6.1 已冻结 `AuditHealthStatus` 字段、三态、failure reason allowlist 与只读 `evaluate()` 语义。
3. `AUD-BLK-003` 已完成解阻，说明本轮不再缺设计上下文，剩余工作是把已冻结边界落成 public header，并给出可执行的最小验证出口。

## 2. 研究学习结果

### 2.1 本地证据

1. 当前 [infra/include/audit](infra/include/audit) 还没有 `IAuditHealthProbe.h`，因此 audit health 仍停留在设计层，尚无可编译的公共接口面。
2. [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp) 已承担 `AuditInterfaceCompileTest`，是冻结 `IAuditHealthProbe::evaluate()` 签名与 `AuditHealthStatus` 一致性守卫的最小出口。
3. [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 已具备根级 integration 注册能力，可像 `ConfigRuntimePatchIntegrationTest` 一样先落一个最小协同用例，再由后续 `AUD-TODO-018` 专门收口目录、标签与顶层聚合拓扑。
4. [infra/include/audit/AuditService.h](infra/include/audit/AuditService.h) 与现有 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 已暴露生命周期状态、`is_degraded()` 和主/降级计数，足以作为 test-local health probe 的只读信号来源。

### 2.2 外部参考

1. Kubernetes readiness/liveness probe 指南强调，readiness 应是低成本、持续评估且无副作用的运行态查询；当组件进入临时故障或停机状态时，探针应报告 degraded/unavailable，而不是在探针内部吸收恢复裁定。这与 audit health 的只读快照语义一致。

### 2.3 可落地启发

1. `IAuditHealthProbe` 首版只需要一个 `evaluate() const`，不应吸收 `IHealthProbe::probe()`、`register_probe()` 或任何恢复动作接口。
2. `AuditHealthStatus` 需要自带一致性守卫，防止 Ready 携带 failure bits、自由文本 failure reason 或 metrics bridge degraded 被错误升级到 `Unavailable`。
3. 为满足当前轮验收，可以先在 `tests/integration/infra/` 根级落一个最小 `InfraAuditHealthIntegrationTest`；`AUD-TODO-018` 再负责把它收口到 audit 专项目录、顶层 target 聚合与 `integration;audit` 标签。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 audit health 的 public interface 边界 | audit 设计 6.6/6.6.1 | `IAuditHealthProbe.h` | `evaluate() const -> AuditHealthStatus` 可编译且无多余职责 |
| D2 | 冻结 `AuditHealthStatus` 的状态/原因一致性守卫 | audit 设计 6.5/6.6.1 | `AuditHealthStatus::has_consistent_state()` | Ready/Degraded/Unavailable 与 reason allowlist 可二值判定 |
| D3 | 锁定当前轮可执行验收出口 | tests/integration/infra 现状；任务验收命令 | root-level `InfraAuditHealthIntegrationTest` | 定向发现与执行通过，且不提前关闭 018 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| `IAuditHealthProbe` 保持只读 evaluate 边界 | 新增 [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h) |
| `AuditHealthStatus` 自带状态/原因一致性守卫 | 扩展 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp) |
| 最小 integration ground truth 只验证健康协同语义，不收口拓扑 | 新增 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp) 与 root-level 注册 |

### 4.2 Build 三件套

1. 代码目标：新增 `IAuditHealthProbe.h`，并更新 [infra/CMakeLists.txt](infra/CMakeLists.txt) 与 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)。
2. 测试目标：扩展 `AuditInterfaceCompileTest`，并新增 `InfraAuditHealthIntegrationTest` 覆盖 Ready、fallback degraded、metrics bridge degraded、service stopped -> Unavailable 四类场景。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_infra_audit_health_integration_test`
   - `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`

### 4.3 D Gate

结论：PASS。

理由：

1. `AUD-BLK-003` 已清除，接口字段、状态机和失败原因边界都已可直接落盘。
2. 当前轮只补 public interface 与最小 integration ground truth，不扩张到 metrics bridge 实现或 audit integration 拓扑收口。

## 5. Build 落地结果

1. 新增 [infra/include/audit/IAuditHealthProbe.h](infra/include/audit/IAuditHealthProbe.h)，在 `dasall::infra` 中冻结 `AuditHealthState`、`AuditHealthStatus`、degraded/unavailable failure reason allowlist 和 `has_consistent_state()` 守卫，在 `dasall::infra::audit` 中定义只读 `IAuditHealthProbe::evaluate() const`。
2. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，将 `IAuditHealthProbe.h` 纳入 `DASALL_INFRA_AUDIT_PUBLIC_HEADERS`。
3. 更新 [tests/unit/infra/AuditLoggerInterfaceTest.cpp](tests/unit/infra/AuditLoggerInterfaceTest.cpp)，新增 `IAuditHealthProbe` 签名冻结、`AuditHealthStatus` 正负例断言与“不得吸收 `probe/register_probe` 职责”的静态校验。
4. 新增 [tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp](tests/integration/infra/audit/InfraAuditHealthIntegrationTest.cpp)，用 test-local `AuditServiceBackedHealthProbe` 组合 `AuditService` 的生命周期与 degraded 信号，验证 Ready、fallback degraded、metrics bridge degraded 与 stopped unavailable 四类协同场景。
5. 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt)，新增根级 `dasall_infra_audit_health_integration_test` 与 `InfraAuditHealthIntegrationTest` 的最小注册，为当前轮提供可执行验收出口。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_audit_logger_interface_unit_test dasall_infra_audit_health_integration_test`：通过。
3. `ctest --test-dir build-ci -N -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`：发现 2 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "AuditInterfaceCompileTest|InfraAuditHealthIntegrationTest"`：2/2 通过。

## 7. 结论

1. `AUD-TODO-014` 已从“健康状态对象只存在于设计文档”推进到“public interface、对象一致性守卫和最小 integration ground truth 均已可编译、可发现、可执行”。
2. 本轮只做了当前任务所需的根级 integration 注册；`AUD-TODO-018` 仍保留为后续目录/标签/顶层 target 聚合收口任务，不在本轮提前关闭。
