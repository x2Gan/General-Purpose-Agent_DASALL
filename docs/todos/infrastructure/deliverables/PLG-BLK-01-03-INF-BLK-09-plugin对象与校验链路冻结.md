# PLG-BLK-01-03 / INF-BLK-09 plugin 对象与校验链路冻结

日期：2026-04-07  
阶段：Blocker Recovery -> Design Freeze  
关联阻塞：INF-BLK-09、PLG-BLK-01、PLG-BLK-02、PLG-BLK-03、PLG-BLK-05  
结论：**已解阻，可按 PLG-TODO-014 -> 015 -> 016 -> 017 串行推进**

---

## 1. 本轮目标

本轮不进入 public header 或运行骨架实现，只完成 shared blocker 解阻所需的设计冻结与台账回写：

1. 冻结 PluginManifest v1.0 字段集合、版本化规则与扩展命名空间。
2. 冻结 plugin 签名链路的 trust anchor 读取边界、允许算法、trust level 次序与 chain_status 集合。
3. 冻结 plugin ABI 兼容矩阵、platform tag 语义与 `infra.plugin.abi.strict_mode` 的影响范围。
4. 将 INF-BLK-09 从 infra/plugin 两级 TODO 台账迁移为 Resolved，并给出后续编码顺序。

## 2. 本地证据

1. docs/architecture/DASALL_infra_plugin模块详细设计.md 的 6.5/6.6 只给出 PluginManifest、SignatureReport、CompatibilityReport 与 verifier/compatibility engine 的高层字段，没有冻结到可编码的 schema/rule 级别。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 把 PLG-TODO-014~017 全部挂在 INF-BLK-09 下，且要求 manifest/signature/ABI 三项同步冻结后才允许恢复执行。
3. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md 仍把 INF-BLK-09 标为当前阻塞，说明 plugin 子域尚未完成体系级校准。
4. profiles 五档 runtime_policy 已在 PLG-TODO-013 中冻结 `infra.plugin.signature.required`、`infra.plugin.trust.min_level`、`infra.plugin.abi.strict_mode`，因此本轮只需补齐对象/规则语义，不必新增 profile key。
5. docs/architecture/DASALL_infrastructure子系统详细设计.md 已冻结 SecretManager 的只读 trust anchor 读取职责，足以为 plugin 签名链提供上游边界，而无需新增跨域 public API。

## 3. 外部参考

1. Semantic Versioning 2.0.0：公开 API 一旦发布，对应版本内容不得被原地修改；`MAJOR.MINOR.PATCH` 应明确表达 breaking change、backward compatible feature 与 bug fix 的差异。本轮据此冻结 `schema_version` 与 `required_abi` 的版本语义。
2. The Update Framework Security Guidance：trust 不能永久授予，必须支持 key rotation / revocation；客户端不得接受比已见版本更旧的元数据；签名验证应显式防御 rollback、freeze 与 mix-and-match 风险。本轮据此冻结 trust anchor 读取、chain_status 与 signature freshness 规则。

## 4. 冻结结论

### 4.1 PluginManifest v1.0

冻结字段集合：

1. `schema_version`：首版固定为 `1.0.0`，采用 SemVer 规则；同一已发布版本不得原地改写。
2. `plugin_id`：稳定插件标识，必须非空，采用小写 ASCII slug，允许 `.`、`-`、`_`。
3. `version`：插件包版本，采用 SemVer `MAJOR.MINOR.PATCH[-prerelease][+build]`。
4. `entry`：插件主入口符号或入口标识，必须非空且不允许空白字符。
5. `required_abi`：格式固定为 `<platform_tag>@<MAJOR>.<MINOR>.<PATCH>`，例如 `x86_64-linux-gnu@1.2.0`。
6. `capabilities`：唯一且非空的能力项集合，使用小写点分 token；不得复用 Tool/Skill contracts 语义。
7. `signature_ref`：签名元数据或签名工件引用；在 v1 默认 profile 基线下必须非空。
8. `extensions[]`：可选扩展字段，key 必须使用 `x.<owner>.<name>` 命名空间；保留前缀 `infra.`、`contracts.`、`profile.`、`runtime.`、`tool.`、`skill.`、`plugin.` 禁止复用。

### 4.2 签名链路与 trust policy v1

