# CAP-TODO-033 IExecutionService / IDataService admission 评审收敛

日期：2026-04-09
任务：CAP-TODO-033
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 7.2、8.2 Phase 6、10.2 与 12.2 已冻结本轮边界：IExecutionService / IDataService 的 shared-contract 工作只能先做 admission review，不允许在本轮直接把接口头迁入 contracts/include 或扩张 ISystemService。
2. [services/include/ServiceTypes.h](../../../../services/include/ServiceTypes.h) 已冻结 execution/data 的上下文、请求、结果 supporting objects：Execution 侧包含 command/compensation/query/subscription/diagnose 五类请求与四类结果，Data 侧包含 query/catalog 两类请求与两类结果，并继续复用既有 contracts::RuntimeBudget、ResultCode、ErrorInfo 语义而不发明新的 shared contracts 基础类型。
3. [services/include/IExecutionService.h](../../../../services/include/IExecutionService.h) 与 [services/include/IDataService.h](../../../../services/include/IDataService.h) 已冻结 5+2 个方法签名，且仍保持 execution facade 与 query-only data facade 的边界，不含额外 safe_mode setter、恢复裁定或写语义扩张。
4. [docs/todos/services/deliverables/CAP-TODO-030-CapabilityServices-smoke-integration设计收敛.md](CAP-TODO-030-CapabilityServices-smoke-integration设计收敛.md)、[docs/todos/services/deliverables/CAP-TODO-031-CapabilityServices-failure-integration设计收敛.md](CAP-TODO-031-CapabilityServices-failure-integration设计收敛.md) 与 [docs/todos/services/deliverables/CAP-TODO-032-CapabilityServices-profile-integration设计收敛.md](CAP-TODO-032-CapabilityServices-profile-integration设计收敛.md) 已补齐 smoke/failure/profile 三类集成证据，说明 services facade 的 route、timeout、cache、partial side effect、overflow 和 observability 路径已不再停留在模块内占位阶段。
5. [contracts/include/boundary/InterfaceCatalog.h](../../../../contracts/include/boundary/InterfaceCatalog.h) 与 [contracts/include/boundary/InterfaceAdmissionGuards.h](../../../../contracts/include/boundary/InterfaceAdmissionGuards.h) 已冻结 admission 判定框架：catalog row 只要 metadata 完整、owner/consumer 构成跨模块边界，且 readiness=ReviewReady，就会被守卫程序化判定为 Admit；因此 033 的核心不是新增共享接口头，而是给出 services pair 的正式 readiness 决策并把理由写回 catalog metadata。
6. [docs/todos/services/DASALL_capability_services子系统专项TODO.md](../DASALL_capability_services子系统专项TODO.md) 中 CAP-BLK-005 的解阻条件已经明确为 supporting objects 稳定、services integration 全量通过、评审结论明确；030~032 已补齐最后一批 integration 证据，033 现在是 Phase 6 的最小可执行原子任务。

## 2. 外部参考

1. Protobuf Best Practices 指出客户端与服务端不会严格同步升级，共享契约不应在 supporting objects 仍快速演化时过早暴露，并建议把广泛复用的 message type 拆成独立、稳定、低依赖文件。该原则支持 033 先验证 ServiceTypes 与接口面已经稳定，再给出 shared-contract admission 结论，而不是把未定型对象提前固化。参考：https://protobuf.dev/best-practices/dos-donts/
2. Azure Architecture Center 的 Strangler Fig pattern 强调：在增量迁移期间，应先稳定 façade，让调用方持续面对同一抽象接口，再逐步收敛内部路由和实现。这支持 services 继续以 IExecutionService / IDataService 作为 tools 侧稳定门面，并把 adapter、cache、route、profile 等变化留在 façade 背后，而不是要求调用方感知内部替换。参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/strangler-fig

## 3. readiness checklist

