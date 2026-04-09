# CAP-TODO-037 LocalPlatformAdapter 最小骨架设计收敛

日期：2026-04-09  
任务：CAP-TODO-037  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.4 已冻结 `LocalPlatformAdapter` 的 route_kind 为 `local_platform`，并把它定位为 platform 能力执行与状态读取的本地适配器。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.9.1 已冻结 `enabled_modules.platform_hal -> ServicePolicyView.local_platform_route_enabled` 的派生关系，说明 profile 禁用时必须 fail-closed，而不是静默尝试本地平台路径。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 9.3 已把 “LocalPlatformAdapter 超时 / 不可用” 定义为 failure injection 的关键注入点，因此 037 需要先落一个可被 loopback/fake fixture 替换的最小骨架。
4. CAP-TODO-036 已提供 `IAdapterInvoker` 与 `AdapterReceipt` 的统一桥接面，因此 037 的最小实现应直接复用这一协议，而不是另起一套本地平台调用约定。

## 2. 外部参考

1. Azure Gateway Routing pattern 强调后端选择和可用性判断应由集中式路由/网关控制，后端实例对客户端保持抽象；这支持本轮让 `LocalPlatformAdapter` 只关注本地平台调用事实，而把是否允许选择该路径留在 Router / profile gate。
2. OWASP Authorization Cheat Sheet 的 deny by default 与 exit safely 原则支持本轮把 `platform_hal_enabled=false` 固定为显式 `route_unavailable`，避免在 profile 禁用时仍然尝试访问本地平台能力。
3. 同一原则也支持对本地平台异常做 fail-safe 封装：adapter 返回结构化 `platform_hal_exception` 结果，而不是把异常直接泄漏到上层 lane。

## 3. Design 结论

1. `LocalPlatformAdapter` 通过 `IAdapterInvoker` 接口接入 AdapterBridge，route_kind 固定为 `local_platform`，保持 internal-only。
2. profile 禁用 `platform_hal` 时，adapter 必须直接返回 `route_unavailable`，不调用底层 handler。
3. 真正的平台调用通过注入的 `invoke_platform` handler 完成，这允许后续 loopback/fake fixture 复用同一实现面，而不要求本轮就绑定真实 HAL。
4. 若 handler 未绑定或抛异常，adapter 必须返回结构化 `platform_hal_unbound` / `platform_hal_exception`，保持 fail-safe 和可诊断性。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 固定 `local_platform` route kind 与 invoker 协议 | services/src/adapters/LocalPlatformAdapter.h |
| 固定 profile disabled / unbound / exception 的 fail-safe 行为 | services/src/adapters/LocalPlatformAdapter.cpp |
| 覆盖 identity、disabled、loopback success、exception 四类 unit 场景 | tests/unit/services/adapters/LocalPlatformAdapterTest.cpp |
| 将 LocalPlatformAdapter 纳入 services 与 unit 聚合构建 | services/CMakeLists.txt、tests/unit/services/adapters/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/adapters/LocalPlatformAdapter.h/.cpp`，实现 `LocalPlatformAdapterOptions` 与 `LocalPlatformAdapter` 最小骨架。
2. 测试目标：新增 `tests/unit/services/adapters/LocalPlatformAdapterTest.cpp`，覆盖 profile disabled、loopback success、handler 未绑定/异常等正负例。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 `LocalPlatformAdapter` 只定义统一骨架和注入点，不绑定真实 platform HAL；后续如果接入 `CapabilityRegistry` 或其他 platform 组件，必须继续保持通过 `IAdapterInvoker` 进入 Bridge。
2. 本轮把 profile disabled 固定为 `route_unavailable`，后续 040 需基于 receipt facts 完成 `RouteUnavailable` / `AdapterUnavailable` 的最终分类，不能在 037 内提前生成 `ErrorInfo`。
3. loopback/fake fixture 未来若扩展为集成夹具，应优先复用 `invoke_platform` 注入点，避免另起一套测试专用 adapter 协议。