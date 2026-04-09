# CAP-TODO-010 ServiceFacade 组合根骨架设计收敛

日期：2026-04-09  
任务：CAP-TODO-010  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.2 已冻结结论：ServiceFacade 是模块内部组合根，同时实现 IExecutionService 与 IDataService，不进入公共 ABI。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.3 与 6.4 明确 ServiceFacade 的输入输出和依赖关系：它先调用 ServiceContextBuilder，再把请求分发给 execution/data/system 子域，而不是直接拥有 platform/infra 细节。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.7.1 主执行链路要求 `normalize_context()` 发生在 façade 进入 execution lane 之前，因此 façade 需要先做上下文规范化，再做委派。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 7.1 已把 ServiceFacade.cpp 映射为统一入口实现，并要求通过 unit 验证“命令/查询分发与上下文透传”。
5. CAP-TODO-010 的完成判定要求 ServiceFacade 同时实现两类接口、只做编排和委派、且不引入审批/恢复逻辑，因此本轮必须把依赖显式注入，而不是在 façade 内部直接 new 具体 lane/adapter。

## 2. 外部参考

1. Martin Fowler 在《Inversion of Control Containers and the Dependency Injection pattern》中强调：依赖注入的关键价值是把配置与使用分离，并让组件只依赖抽象协作者而非具体实现。本轮据此让 ServiceFacade 通过构造注入 `ServiceFacadeDependencies`，仅负责组合和委派，不直接绑定具体 execution/data 实现。

## 3. Design 结论

1. ServiceFacade 保持模块内部头文件，不进入 services/include 的公共 ABI。
2. ServiceFacade 同时实现 IExecutionService 与 IDataService，但内部只依赖 ServiceContextBuilder 和一组显式注入的委派 handler。
3. 所有公共方法都必须先做 `normalize_context()`；若上下文非法，直接返回 validation failure，不触发委派 handler。
4. 本轮只落 façade 级的组合与委派骨架；不引入审批、权限、确认、恢复或 adapter 选择逻辑。
5. 为满足本轮 unit 验证，需要补一条最小 ServiceFacadeTest target；services unit 拓扑的系统性收口仍保留给 CAP-TODO-011。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 定义 façade 依赖注入面 | services/src/ServiceFacade.h 中的 ServiceFacadeDependencies |
| 让 façade 同时实现两类公共接口 | services/src/ServiceFacade.h 中的 ServiceFacade |
| 先规范化后委派 | services/src/ServiceFacade.cpp 中各公共方法的 normalize_context + handler 调用流程 |
| 非法上下文阻断委派 | tests/unit/services/ServiceFacadeTest.cpp 的负例 |
| 覆盖 execute/query 两类入口 | tests/unit/services/ServiceFacadeTest.cpp 的正例 |
| 提供最小 façade unit 入口 | tests/unit/CMakeLists.txt 中的 dasall_service_facade_unit_test |

## 5. Build 三件套

1. 代码目标：新增 services/src/ServiceFacade.h，更新 services/src/ServiceFacade.cpp，实现 IExecutionService / IDataService 的内部 façade 委派骨架。
2. 测试目标：新增 ServiceFacadeTest，覆盖一条 execute/query 正例和一条非法上下文负例；保持 InterfaceCatalogContractTest 不回退。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -L unit
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"

## 6. 风险与回退

1. ServiceFacade 当前通过 injected handlers 组合子域；等到 execution/data/system 真实 lane 落地后，应由后续任务替换这些 handler，而不是把具体实现直接硬编码进 façade。
2. façade 返回的 validation/runtime failure 只用于骨架阶段的显式失败面；更细的 ServiceErrorClass 与 ErrorInfo 映射仍应留在后续 ResultMapper / lane 任务中收口。