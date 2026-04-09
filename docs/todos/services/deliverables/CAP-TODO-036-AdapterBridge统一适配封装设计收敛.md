# CAP-TODO-036 AdapterBridge 统一适配封装设计收敛

日期：2026-04-09  
任务：CAP-TODO-036  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 已冻结 `AdapterBridge` 的职责为 `AdapterSelection + adapter invocation -> AdapterReceipt`，说明 036 的目标不是结果映射，而是把三类 adapter 的调用结果收束成统一 receipt facts。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8.1 与 6.9.4 已冻结 `AdapterReceipt` 的字段边界和 Receipt Mapping Gate：Bridge 只记录 provider / transport 事实，不能提前生成 `ErrorInfo` 或补偿裁定。
3. docs/todos/services/deliverables/CAP-TODO-014-AdapterReceipt与结果映射契约设计收敛.md 已把 `receipt_ref`、`provider_status_code`、`side_effects`、`evidence_refs` 等字段定义为 D1 的直接 Build 输入，因此 036 必须把这些字段先落实到可复用的 fixture 与 adapter 统一接口上。
4. docs/todos/services/DASALL_capability_services子系统专项TODO.md 已把 CAP-TODO-036 设为 037~040 和 015~021 的共同前置，要求负路径不吞错、AdapterReceipt 字段完整，并为后续具体 adapter 与 ResultMapper 提供稳定输入。

## 2. 外部参考

1. Azure Architecture Center 的 Compensating Transaction pattern 强调在多步操作中必须记录每一步已完成工作的可追溯上下文，并把补偿所需信息保留下来；这支持本轮让 AdapterBridge 保留 `receipt_ref`、`provider_status_code`、`side_effects` 和 `evidence_refs`，而不是只返回模糊成功/失败。
2. 该模式还强调补偿步骤应设计为幂等且在失败后可恢复，这意味着 Bridge 不能吞掉 provider 异常或丢失 receipt facts，否则 040 和后续 command lane 无法据此生成可靠的 `compensation_hints`。
3. OWASP Authorization Cheat Sheet 的 fail safely / deny by default 原则支持本轮把“adapter 未注册”“route_kind 不匹配”和“adapter 抛异常”全部显式变成结构化 receipt failure，而不是隐式回退或静默吞错。

## 3. Design 结论

1. `AdapterReceipt`、`AdapterInvocationRequest`、`AdapterInvocationResult` 和 `IAdapterInvoker` 都保持 internal-only，落在 services/src/adapters/AdapterBridge.h，不进入 public ABI。
2. `AdapterBridge` 只做三件事：根据 `adapter_id` 找到统一 invoker、核对 `route_kind` 一致性、把 invoker 返回的 provider / transport 事实组装为 `AdapterReceipt`。
3. `AdapterBridge` 不生成 `ErrorInfo`、不分类 `ServiceErrorClass`、不输出 `compensation_hints`；这些职责保留给 040 的 ResultMapper 和后续 command lane。
4. 对于未注册 adapter、`route_kind` 不匹配和 adapter 抛异常，Bridge 必须 fail-safe，返回结构化 failure receipt，并保留错误上下文到 `provider_status_code` / `payload_json`。
5. 对于 partial side effect，Bridge 只保留 provider 返回的 `side_effects` 与 `evidence_refs`，不做语义扩张或自动补偿。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `AdapterReceipt` internal-only 事实对象 | services/src/adapters/AdapterBridge.h |
| 统一 invoker 接口与 Bridge 调用骨架 | services/src/adapters/AdapterBridge.cpp |
| 验证成功、未注册、route mismatch、partial、exception 五类路径 | tests/unit/services/adapters/AdapterBridgeTest.cpp |
| 将 AdapterBridge 纳入 services 与 unit 聚合构建 | services/CMakeLists.txt、tests/unit/services/adapters/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/adapters/AdapterBridge.h/.cpp`，实现 `IAdapterInvoker`、`AdapterInvocationRequest`、`AdapterInvocationResult`、`AdapterReceipt` 与 `AdapterBridge::invoke()`。
2. 测试目标：新增 `tests/unit/services/adapters/AdapterBridgeTest.cpp`，覆盖成功路径、未注册 adapter、route_kind 不匹配、partial side effect 和 invoker 异常五类正负例。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. `AdapterBridge` 当前只依赖统一 invoker 接口，不绑定具体 LocalPlatform/LocalService/RemoteService 实现；037~039 落地时必须复用该接口，而不是在 lane 层再次分叉调用协议。
2. `AdapterReceipt` 只保留事实，不做错误分类；040 若需要增加 `ServiceErrorClass`、`ErrorInfo` 或 `compensation_hints` 逻辑，必须继续保持这一分层，不得反向塞回 Bridge。
3. 若后续 provider 需要更丰富的状态码或原始响应体，优先扩展 internal-only `AdapterInvocationResult` / `AdapterReceipt`，不得把这些字段提前升级为 ServiceTypes 公共字段。