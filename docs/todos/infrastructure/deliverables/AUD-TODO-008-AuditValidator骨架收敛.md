# AUD-TODO-008 AuditValidator 骨架收敛

日期：2026-04-03  
任务：AUD-TODO-008  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md) 已将 `AUD-TODO-008` 定义为“实现 AuditValidator 字段校验骨架”，完成标准是把必填字段缺失、越权边界漂移和非法时间窗都收敛为可判定失败。
2. [docs/architecture/DASALL_infra_audit模块详细设计.md](docs/architecture/DASALL_infra_audit模块详细设计.md) 6.2/6.3/6.7/6.8 已冻结 `AuditValidator` 的职责边界：只负责字段完整性、边界语义和输入拒绝，不承担主写、fallback、恢复裁定或导出执行。
3. [docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md](docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md)、[docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md](docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md)、[docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md](docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md) 要求 audit 只记录事实边界，不越权进入上下文装配、恢复裁定或全局主控。
4. 当前仓库现状显示 `AuditService` 仍把字段校验、主写和 fallback 逻辑塞在同一个实现里；`AuditValidator` 尚未落盘，正是 008 的最小可执行缺口。

## 2. 研究学习结果

### 2.1 本地证据

1. audit 详细设计已明确 `AuditValidator -> AuditPipeline -> AuditFallbackPipeline -> AuditServiceFacade` 的串行主链顺序，008 只应完成第一环。
2. `AuditEvent` / `AuditContext` / `ExportQuery` / `AuditErrors` 已在 001~007 中冻结，008 不需要新增 public object，只需要把既有 guard 汇聚成统一 validator 出口。
3. 现有 `AuditServiceFallbackTest` 已覆盖 service 层主写与 fallback 行为，因此 008 的改动必须保持这一回归用例继续通过。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 建议把输入校验失败作为显式可观测事件处理，并要求审计链路记录 who/what/when/outcome，同时保证日志故障或拒绝路径不会被静默吞掉。
2. OpenTelemetry Logs Data Model 强调：频繁出现且语义稳定的字段应保留为类型化顶层字段，而不是退回成模糊文本；这支持把 event/context/query 继续保持为冻结类型，并让 validator 只做类型化字段校验而非文本解析。

### 2.3 可落地启发