冻结规则：

1. plugin v1 只允许 `ed25519` 与 `ecdsa-p256-sha256` 两种签名算法；未知算法直接拒绝。
2. trust anchor 只通过 infra 已冻结的只读边界读取，目的值固定为 `plugin.package.verify`；plugin 不拥有 anchor 持久化、轮换或撤销职责。
3. trust level 顺序固定为 `untrusted < external < vendor < internal`；`infra.plugin.trust.min_level` 只允许在该序中上调门槛，不允许自定义枚举值。
4. `chain_status` 允许值固定为 `verified`、`anchor_missing`、`algorithm_unsupported`、`signature_invalid`、`certificate_expired`、`trust_level_too_low`、`rollback_rejected`。
5. 同一 `plugin_id` 的签名引用不得回退到已见更旧版本；rollback/freeze 类失败只通过 `reason_code`、审计与 evidence_ref 留痕，不扩写 contracts。

### 4.3 ABI 兼容矩阵 v1

冻结规则：

1. `platform_tag` 采用 GNU triplet 风格，首版允许 `x86_64-linux-gnu`、`aarch64-linux-gnu`、`armv7-linux-gnueabihf`。
2. host ABI 快照最小字段固定为 `platform_tag` 与 `abi_version`；`abi_version` 使用 SemVer `MAJOR.MINOR.PATCH`。
3. `infra.plugin.abi.strict_mode=true` 时：`platform_tag` 必须完全一致，`MAJOR` 与 `MINOR` 必须完全一致，host patch 必须大于等于 required patch。
4. `infra.plugin.abi.strict_mode=false` 时：`platform_tag` 必须完全一致，`MAJOR` 必须一致，host `MINOR.PATCH` 允许前向覆盖 plugin 所需版本。
5. 无论 strict mode 是否关闭，都禁止跨 `platform_tag` 激活，也禁止 `MAJOR` 版本不匹配时继续 load。
6. `api_ok` 先冻结为 runtime bridge handshake 预留位；在当前阶段它仍必须显式出现在 CompatibilityReport 中，但默认由 compatibility engine skeleton 给出稳定布尔结果，而不是由 manager 隐式推断。

## 5. Design -> Build 映射

1. PLG-TODO-014：新增 PluginManifest public header 与 unit/contract 守卫，验证 schema_version、required_abi、extensions namespace。
2. PLG-TODO-015：新增 IPluginSignatureVerifier public header，并冻结 signature/trust 相关最小输入输出对象与 compile/boundary tests。
3. PLG-TODO-016：新增 IPluginCompatibilityEngine public header，并冻结 host ABI snapshot / compatibility report 与 ABI matrix tests。
4. PLG-TODO-017：把 SignatureReport / CompatibilityReport 对象正式接入 plugin public boundary 与 validation aggregation tests。

## 6. Gate 结论

1. INF-BLK-09：PASS。manifest/signature/ABI 三项已在同一轮同步冻结，满足 plugin TODO 的解阻协调门。
2. PLG-GATE-08：本轮未触发。共享 blocker 解阻只改文档与台账，不改 public code signature；具体接口变化将在 014~017 各轮单独评审与提交。
3. 后续执行顺序固定为：PLG-TODO-014 -> PLG-TODO-015 -> PLG-TODO-016 -> PLG-TODO-017。

## 7. 验收命令

1. `rg -n "schema_version|required_abi|x\.<owner>|ed25519|ecdsa-p256-sha256|trust_level_too_low|rollback_rejected|strict_mode=false|platform_tag" docs/architecture/DASALL_infra_plugin模块详细设计.md docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md`
2. `rg -n "INF-BLK-09|PLG-TODO-014|PLG-TODO-015|PLG-TODO-016|PLG-TODO-017" docs/worklog/DASALL_开发执行记录.md docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md`

## 8. 结果

1. shared blocker 已由“设计缺口”收敛为“明确的对象与规则冻结”，后续编码轮次无需再反复补 schema/trust/ABI 口径。
2. plugin 专项 TODO 与 infra 总 TODO 现在都可以把 014~017 视为可执行任务，而不是继续停留在 Blocked。
3. 真正仍未解除的只剩 PluginRuntimeBridge 平台装载细节，这不会再阻塞 014~017 的对象/接口冻结任务。