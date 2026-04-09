# CAP-TODO-035 AdapterRouter 路由组件设计收敛

日期：2026-04-09  
任务：CAP-TODO-035  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 已把 `AdapterRouter` 冻结为 `capability_id + target_id + ServicePolicyView + capability snapshot -> AdapterSelection` 的模块内组件，并明确它是具体 adapter 选择的唯一 owner。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.4、6.9.1 与 8.3 已冻结 local_platform、local_service、remote_service 的 route_kind 语义、`local_platform_route_enabled` 的 profile 派生来源，以及“不越过 fallback envelope”的 blocker 约束。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 9.1 与 9.4 已把 AdapterRouter unit 验证、Route Contract Gate 与 profile / trust / availability 的二值化路由行为列为 D1 的直接验收出口。
4. docs/todos/services/DASALL_capability_services子系统专项TODO.md 已将 CAP-TODO-035 定义为 D1 的起始 Build 任务，要求在不改写 public ABI 和 shared contracts 的前提下，为后续 AdapterBridge / 三类 Adapter / ResultMapper 提供稳定的 route supporting objects 与决策骨架。

## 2. 外部参考

1. Azure Architecture Center 的 Gateway Routing pattern 强调路由决策应集中在网关，根据后端健康度、版本与可用实例完成选择，而不是让客户端感知或指定后端；这支持本轮把 `AdapterRouter` 设计成 services 内部唯一 owner，并用可用性事实驱动同语义候选间的选择。
2. OWASP Authorization Cheat Sheet 强调 deny by default、逐请求校验与不要让客户端控制敏感路由/授权字段；这支持本轮把 trust 不足、availability 缺失和超出 fallback envelope 的情况固定为 fail-closed，而不是静默切换到语义不等价后端。

## 3. Design 结论

1. `AdapterSelection`、`CapabilitySnapshotView`、`FallbackEnvelope` 与 `ServicePolicyView` 均保持 internal-only，落在 services/src/adapters/AdapterRouter.h，不进入 ServiceTypes 或 shared contracts。
2. `AdapterRouter` 只消费既有 route 输入契约：请求 capability/target、模块内 capability snapshot、ServicePolicyView、FallbackEnvelope 与候选 adapter 事实；它不接受 Tool 直接指定 `adapter_id`，也不本地扩张 fallback hop。
3. 路由选择顺序固定为：优先使用 Runtime 给出的 `FallbackEnvelope.ordered_candidates`；若 envelope 未给顺序，则退回 `ServicePolicyView.adapter_preference_order` 与 snapshot route classes 的交集。
4. `local_platform_route_enabled=false`、trust 低于要求、availability 为 unknown/unavailable、或 route equivalence class 与 envelope 不一致时，Router 必须 fail-closed。
5. 只有在 `allow_degrade=true` 且候选仍位于 `route_equivalence_class` 内时，Router 才允许消耗 fallback hop；否则返回 `fallback_blocked`。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 internal-only route supporting objects | services/src/adapters/AdapterRouter.h |
| 固定 fallback envelope / trust / availability 过滤逻辑 | services/src/adapters/AdapterRouter.cpp |
| 覆盖 profile / trust / availability / 等价 fallback 路由 | tests/unit/services/adapters/AdapterRouterTest.cpp |
| 将 AdapterRouter 纳入 services 与 unit 聚合构建 | services/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/services/adapters/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/adapters/AdapterRouter.h/.cpp`，实现 internal-only route supporting objects、route order 求值、trust / availability / equivalence 过滤与 fail-closed 决策。
2. 测试目标：新增 `tests/unit/services/adapters/AdapterRouterTest.cpp`，覆盖 preferred route、profile 禁用、availability fallback、等价类阻断与 trust mismatch 五类正负例。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. `ServicePolicyView` 目前只承载 035 路由所需的最小内部字段；023 落地派生逻辑时可以扩展字段，但不能改变当前 fail-closed 语义或引入 `services.*` 顶层 schema。
2. 本轮不实现 AdapterBridge 和具体 Adapter，因此 `AdapterCandidateView` 仍是 router-side fixture 视图；036/037~039 需要继续把 provider invocation 事实与 route selection 分层隔离。
3. 若后续需要引入多候选权重或成本模型，必须保持 Runtime envelope 优先级不被本地权重覆盖，并先回写 detailed design 的 Route Contract Gate。 