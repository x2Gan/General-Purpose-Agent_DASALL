# SEC-TODO-012 SecretAuditBridge 审计桥收敛

日期：2026-04-04
任务：SEC-TODO-012
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-012 定义为“实现 SecretAuditBridge 审计桥骨架”，验收出口是事件完整性和 audit write fail 路径。
2. secret 详细设计 6.10.1 已在 SEC-BLK-004 中冻结 `audit::IAuditLogger` v1 sink 合同、SecretAuditEvent -> AuditEvent/AuditContext 字段映射，以及 success/degraded success/failure 判定语义。
3. SEC-TODO-003 与 SEC-TODO-011 已完成对象模型和错误码域冻结，因此本轮可以直接在 infra/secret 私有层落盘 bridge skeleton，而不改 public contracts。

## 2. 研究学习结果

### 2.1 本地证据

1. infra/include/audit/IAuditLogger.h 与 infra/include/audit/AuditTypes.h 已冻结 `write_audit(event, context)` 调用面，说明 012 的关键工作是 secret 侧映射与失败归一，而不是重造 audit sink 抽象。
2. SecretAuditEvent 已在 SecretTypes.h 中冻结 actor/action/target_secret/consumer_module/reason_code/version/request/task 字段，因此 bridge 需要把这些字段无损投影到 AuditEvent 和 AuditContext，而不是再引入 secret 私有副本。
3. tests/integration/infra/config/ConfigObservabilityIntegrationTest.cpp 已展示 RecordingAuditLogger / FailingAuditLogger 的仓库样板，适合直接复用到本轮 unit test 的 success / degraded success / hard failure 三路径。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secret access、rotation、fallback 和 access denied 等关键事件必须留下独立审计记录且失败不能静默吞掉，这支持 DASALL 把 bridge 失败统一映射到 `INF_E_SECRET_AUDIT_WRITE_FAIL`。
2. Azure Key Vault secrets best practices 建议 secret lifecycle events 与请求上下文联动留痕，这支持 DASALL 在 bridge 中显式保留 `request_id` / `task_id` / `consumer_module` 三类可追溯上下文。

### 2.3 可落地启发

1. bridge 需要一个通用 `emit_event` 内核，再用 `emit_access_granted` / `emit_access_denied` / `emit_rotate` / `emit_revoke` / `emit_fallback` 五个 wrapper 收敛动作名。
2. `AccessDenied` 和 `Fallback` 不能直接复用布尔 outcome；必须分别固定映射为 `AuditOutcome::Rejected` 与 `AuditOutcome::Escalated`。
3. secret audit health 的最小输入面可以先表现为 bridge 的 emitted_total / emit_failures / last_error_code status，留待 SEC-TODO-013 消费，不必在 012 同轮引入 IHealthMonitor。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 bridge 私有 emit 结果与状态模型 | secret 设计 6.10.1 | SecretAuditBridge.h | emitted / failure / status 都可二值判定 |
| D2 | 落盘 SecretAuditEvent 到 AuditEvent/AuditContext 的映射 | secret 设计 6.10.1 | SecretAuditBridge.cpp | action / outcome / side_effects / context 映射固定 |
| D3 | 固化 success / degraded success / hard failure 语义 | TODO 任务要求；SecretErrors | SecretAuditBridge.cpp | 非 success / degraded success 一律映射 `INF_E_SECRET_AUDIT_WRITE_FAIL` |
| D4 | 注册并验证 bridge 单测 | TODO 任务要求 | SecretAuditBridgeTest.cpp；CMakeLists | target 可被 ctest 发现且 1/1 通过 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| bridge 只依赖 `audit::IAuditLogger` | infra/src/secret/SecretAuditBridge.h；infra/src/secret/SecretAuditBridge.cpp |
| SecretAuditEvent -> AuditEvent/AuditContext 映射在 bridge 内收敛 | infra/src/secret/SecretAuditBridge.cpp |
| success / degraded success / hard failure 都要可验证 | tests/unit/infra/secret/SecretAuditBridgeTest.cpp |
| CMake 必须纳入 bridge 源码与 unit test discoverability | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 SecretAuditBridge 私有头/源，落盘通用 `emit_event`、五个动作 wrapper、status 跟踪，以及审计失败统一错误映射。
2. 测试目标：新增 SecretAuditBridgeTest，覆盖 access/rotate/revoke 完整性、access_denied/fallback 特殊 outcome 映射，以及 audit write fail 路径。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_audit_bridge_unit_test`
   - `ctest --test-dir build-ci -N -R SecretAuditBridgeTest`
   - `ctest --test-dir build-ci --output-on-failure -R SecretAuditBridgeTest`

### 4.3 D Gate

结论：PASS。

理由：

1. 012 的 blocker SEC-BLK-004 已解阻，且对象模型与错误码边界已冻结。
2. 本轮实现保持在 secret audit bridge 私有骨架边界内，不提前耦合 manager 主链、health probe 或 integration。

## 5. Build 落地结果

1. 新增 infra/src/secret/SecretAuditBridge.h，定义 `SecretAuditBridgeOptions`、`SecretAuditEmitResult`、`SecretAuditBridgeStatus` 和 bridge 主类。
2. 新增 infra/src/secret/SecretAuditBridge.cpp，落盘动作名映射、AuditOutcome 特殊规则、side_effects/context 投影、status 跟踪，以及 audit write failure 的 secret 错误码归一。
3. 新增 tests/unit/infra/secret/SecretAuditBridgeTest.cpp，覆盖 access/rotate/revoke 完整性、AccessDenied/ Fallback 特殊 outcome，以及 hard failure 路径。
4. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 bridge 源码和 unit test target 纳入构建图。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_audit_bridge_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R SecretAuditBridgeTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R SecretAuditBridgeTest`：通过，1/1 tests passed。

## 7. 结论

1. SEC-TODO-012 已把 secret audit 能力从“设计冻结但未编码”推进到“存在 IAuditLogger bridge + 字段映射 + failure 归一 + unit evidence 的可验证骨架”。
2. SecretAuditBridgeStatus 现在已具备 emitted_total / emit_failures / last_error_code 输入面，为后续 SEC-TODO-013 健康出口聚合提供稳定信号源。