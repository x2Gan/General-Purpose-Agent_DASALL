# CAP-TODO-003 Execution 请求对象族设计收敛

日期：2026-04-09  
任务：CAP-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 明确 ExecutionCommandRequest、ExecutionCompensationRequest、ExecutionQueryRequest、ExecutionSubscriptionRequest、ExecutionDiagnoseRequest 都属于 V1 公共 supporting objects，并统一落在 ServiceTypes.h。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 对 Execution 请求对象族给出边界约束：它们由 ToolRequest/GoalContract/PolicyDecision 派生，但不进入 contracts，且不得直接嵌入 Observation 或 RecoveryOutcome。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 代码草图已经冻结五个请求对象的字段清单、`arguments_json` 的 SerializedJson 字符串承载方式，以及 `cursor` / `idempotency_key` 的 optional 语义。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6.1 进一步要求订阅能力经 cursor/batch ABI 暴露、安全模式切换经 execute action taxonomy 进入，而不是新增独立顶层接口，因此本轮要在请求对象层保留 `stream_kind`、`action` 与 `compensation_action` 的稳定槽位。
5. CAP-TODO-002 已冻结 ServiceCallContext、CapabilityTargetRef、ServiceDataFreshness，本轮只在这些基础对象之上追加 execution 请求对象，不回改基础层定义。

## 2. 外部参考

1. Protobuf Best Practices 建议为 RPC API 与存储使用不同消息，并保持对象职责单一、字段类型稳定。本任务据此把 execution 命令、补偿、查询、订阅、诊断拆成五个独立请求对象，而不是复用一个“万能请求”并塞入模糊字段；同时继续用字符串化 `SerializedJson` 承载结构化参数，避免把服务内部 payload 结构直接冻结进公共 ABI。

## 3. Design 结论

1. ExecutionCommandRequest 只承载副作用命令请求所需的 context、target、action、arguments_json 与幂等键，不混入补偿、查询或诊断特有字段。
2. ExecutionCompensationRequest 明确补偿动作、source_execution_id 与 reason_code，以体现“显式授权补偿”而非自动回滚的设计边界。
3. ExecutionQueryRequest 与 ExecutionSubscriptionRequest 分离：前者只读查询状态，后者专注于 cursor/batch 风格订阅语义。
4. ExecutionDiagnoseRequest 只承载 include_last_error 这一诊断附加开关，不引入会改变目标状态的控制字段。
5. SerializedJson 作为 string alias 放在 ServiceTypes.h 中，保持所有公共结构化参数字段统一使用字符串承载，不引入具体 JSON 库依赖。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结副作用命令请求对象 | services/include/ServiceTypes.h 中的 ExecutionCommandRequest |
| 冻结显式补偿请求对象 | services/include/ServiceTypes.h 中的 ExecutionCompensationRequest |
| 冻结只读查询请求对象 | services/include/ServiceTypes.h 中的 ExecutionQueryRequest |
| 冻结 cursor/batch 订阅请求对象 | services/include/ServiceTypes.h 中的 ExecutionSubscriptionRequest |
| 冻结诊断请求对象 | services/include/ServiceTypes.h 中的 ExecutionDiagnoseRequest |

## 5. Build 三件套

1. 代码目标：更新 services/include/ServiceTypes.h，新增五个 Execution 请求对象与 SerializedJson alias。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 ServiceTypes.h 语法编译检查，显式实例化五个 request 类型。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "ServiceTypes.h"\nusing namespace dasall::services;\nint main() { ExecutionCommandRequest a{}; ExecutionCompensationRequest b{}; ExecutionQueryRequest c{}; ExecutionSubscriptionRequest d{}; ExecutionDiagnoseRequest e{}; return static_cast<int>(a.action.size() + b.reason_code.size() + c.query_kind.size() + d.stream_kind.size() + e.include_last_error); }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. Execution 结果对象属于 CAP-TODO-004，本轮不得提前引入 ResultCode、ErrorInfo、side_effects 或 compensation_hints 字段。
2. 若后续 AdapterSelection 或 taxonomy 设计调整 action 字段的枚举化形式，应在命令车道或补设计任务中处理，不应在 003 阶段把 `action` / `stream_kind` 提前冻结成更强类型。