1. `AuditValidator` 应直接复用冻结对象上的 guard，而不是重新发明一套平行 schema。
2. write 与 export 输入校验都应复用统一结果类型，方便后续 `AuditServiceFacade` 串接并统一映射 `AuditErrorCode`。
3. validator 应保持 internal，可被 service 调用，但不新增 public contract 或额外 runtime 依赖。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 validator 的 internal 输入/输出边界 | audit 设计 6.2/6.3/6.8 | `AuditValidationResult`、`AuditValidator` | validator 只负责输入校验，不承接主写或 fallback |
| D2 | 收敛 write / export 的最小失败分类 | audit 设计 6.7/6.8；AuditErrors | internal 校验失败映射 | 必填字段缺失、越权 evidence、非法时间窗都可判定失败 |
| D3 | 锁定 Build 三件套 | 008 任务行；tests 现状 | 代码目标、测试目标、验收命令 | 进入 Build 前已有明确验证出口 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| validator 保持 internal，不改 public audit contract | 新增 [infra/src/audit/AuditValidator.h](infra/src/audit/AuditValidator.h) 与 [infra/src/audit/AuditValidator.cpp](infra/src/audit/AuditValidator.cpp) |
| write 输入校验统一为字段/边界/side effects/context 四类守卫 | [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 改为委托 `AuditValidator::validate_write_input()` |
| export query 非法时间窗单独收敛为 validator 入口 | [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp) 改为委托 `AuditValidator::validate_export_query()` |
| 008 不新增新的 ctest 名称，只在既有 unit 面补 validator 正负例 | [tests/unit/infra/AuditTypesTest.cpp](tests/unit/infra/AuditTypesTest.cpp) 增补 validator 正例、缺字段负例、边界漂移负例、非法时间窗负例 |

### 4.2 Build 三件套

1. 代码目标：新增 `infra/src/audit/AuditValidator.h/.cpp`，并把 `AuditService` 改为通过 validator 统一做 write/export 输入校验。
2. 测试目标：扩展 `AuditTypesTest` 覆盖 validator 的正负例；保持 `AuditBoundaryContractTest` 与 `AuditServiceFallbackTest` 回归通过。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_audit_event_unit_test dasall_contract_audit_event_boundary_test dasall_audit_service_fallback_unit_test`
   - `ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"`
   - `ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest|AuditServiceFallbackTest" --output-on-failure`

说明：专项 TODO 基线命令写的是 Ninja，但当前仓库 `build-ci` 已锁定为 Unix Makefiles；本轮沿用现有生成器完成验证，属于构建目录状态差异，不是任务 blocker。

### 4.3 D Gate

结论：PASS。

理由：

1. validator 的 internal 边界、失败分类和 build 出口都已明确，不依赖额外 blocker。
2. 本轮范围仍限制在 `infra/src/audit`、`tests/unit/infra`、专项 TODO/交付物/worklog，不扩张到 pipeline/fallback/facade 后续任务。
3. public audit types、IAuditLogger 和 contracts 映射未发生 breaking 变化。

## 5. Build 合规提醒

1. validator 只能做输入校验，不得偷偷实现主写、fallback 或导出逻辑。
2. 测试至少覆盖 1 个正例和 1 个负例；本轮正例为合法 write/export 输入，负例覆盖缺字段、边界漂移和非法时间窗。
3. 本轮为了让 internal header 可被 unit 引用，只给 `dasall_audit_event_unit_test` 增加 `infra/src` include path，不改全局 include 规则。
4. `infra/CMakeLists.txt` 仅做最小 source 接线以支撑本轮验证；`AUD-TODO-016` 的“完整 audit 源码接线收敛”仍保持后续任务身份，不在本轮关闭。

## 6. Build 落地结果

1. 新增 [infra/src/audit/AuditValidator.h](infra/src/audit/AuditValidator.h) 与 [infra/src/audit/AuditValidator.cpp](infra/src/audit/AuditValidator.cpp)，定义 internal `AuditValidationResult` 与 `AuditValidator`，统一收敛 write/export 输入校验。
2. 更新 [infra/src/audit/AuditService.cpp](infra/src/audit/AuditService.cpp)，将原先内联在 `write_audit()` / `export_audit()` 中的输入校验切到 validator，保持 service 只消费统一的校验结论。
3. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)，把 `AuditValidator.cpp` 纳入 `dasall_infra` 构建图，以保证 validator 真正参与编译验证。
4. 更新 [tests/unit/infra/AuditTypesTest.cpp](tests/unit/infra/AuditTypesTest.cpp) 与 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)，为现有 `AuditTypesTest` 增加 validator 正负例，并给该 test target 增补 `infra/src` include path 以访问 internal header。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "AuditTypesTest|AuditBoundaryContractTest"`：发现 2 个定向测试。
2. `ctest --test-dir build-ci -R "AuditTypesTest|AuditBoundaryContractTest|AuditServiceFallbackTest" --output-on-failure`：3/3 通过。
3. `dasall_infra`、`dasall_audit_event_unit_test`、`dasall_contract_audit_event_boundary_test`、`dasall_audit_service_fallback_unit_test` 定向构建通过。

## 8. 结论

1. `AUD-TODO-008` 已从“校验逻辑散落在 service 中”推进到“internal validator 骨架已落盘并具备正负例验证”。
2. validator 已覆盖缺字段、边界漂移与非法时间窗三类输入异常，满足 008 的完成判定。
3. 下一顺位任务应进入 `AUD-TODO-009`，把主写 append-only 路径从 `AuditService` 继续拆分到独立 `AuditPipeline`。