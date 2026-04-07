# OTA-TODO-021 OTA profile 键命名与覆盖优先级收敛

日期：2026-04-07
任务：OTA-TODO-021
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md](../DASALL_infrastructure_ota组件专项TODO.md) 将 OTA-TODO-021 定义为“补齐 ota profile 键命名与覆盖优先级收敛”，完成判定是配置键与覆盖次序冻结并可被实现引用。
2. 同文档将 `OTA-BLK-05` 定义为 `OTA-TODO-006`、`OTA-TODO-011` 的残余阻塞，解阻条件是 `infra.ota.*` 键命名冻结且跨 profile 一致。
3. [docs/architecture/DASALL_infra_OTA模块详细设计.md](../../architecture/DASALL_infra_OTA模块详细设计.md) 已有 `infra.ota.*` 配置表，但尚未明确 keyspace 分组、deployment/runtime override 准入边界，也未把五档 Profile 默认矩阵与现有 profile 资产基线对齐。
4. [docs/architecture/DASALL_profiles模块详细设计.md](../../architecture/DASALL_profiles模块详细设计.md) 与 [docs/architecture/DASALL_infra_config模块详细设计方案.md](../../architecture/DASALL_infra_config模块详细设计方案.md) 已冻结 `runtime_policy.yaml` v1 顶层逻辑域和四层配置模型；直接把 OTA 键塞进新的 `infra` 顶层会破坏既有 schema。
5. 现有五档 [profiles/cloud_full/runtime_policy.yaml](../../../../profiles/cloud_full/runtime_policy.yaml)、[profiles/desktop_full/runtime_policy.yaml](../../../../profiles/desktop_full/runtime_policy.yaml)、[profiles/edge_balanced/runtime_policy.yaml](../../../../profiles/edge_balanced/runtime_policy.yaml)、[profiles/edge_minimal/runtime_policy.yaml](../../../../profiles/edge_minimal/runtime_policy.yaml)、[profiles/factory_test/runtime_policy.yaml](../../../../profiles/factory_test/runtime_policy.yaml) 已冻结 `ops_policy.upgrade_strategy`，可作为 OTA rollout intent 的 profile 基线。

## 2. 外部参考

1. Spring Boot Externalized Configuration 明确要求 later property sources override earlier ones，且 YAML 层级配置最终会被展平成稳定 property key，这为“先冻结统一 keyspace，再定义明确覆盖顺序”提供了业界基线。
2. Twelve-Factor Config 强调配置应与代码分离、按 deploy 粒度正交管理，避免把不同 deploy 需求混成一组模糊“环境”；这支持 OTA 把 deployment override 与 runtime override 明确分治，并拒绝高风险临时 patch。

## 3. 设计收敛结论

1. OTA v1 keyspace 统一为 `infra.ota.*`，二级分组仅允许 `package`、`precheck`、`install`、`slot`、`rollback`、`repo_switch`、`audit`，根级键只保留 `enabled`、`mode`、`allow_downgrade`。
2. ConfigCenter 继续负责四层全局来源顺序，但 OTA 本地接受规则冻结为 `defaults < profile < deployment_override`；`runtime_override` 对 `infra.ota.*` 一律拒绝，不允许通过运行时 patch 放宽升级门槛、确认判据或审计要求。
3. OTA 不扩写 profiles v1 的 `runtime_policy.yaml` 顶层 schema；Profile 侧沿用 `ops_policy.upgrade_strategy` 表达 rollout intent，后续实现由 ConfigLoader/Adapter 将 OTA 默认矩阵投影为 typed config，而不是在 profile YAML 中再发明第二套 OTA 裸键。
4. `deployment_override` 的 allowlist 固定为 `infra.ota.enabled`、`infra.ota.mode`、`infra.ota.precheck.min_free_space_mb`、`infra.ota.precheck.max_cpu_load_pct`、`infra.ota.install.max_parallel_artifacts`、`infra.ota.slot.confirm_timeout_sec`、`infra.ota.rollback.token_ttl_sec`、`infra.ota.allow_downgrade`。
5. 安全关键键保持受保护：`infra.ota.package.verify_required`、`infra.ota.rollback.auto_on_confirm_fail`、`infra.ota.repo_switch.atomic_required`、`infra.ota.audit.required` 只能保持强约束，不允许 deployment/runtime 放宽；`infra.ota.package.signature_algorithm` 只允许在 `ed25519` 与 `ecdsa-p256-sha256` 内切换。
6. 五档 Profile 默认矩阵已冻结，覆盖 `enabled`、`mode`、`min_free_space_mb`、`confirm_timeout_sec`、`token_ttl_sec` 与 `max_parallel_artifacts`，并与现有 `ops_policy.upgrade_strategy` 对齐。

