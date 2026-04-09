# CAP-TODO-002 服务调用基础对象设计收敛

日期：2026-04-09  
任务：CAP-TODO-002  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 已冻结 ServiceTypes.h 的 V1 公共 supporting object 清单，其中基础对象只包括 ServiceCallContext、CapabilityTargetRef、ServiceDataFreshness。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 约束这些对象只能依赖 STL 与既有冻结 contracts 类型，且不得新增共享 helper family。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 表格明确 ServiceCallContext 只透传 request_id、session_id、trace_id、tool_call_id、goal_id、RuntimeBudget 与 deadline，不新增 contracts 语义。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 代码草图已给出 ServiceDataFreshness 取值 strict/allow_stale、CapabilityTargetRef 的 capability_id/target_id 二元定位，以及 ServiceCallContext 的字段顺序与 RuntimeBudget optional 引用语义。
5. contracts/include/checkpoint/RuntimeBudget.h 已冻结 runtime budget 的五维 contracts 语义，因此本轮只能复用该类型，不能在 services 内重新定义预算对象。

## 2. 外部参考

1. Protobuf Best Practices 强调跨版本演进中应复用已有 common types、避免随意改变字段类型，并尽量把对象拆成职责清晰的小消息而不是臃肿大对象。本任务据此保持基础对象家族最小化：定位、freshness、调用上下文三类职责分开，且预算字段直接复用既有 RuntimeBudget，而不是再造 services 私有预算语义。

## 3. Design 结论

1. ServiceDataFreshness 采用 enum class，并只暴露 strict / allow_stale 两个已在详细设计中冻结的读策略值，不使用布尔字段压缩未来扩展空间。
2. CapabilityTargetRef 仅承载 capability_id 与 target_id，保持“稳定定位对象”语义，不提前混入路由、trust、availability 或 health 事实。
3. ServiceCallContext 严格透传五个标识字段、可选 RuntimeBudget 与 deadline_ms；其中 budget_guard 直接复用 contracts::RuntimeBudget，防止 services 私自扩张预算维度。
4. 基础对象仍保持 plain struct，不在本轮引入校验 helper、构造器或序列化逻辑，避免越过 CAP-TODO-002 的粒度边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 freshness 双态策略 | services/include/ServiceTypes.h 中的 ServiceDataFreshness |
| 冻结 capability/target 二元定位对象 | services/include/ServiceTypes.h 中的 CapabilityTargetRef |
| 冻结服务调用上下文透传对象 | services/include/ServiceTypes.h 中的 ServiceCallContext |
| 复用既有 RuntimeBudget contracts 语义 | ServiceCallContext::budget_guard -> contracts::RuntimeBudget |

## 5. Build 三件套

1. 代码目标：更新 services/include/ServiceTypes.h，新增 ServiceDataFreshness、CapabilityTargetRef、ServiceCallContext。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 ServiceTypes.h 语法编译检查，确认基础对象头文件可被独立包含。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "ServiceTypes.h"\nint main() { return 0; }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. Execution/Data 请求与结果对象仍属于 CAP-TODO-003~005，本轮不得把 SerializedJson、ResultCode、ErrorInfo 等字段提前并入基础对象层。
2. 若后续 detailed design 把 deadline 改成更强类型包装，应单独走字段演进评审；本轮先与当前 6.6 代码草图保持一致，使用 uint64_t 毫秒值。