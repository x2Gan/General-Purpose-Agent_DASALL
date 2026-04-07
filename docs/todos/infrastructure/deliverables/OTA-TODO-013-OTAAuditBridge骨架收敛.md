# OTA-TODO-013 OTAAuditBridge 骨架收敛

日期：2026-04-07
任务：OTA-TODO-013
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-013 定义为“实现 OTAAuditBridge 骨架”，验收要求是高风险动作强制审计，且审计写失败必须显式可见。
2. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.11.4 冻结了 OTA 审计动作集合：`ota.precheck`、`ota.apply`、`ota.switch_boot_target`、`ota.mark_boot_success`、`ota.rollback`、`ota.freeze_apply_channel`；当前 013 的最小闭环只要求先补 `ota.precheck / ota.apply / ota.rollback`。
3. 同一设计文档要求审计字段必须包含 `actor / plan_id / package_id / target_scope / outcome / evidence_ref / rollback_id`，因此 013 必须把这些字段稳定投射到 `AuditEvent + side_effects`，而不是只写一个模糊 action。
4. OTA-TODO-012 已完成 RollbackController 骨架，因此 013 可以直接围绕 apply/rollback 的高风险动作补齐统一审计出口，而不必回退到 rollback token 设计层讨论。

## 2. 研究学习结果

### 2.1 本地证据

1. OTA 设计 6.11.4 已把 `ota.apply` 与 `ota.rollback` 标为高风险动作，这说明 013 不能把“无 logger 时静默跳过”当作可接受降级路径。
2. infra/audit 已冻结 `AuditEvent / AuditContext / AuditWriteOutcome` 和 `IAuditLogger`，因此 013 应复用现有审计边界，而不是新增 OTA 专属 public contract。
3. SecretAuditBridge、MetricsAuditBridge 等现有子系统都采用“内部事件对象 -> AuditEvent/AuditContext -> status/detail_ref”模式；013 延续这一路径最稳妥，也便于后续 014 和集成测试读取桥接状态。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 强调高风险动作必须留存 who/what/when/outcome 等关键字段，且日志/审计失败不能静默吞没；这与 DASALL 当前对 OTA 审计字段完整性和失败可见性的要求一致。
2. OpenTelemetry Logs Data Model 强调事件动作名、时间戳和上下文关联字段应保持稳定命名，这支持 013 把 `ota.precheck / ota.apply / ota.rollback` 固化为显式 action token，而不是动态拼装模糊文案。

### 2.3 可落地启发

1. OTAAuditBridge 的最小私有依赖只需要 `audit::IAuditLogger`，不应提前吞并 metrics/health 写入职责。
2. `plan_id / package_id / target_scope / rollback_id` 可以稳定承载到 `AuditEvent.side_effects`，而 `evidence_ref` 继续走已冻结的 `AuditEvidenceRef`。
3. precheck failure 更接近“拒绝执行”，应映射到 `AuditOutcome::Rejected`；rollback failure 属于 critical escalation，适合映射到 `AuditOutcome::Escalated`。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 OTA 审计私有事件与状态对象 | OTA 设计 6.11.4 | OTAAuditBridge.h | 记录对象、emit result、status 对象全部留在 ota 私有域 |
| D2 | 将 precheck/apply/rollback 映射到稳定审计 action | OTA 设计 6.11.4 | OTAAuditBridge.cpp | `ota.precheck / ota.apply / ota.rollback` 均可独立发审计 |
| D3 | 保证高风险动作缺失 logger 或写失败可见 | OTA TODO 013 验收要求 | OTAAuditBridge.cpp | missing logger / write failure 都返回 contract-shaped failure |
| D4 | 补足 unit/CMake 发现性 | OTA TODO 013 验收要求 | OTAAuditBridgeTest.cpp 与 infra/tests CMake | 新单测进入 `dasall_unit_tests` 与 `unit;ota` 标签 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| OTA 高风险动作必须走统一审计桥 | 新增 `OTAAuditBridge::write_precheck_audit / write_apply_audit / write_rollback_audit` |
| 审计字段必须完整可追溯 | 将 `plan_id / package_id / target_scope / rollback_id` 收敛为固定 `side_effects` |
| rollback failure 必须是更高等级观测 | rollback 失败映射到 `AuditOutcome::Escalated` |
| 审计 sink 缺失或写失败不可静默 | 统一映射 `INF_E_AUDIT_WRITE_FAIL` 并更新 bridge status |