## 4. Design -> 阻塞解锁映射

| 阻塞点 | 设计补丁 | 结果 |
|---|---|---|
| `infra.ota.*` 只有散列表，没有命名规则 | 冻结统一前缀、二级分组、叶子命名规则与追加式演进约束 | 后续实现不再需要猜键名或维护别名 |
| OTA 配置表未说明四层来源下的组件本地接受规则 | 冻结 OTA 本地只接受 `defaults < profile < deployment_override`，并显式拒绝 `runtime_override` | 006/011 不再受运行时 patch 旁路影响 |
| profiles/config v1 顶层域与 OTA 键表存在冲突 | 保持 profile YAML 顶层 schema 不变，仅把 `ops_policy.upgrade_strategy` 作为 rollout intent 信号 | 不引入新的 profile schema 返工 |
| `OTA-BLK-05` 仍挂在 TODO 中 | TODO 与 architecture 同步回链解阻 | `OTA-TODO-006`、`OTA-TODO-011` 的残余设计歧义解除 |

## 5. Design -> Build 回链

1. `OTAPrecheckService` 读取 `infra.ota.enabled`、`infra.ota.mode`、`infra.ota.precheck.*` 与 `infra.ota.allow_downgrade`，不再从 profile 名称推断行为。
2. `PackageVerifier` 读取 `infra.ota.package.*`，并按 019 已冻结的算法/anchor 语义处理 verify fail。
3. `InstallExecutor` / `SlotSwitchCoordinator` 读取 `infra.ota.install.*`、`infra.ota.slot.*`、`infra.ota.repo_switch.*`；`ops_policy.upgrade_strategy` 只作为 rollout 节奏背景，不直接驱动 install 细节。
4. `BootConfirmationMonitor` / `RollbackController` 读取 `infra.ota.slot.confirm_timeout_sec`、`infra.ota.rollback.*` 与 `infra.ota.audit.required`；020 已冻结的 success/fail 判据不允许被 runtime patch 放宽。

## 6. 过程验证

1. 验收命令：
   - `rg -n "infra\.ota\.|runtime override|upgrade_strategy|OTA-BLK-05" docs/architecture/DASALL_infra_OTA模块详细设计.md docs/architecture/DASALL_profiles模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md profiles/**/runtime_policy.yaml`
2. 验证目标：
   - OTA 详细设计中存在统一 `infra.ota.*` keyspace、deployment override allowlist、runtime override 禁区与五档 Profile 默认矩阵；
   - Profiles 详细设计仍保持 `runtime_policy.yaml` v1 顶层逻辑域冻结，不需要为 OTA 引入新的 `infra` 顶层；
   - 五档 `runtime_policy.yaml` 已存在 `ops_policy.upgrade_strategy` 基线，可为 OTA rollout intent 提供 profile 侧证据；
   - `OTA-BLK-05` 与 `OTA-TODO-021` 已在 TODO 中完成回链。

## 7. 结论

1. `OTA-BLK-05` 已由本轮设计补丁解阻，`OTA-TODO-006` 与 `OTA-TODO-011` 的 profile/config 残余歧义已完成回链说明。
2. OTA v1 不再在 profile/runtime 配置面引入第二套键空间；后续实现必须围绕已冻结的 `infra.ota.*` typed keyspace 收敛，而不是直接在 `runtime_policy.yaml` 中增加新的顶层逻辑域。
3. OTA 组件专项 TODO 的 001~021 现已全部完成，后续若继续演进，应把 `UpgradePlan.target_scope` 与 repo_bound 原子指针职责归属转入新一轮设计评审，而不是继续阻塞 V1。