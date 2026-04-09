# CAP-TODO-006 IExecutionService 公共接口设计收敛

日期：2026-04-09  
任务：CAP-TODO-006  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 已给出 IExecutionService 的 V1 接口草图，固定为 `execute`、`compensate`、`query_state`、`subscribe`、`diagnose` 五个方法。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 要求 `execute` 与 `compensate` 继续复用已冻结的 Execution request/result 对象，不额外引入顶层控制面方法。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6.1 明确状态订阅必须通过公共 ABI 暴露，但订阅缓冲、溢出与重同步实现必须保持 internal-only。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6.1 明确安全模式切换属于 `IExecutionService.execute` 的受限 action taxonomy，V1 不新增 `set_safe_mode()` 等单独接口。
5. docs/architecture/DASALL_capability_services子系统详细设计.md 3.9、6.2 与 9.7 均要求当前阶段只保持模块内公共 ABI，不改动 InterfaceCatalog 中 IExecutionService 的 awaiting 准入状态。

## 2. 外部参考

1. C++ Core Guidelines C.121 与 C.127 建议“用作接口的基类应保持纯抽象，并提供公共虚析构函数”。本任务据此将 IExecutionService 收敛为只包含纯虚方法与默认虚析构的稳定接口头，不在基类中引入状态、内联实现或实现细节，从而降低 ABI 脆弱性并保持 tools 与 façade/lanes 的清晰分离。

## 3. Design 结论

1. IExecutionService 作为 execution 子域唯一公共接口头，只暴露五个纯虚方法，不携带任何数据成员。
2. `execute` 与 `compensate` 统一返回 `ExecutionCommandResult`，保持副作用执行与补偿执行共用结果语义面。
3. `query_state`、`subscribe`、`diagnose` 分别对应只读状态、增量订阅与诊断路径，返回各自独立的结果对象，避免把多类语义混入单一返回值。
4. V1 不新增 `set_safe_mode()`、callback handle、reactor 句柄等额外公共面，继续把高风险动作 taxonomy、订阅缓冲与健康探针留在 internal-only 实现层。
5. 本轮只冻结头文件签名，不提前落 ServiceFacade、ExecutionCommandLane 或 mock 层实现，保持 006 的 L3 粒度边界。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 execution 公共接口基类 | services/include/IExecutionService.h 中的 IExecutionService |
| 暴露副作用执行入口 | services/include/IExecutionService.h 中的 execute() |
| 暴露补偿执行入口 | services/include/IExecutionService.h 中的 compensate() |
| 暴露只读状态查询入口 | services/include/IExecutionService.h 中的 query_state() |
| 暴露状态订阅与诊断入口 | services/include/IExecutionService.h 中的 subscribe()、diagnose() |

## 5. Build 三件套

1. 代码目标：更新 services/include/IExecutionService.h，定义纯抽象接口与五个稳定方法签名。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 IExecutionService.h 语法编译检查，显式定义最小 stub 实现以验证签名可覆写。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "IExecutionService.h"\nusing namespace dasall::services;\nstruct Demo final : IExecutionService {\n  ExecutionCommandResult execute(const ExecutionCommandRequest&) override { return {}; }\n  ExecutionCommandResult compensate(const ExecutionCompensationRequest&) override { return {}; }\n  ExecutionQueryResult query_state(const ExecutionQueryRequest&) override { return {}; }\n  ExecutionSubscriptionResult subscribe(const ExecutionSubscriptionRequest&) override { return {}; }\n  ExecutionDiagnoseResult diagnose(const ExecutionDiagnoseRequest&) override { return {}; }\n};\nint main() { Demo demo{}; IExecutionService* service = &demo; return static_cast<int>(service == nullptr); }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. IDataService 仍属于 CAP-TODO-007，本轮不得顺手在 data 接口头中追加 `query` / `list_capabilities`，避免把两个原子任务混做一轮。
2. tests/mocks 与 façade/lane 适配属于后续实现/测试任务；本轮若提前引入具体类依赖，会把纯 ABI 冻结任务拉宽成实现任务。