### 4.2 Build 三件套

1. 代码目标：新增 OTAAuditBridge internal 骨架，实现 `write_precheck_audit / write_apply_audit / write_rollback_audit`。
2. 测试目标：新增 OTAAuditBridgeTest，覆盖完整事件、precheck/apply/rollback 失败 outcome 映射、missing logger 和 write failure 两类负例。
3. 验收命令：
   - `cmake -S . -B build-ci`
   - `cmake --build build-ci --target dasall_ota_audit_bridge_unit_test`
   - `ctest --test-dir build-ci -N -R "OTAAuditBridgeTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "OTAAuditBridgeTest"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. 013 只收敛 OTA 私有审计桥，不改 public headers，也不跨到 runtime 的恢复裁定或 health 判定职责。
2. 012 已提供 rollback 语义锚点，013 现在只需保证 apply/rollback 的审计统一出口，不受未完成的 011/014 约束。

## 5. Build 落地结果

1. 新增 infra/src/ota/OTAAuditBridge.h 与 infra/src/ota/OTAAuditBridge.cpp，冻结 `OTAAuditRecord`、`OTAAuditEmitResult`、`OTAAuditBridgeStatus` 与 bridge options，保持在 ota 私有域。
2. `OTAAuditBridge` 现在显式提供三个入口：`write_precheck_audit`、`write_apply_audit`、`write_rollback_audit`；三者统一输出 `AuditEvent + AuditContext + AuditWriteOutcome`，并维护 bridge detail/status。
3. `ota.precheck` failure 映射到 `AuditOutcome::Rejected`，`ota.apply` failure 映射到 `AuditOutcome::Failed`，`ota.rollback` failure 映射到 `AuditOutcome::Escalated`，使不同高风险路径在审计层可区分。
4. 审计 sink 缺失或 sink 返回失败写入结果时，bridge 统一映射到已冻结的 `INF_E_AUDIT_WRITE_FAIL` outward category，并保留 message/stage/source_ref 细节，不扩写 contracts。
5. 新增 tests/unit/infra/ota/OTAAuditBridgeTest.cpp，覆盖完整事件、outcome 映射、mandatory audit sink 与 sink write failure 两类负例。
6. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt 与 tests/unit/infra/CMakeLists.txt，将 OTAAuditBridge 源码和 OTAAuditBridgeTest 接入 `dasall_infra`、`dasall_unit_tests` 与 `unit;ota` 标签。

## 6. Build 合规复核

1. 边界：013 只桥接审计，不引入新的 health/metrics 判定逻辑，也不回写 public OTA contract。
2. 根因处理：把“高风险动作没有统一审计出口”和“审计写失败不可见”收敛到 OTAAuditBridge 自身，而不是留给调用方散落补丁。
3. 测试覆盖：正例覆盖 precheck/apply/rollback 三条主路径；负例覆盖 missing logger 与 sink write failure；同时显式验证 precheck/apply/rollback 的差异化 outcome 映射。
4. CMake：新单测已加入 `dasall_unit_tests` 聚合目标，并通过 `ctest -N` 被发现。
5. 兼容性：未修改任何 public OTA header，后续 014 或 integration 任务可以直接消费 bridge status/detail_ref，而无需回改 013。

## 7. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_ota_audit_bridge_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R "OTAAuditBridgeTest"`：发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R "OTAAuditBridgeTest"`：通过，1/1 tests passed。
5. `cmake --build build-ci --target dasall_unit_tests`：通过。
6. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，169/169 tests passed。

## 8. 结论

1. OTA-TODO-013 已把 OTA 审计出口从“设计约束”推进为“可执行骨架”，高风险的 precheck/apply/rollback 现在都有统一、可验证的审计动作名和字段映射。
2. 审计 sink 缺失或写失败不再被静默吞没，而会转成 contract-shaped failure 与 bridge degraded 状态，为后续 014 和 integration 门提供直接观测输入。
3. 下一轮若继续推进观测与健康出口，应先处理 020 -> 011 的解阻链，再进入 014，把 pending_confirm / backlog / last_failure 信号收敛为 OTAHealthProbe。