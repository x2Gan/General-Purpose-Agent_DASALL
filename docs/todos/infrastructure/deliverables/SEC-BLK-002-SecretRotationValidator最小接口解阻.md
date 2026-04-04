# SEC-BLK-002 SecretRotationValidator 最小接口解阻

日期：2026-04-04
任务：SEC-BLK-002
状态：解阻 PASS

## 1. 本地证据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 将 SEC-TODO-010 标记为 Blocked，根因是 dual-slot 轮换验证规则、`validation_required` 与 `grace_period_sec` 的最小语义尚未冻结。
2. docs/architecture/DASALL_infra_secret模块详细设计.md 6.8 在本轮之前只给出“轮换验证失败保留旧版本、回退失败返回明确错误码”的高层约束，但没有冻结 candidate version 推导、internal validator 输入/输出与 dual-slot 宽限窗口的执行规则。
3. 当前代码中 infra/src/secret/SecretManagerFacade.cpp 仍将 `rotate` 保持为 deferred failure，而 infra/src/secret/backends/MockSecretBackend.cpp 已具备最小 `promote_version` / `revoke_version` 能力，说明 010 的真实缺口是轮换协调器契约，而不是 backend 协议或 lease 基线。
4. docs/architecture/DASALL_infra_secret模块详细设计.md 6.9 已存在 `infra.secret.rotation.dual_slot_enabled`、`infra.secret.rotation.validation_required` 与 `infra.secret.rotation.grace_period_sec` 三个配置项，因此 blocker 的根因不是“缺配置项”，而是“配置项未映射为可编码的最小执行语义”。

## 2. 外部参考

1. OWASP Secrets Management Cheat Sheet 指出自动化轮换应采用显式的多阶段流程，例如 create / set / test / finish，并建议在轮换期间支持“新版本写、旧版本读”的渐进切换，同时要求 secret 生命周期具备 rotation、revocation、expiration 与 auditing 的明确证据。
2. Azure Key Vault secrets best practices 明确建议对零停机轮换使用 dual credentials，并在 secret rotation 后刷新缓存与监控生命周期事件；这支持 DASALL 在 dual-slot 模式下保留旧版本宽限窗口，同时把验证与切换证据显式建模，而不是把 promote/revoke 合并成不可观测的一步。

## 3. 阻塞修复与设计结论

阻塞结论：

1. SEC-BLK-002 已具备解阻条件。secret 详细设计 6.8.1 / 6.9 现已冻结 internal `ISecretRotationValidator` 的最小接口、candidate version 推导规则，以及 `validation_required` / `dual_slot_enabled` / `grace_period_sec` 的行为约束。

最小 blocker-fix：

1. 在 secret 详细设计新增 6.8.1，冻结 coordinator 必须构造的 `RotationValidationContext`、validator 最小返回值，以及 dual-slot / rollback 的时序规则。
2. 将 secret 专项 TODO 中的 SEC-BLK-002 改写为“已解阻（2026-04-04）”，并把 SEC-TODO-010 的 blocker 列迁移为已解阻说明。
3. 保持当前范围只做 blocker 设计收敛，不提前落盘 `SecretRotationCoordinator.cpp` 或 unit tests。

设计结论：

1. `RotationRequest` 继续保持冻结的 public interface，不追加 `candidate_version` 字段；候选版本由 coordinator 内部根据当前版本推导。
2. `ISecretRotationValidator` 只承担最小 validate-only 语义：判断候选版本是否可 promote，并产出 `reason_code` 与 `evidence_ref`；不吸收业务审批或外部策略引擎职责。
3. `validation_required=true` 时必须先验证再 promote；若显式关闭验证，也必须生成 `validation_skipped` 证据，不允许默默绕过验证链。
4. dual-slot 仅在 `dual_slot_enabled=true` 时可执行；若禁用 dual-slot，则请求必须显式失败，不能退化为 inplace promote。
5. dual-slot 且 `grace_period_sec>0` 时，旧版本先进入 rollback-ready 宽限窗口，宽限期后再 revoke；若宽限窗口为 0 或使用 inplace 策略，则 promote 后立即 revoke 旧版本。
6. 若 revoke 或收口阶段失败，coordinator 必须尝试 rollback；rollback 失败统一映射 `INF_E_SECRET_ROTATION_ROLLBACK_FAILED`，不得吞错或伪装为普通 backend failure。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `RotationValidationContext` 与 `ISecretRotationValidator` 最小接口 | infra/src/secret/SecretRotationValidator.h |
| 冻结 candidate version 推导与 validate-only 入口 | infra/src/secret/SecretRotationCoordinator.cpp；tests/unit/infra/secret/SecretRotationCoordinatorTest.cpp |
| 冻结 dual-slot 宽限窗口与 promote/revoke/rollback 顺序 | infra/src/secret/SecretRotationCoordinator.cpp；tests/unit/infra/secret/SecretRotationCoordinatorTest.cpp |
| 把 blocker 状态回链到 TODO 与执行日志 | docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md；docs/worklog/DASALL_开发执行记录.md |

## 5. 对 SEC-TODO-010 的直接交接

1. SEC-TODO-010 可以从“受 SEC-BLK-002 阻塞”转为“可执行”，并按已冻结的 internal validator / grace period 语义落盘 `SecretRotationCoordinator` 最小骨架。
2. 后续实现至少需要覆盖：
   - validate_only 成功路径；
   - validator 拒绝导致的 `INF_E_SECRET_ROTATION_VALIDATION_FAILED`；
   - dual-slot 关闭时的显式拒绝；
   - promote 后 immediate revoke 或 deferred revoke 的二值路径；
   - revoke 失败触发 rollback，以及 rollback fail 的 failure injection 路径。
3. 后续实现不得：
   - 修改 `RotationRequest` / `RotationResult` 的 public headers 以追加 candidate version 字段；
   - 把 dual-slot 请求静默降级为 inplace promote；
   - 在 rollback 失败时只返回 backend unavailable / not found，而不映射 rotation rollback 错误码。

## 6. 风险与回退

1. 若后续 010 实现忽略 `validation_required`、`dual_slot_enabled` 或 `grace_period_sec` 的冻结语义，本 blocker 需要重新打开。
2. 若后续轮换实现不生成 `evidence_ref` 或把 rollback failure 退化为普通 backend 错误，本 blocker 需要重新打开。
3. 本轮只解阻设计与 TODO 状态；真正的构建、单测和回退验证留给 SEC-TODO-010。