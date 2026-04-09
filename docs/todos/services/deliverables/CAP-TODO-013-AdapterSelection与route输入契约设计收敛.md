# CAP-TODO-013 AdapterSelection 与 route 输入契约设计收敛

日期：2026-04-09  
任务：CAP-TODO-013  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 已明确 `AdapterRouter` 的职责是基于 policy、capability、target、trust 与 availability 选择后端适配器，但在本轮之前缺少 supporting objects 与 source owner 的成表定义。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.4 已冻结 local_platform、local_service、remote_service 三类 route_kind 的语义边界，因此 route contract 必须保证 fallback 只发生在语义等价范围内，不能把 remote 与 local 强行互换。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.9.1 已把 RuntimePolicySnapshot / BuildProfileManifest 到 ServicePolicyView 的派生责任放在模块内部，说明 trust / availability / fallback 相关输入必须复用既有 owner，而不能由 Tool 请求直接覆盖。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 8.3 与 9.4 把 route 输入契约缺失列为 AdapterRouter、SystemSnapshotLane 与 execution/data 深链路的前置 blocker，因此本轮必须把 `CapabilitySnapshotView`、`AdapterSelection`、`FallbackEnvelope` 和 Route Contract Gate 一起落表。

## 2. 外部参考

1. Azure Architecture Center 的 Gateway Routing pattern 强调路由决策应由集中式网关根据健康度、版本和目标能力完成，而不是让客户端直接指定后端；这支持本任务把 route 输入 owner 收束到 ServiceConfigAdapter、ServiceHealthProbe 与 Runtime，下游 Tool 不得直接指定 `adapter_id`、trust override 或 fallback hop。
2. OWASP Authorization Cheat Sheet 强调逐请求授权、deny by default 与避免客户端可控的敏感策略字段；这支持本任务把 trust / availability 缺失或低置信度场景固定为 fail-closed，并明确任何 Tool 传入的 override 都视为越权输入。

## 3. Design 结论

1. `CapabilitySnapshotView`、`AdapterSelection` 与 `FallbackEnvelope` 全部保持 internal-only；它们不进入 ServiceTypes，也不新增 `services.*` profile schema。
2. route 输入只允许来自请求 target、模块内 capability snapshot、ServicePolicyView、ServiceHealthProbe 与 Runtime 给定的 fallback envelope；Tool 不能直接指定 `adapter_id`、`trust_class`、`availability_state` 或 fallback hop。
3. `CapabilitySnapshotView` 的 owner 固定为 ServiceConfigAdapter，来源是 BuildProfileManifest、adapter registration 与 runtime probe；不允许从 `target_id` 字符串推断隐藏 capability。
4. `trust_class` 由 adapter registration 信任级别、`caller_domain_allowlist` 与 action class 约束共同决定；`availability_state` 由 ServiceHealthProbe、adapter probe、circuit state 与 timeout budget 提供，缺失时必须 fail-closed。
5. `FallbackEnvelope` 只能由 Runtime 与 Tool Policy Gate 给出；services 只能消费并收紧，不能追加 hop、跨出 `route_equivalence_class`，也不能放宽 `allow_degrade`。
6. Router 的 fail-closed 规则固定为：capability 不支持则拒绝、超出 envelope 则返回 `fallback_blocked`、availability 只能剔除或降序候选、trust / caller_domain 不一致时必须拒绝选择。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `AdapterSelection` 字段与 route 输入 owner | AdapterRouter 的 select_adapter 求值入口 |
| 固定 `CapabilitySnapshotView` 来源 | ServiceConfigAdapter 与 adapter registration / probe 汇聚逻辑 |
| 固定 trust / availability 的 owner 与 fail-closed 规则 | AdapterRouter 的路由筛选与拒绝分支 |
| 固定 `FallbackEnvelope` 不可扩张 | AdapterRouter 的 fallback_blocked / RouteUnavailable 处理 |
| 增加 Route Contract Gate | CAP-TODO-013 / 035 / 022 的设计评审与后续 unit 验证入口 |

## 5. Build 三件套

1. 代码目标：更新 docs/architecture/DASALL_capability_services子系统详细设计.md，补齐 `CapabilitySnapshotView`、`AdapterSelection`、`FallbackEnvelope`、route 输入 source owner、fail-closed 规则与 Route Contract Gate；同步回写 services 专项 TODO。
2. 测试目标：以文档审阅和关键字校验确认 `AdapterSelection`、capability snapshot、trust / availability owner、fallback envelope 与 Route Contract Gate 已回链到 6.3 / 6.4 / 9.4。
3. 验收命令：
   - `rg -n "AdapterSelection|capability snapshot|trust|availability|fallback" docs/architecture/DASALL_capability_services子系统详细设计.md docs/architecture/DASALL_Engineering_Blueprint.md`

## 6. 风险与回退

1. `AdapterSelection`、`CapabilitySnapshotView` 与 `FallbackEnvelope` 保持 internal-only；在 AdapterReceipt 未冻结前，不应把它们提前暴露为 public ABI 或 shared contracts。
2. 若后续新增 route_kind、trust tier 或 degrade policy 语义，必须先回写本表与 Route Contract Gate，再修改 AdapterRouter / fixture 方案，不能在实现阶段隐式扩张。
3. 若 profile 或 runtime 未来调整 fallback chain、trust source 或 availability probe 语义，必须同步复核本表与 9.4 Gate，避免 route contract 与 policy / health 事实漂移。