1. supporting objects 是否冻结：通过。ServiceTypes 已覆盖 execution/data 所需公共上下文、请求、结果，不再依赖未定型的 adapter receipt、health snapshot 或 system snapshot supporting object。
2. 公共 ABI 是否稳定：通过。IExecutionService / IDataService 的方法面已闭合，且未引入恢复裁定、审批决策、system shared ABI 或 `services.*` schema。
3. integration 证据是否齐全：通过。030~032 已分别验证 smoke、failure、profile 差异；专项 TODO 顶部结论同时记录 audit/metrics/trace/health 七类入口已形成稳定 evidence chain。
4. admission 边界是否清晰：通过。本轮只更新 InterfaceCatalog / contract tests / design evidence，不直接在 contracts/include 中落位正式 services 接口头，也不改变现有 Tool/Runtime/Service 控制权分层。
5. contract gate 是否可程序化验证：通过。InterfaceCatalogContractTest 与 InterfaceAdmissionContractTest 都可以直接消费 catalog/guard 元数据，避免把 admission 结论留在纯文档层。

## 4. 评审结论

1. `IExecutionService`：Admit。原因是 execution supporting objects、接口签名、smoke/failure/profile evidence 与 high-risk fail-closed 语义已经闭合，tools 侧也确实需要一个稳定的 execution facade，而不是直连 platform / remote adapter 细节。
2. `IDataService`：Admit。原因是 data request/result supporting objects 与 query-only 边界已经冻结，并且 route / cache / stale-read / profile 差异已有稳定 integration 证据，不再需要继续保持 awaiting 状态。
3. `ISystemService` 或其他 services internal object：仍不进入 admission。system snapshot、health probe、adapter receipt、policy view 继续保持 internal-only，不随 033 一并升格。
4. 本轮 Admit 的含义仅限“进入 shared-contract admission baseline”。它会把 InterfaceCatalog readiness 从 AwaitingSupportingContracts 提升为 ReviewReady，并使 admission guards 返回 Admit；但它不等于本轮必须新增 contracts/include 下的 services interface 头文件。

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| services pair 的 readiness checklist 收口 | 本文件、docs/architecture/DASALL_capability_services子系统详细设计.md、docs/todos/services/DASALL_capability_services子系统专项TODO.md |
| 把 Admit 结论程序化写回 catalog metadata | contracts/include/boundary/InterfaceCatalog.h |
| 收紧 contract gate，避免 services pair 再被误判为 awaiting | tests/contract/smoke/InterfaceCatalogContractTest.cpp、tests/contract/smoke/InterfaceAdmissionContractTest.cpp |
| 回写执行记录与下一任务入口 | docs/worklog/DASALL_开发执行记录.md |

## 6. Build 三件套

1. 代码目标：更新 InterfaceCatalog 中 IExecutionService / IDataService 的 readiness、stable_anchor 和 rationale，并同步修正 InterfaceCatalog / InterfaceAdmission contract tests 的基线断言；补齐 services 详细设计、专项 TODO 与交付物中的 admission 评审记录。
2. 测试目标：`InterfaceCatalogContractTest` 需要验证 services pair 已进入 ReviewReady 集合，`InterfaceAdmissionContractTest` 需要验证 admitted baseline 扩展到 services pair，且其余未冻结接口仍保持 Postpone/Return 路径不回退。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest`
   - `ctest --test-dir build-ci --output-on-failure -R InterfaceAdmissionContractTest`
   - `ctest --test-dir build-ci --output-on-failure -L contract`

## 7. 风险与回退

1. `ReviewReady` / `Admit` 现在只意味着 admission baseline 已更新，不意味着 services 接口头已经正式迁入 contracts/include；若后续要做 shared header 落位，必须新开原子任务处理 include 路径、迁移策略和兼容评审。
2. 033 不会放宽 system/internal supporting objects 的边界。若未来出现稳定跨模块 system 消费者，仍要单独发起新的 interface admission review，而不是借 services pair 的 Admit 结论顺带升格 `ISystemService`。
3. 若后续 smoke/failure/profile 其中任一证据回退，应优先回退 services pair 的 readiness 结论，而不是让 contract tests 与 design evidence 脱节。