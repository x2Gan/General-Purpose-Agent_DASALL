# CAP-TODO-004 Execution 结果对象族设计收敛

日期：2026-04-09  
任务：CAP-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 已把 ExecutionCommandResult、ExecutionQueryResult、ExecutionSubscriptionResult、ExecutionDiagnoseResult 列入 V1 公共 supporting object 清单。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 明确 services 只生产公共 result objects，不直接拥有 Observation/ObservationDigest 的最终结构化职责。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 6.5 同时要求 ErrorInfo.failure_type、retryable、safe_to_replan、details、source_ref 全部复用既有 contracts 语义，services 只能填值，不可重定义。
4. docs/architecture/DASALL_capability_services子系统详细设计.md 6.6 代码草图已冻结四个 Execution 结果对象的字段清单，包括 `payload_json`、`snapshot_json`、`events_json`、`report_json` 的 SerializedJson 承载方式，以及 `side_effects`、`compensation_hints`、`from_cache`、`resync_required`、`dropped_count` 等结果字段。
5. docs/architecture/DASALL_capability_services子系统详细设计.md 6.8 要求 PartialSideEffect 路径必须对外携带 side_effects 与 compensation_hints，订阅溢出则必须对外携带 `resync_required` 与丢弃计数，因此这些字段必须在公共结果对象层冻结，而不是推迟到内部 mapper。

## 2. 外部参考

1. Protobuf Best Practices 建议 RPC API 与存储消息分离，并避免把未来可能独立演进的内部结构直接暴露为共享对象。本任务据此把 services 的执行结果对象保持为面向 tool/service 边界的最小事实载体：只暴露公共结果字段、结构化字符串载荷与既有 ErrorInfo/ResultCode 语义，不把 Observation、AuditEvent 或内部 AdapterReceipt 直接泄露到公共 ABI。

## 3. Design 结论

1. ExecutionCommandResult 聚焦副作用命令结果，必须同时承载 `execution_id`、`payload_json`、`side_effects`、`compensation_hints` 与可选错误对象。
2. ExecutionQueryResult 保持只读查询边界，通过 `state`、`snapshot_json` 与 `from_cache` 表达结果事实，不混入副作用字段。
3. ExecutionSubscriptionResult 统一承载增量事件、续传游标、重同步标志与丢弃计数，满足 cursor/batch 订阅公共 ABI 的冻结要求。
4. ExecutionDiagnoseResult 只暴露 reachability 与 report_json，不替代 infra diagnostics 的系统级导出职责。
5. 四个结果对象统一复用 contracts::ResultCode 与 contracts::ErrorInfo，确保 services 层只填值、不重定义错误语义。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结副作用命令结果对象 | services/include/ServiceTypes.h 中的 ExecutionCommandResult |
| 冻结只读执行查询结果对象 | services/include/ServiceTypes.h 中的 ExecutionQueryResult |
| 冻结状态订阅结果对象 | services/include/ServiceTypes.h 中的 ExecutionSubscriptionResult |
| 冻结执行诊断结果对象 | services/include/ServiceTypes.h 中的 ExecutionDiagnoseResult |
| 复用既有错误与结果码语义 | 上述对象中的 contracts::ResultCode / contracts::ErrorInfo |

## 5. Build 三件套

1. 代码目标：更新 services/include/ServiceTypes.h，新增四个 Execution 结果对象。
2. 测试目标：保持 InterfaceCatalogContractTest 不回退，并补一条 ServiceTypes.h 语法编译检查，显式实例化四个 result 类型。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services dasall_contract_tests
   - ctest --test-dir build-ci --output-on-failure -R "^InterfaceCatalogContractTest$"
   - printf '#include "ServiceTypes.h"\nusing namespace dasall::services;\nint main() { ExecutionCommandResult a{}; ExecutionQueryResult b{}; ExecutionSubscriptionResult c{}; ExecutionDiagnoseResult d{}; return static_cast<int>(a.side_effects.size() + a.compensation_hints.size() + b.state.size() + c.dropped_count + d.target_reachable); }\n' | c++ -std=c++20 -Iservices/include -Icontracts/include -xc++ -fsyntax-only -

## 6. 风险与回退

1. Data 请求/结果对象属于 CAP-TODO-005，本轮不得把 rows_json、catalog_json 或 data query 语义提前并入 Execution 结果层。
2. ErrorInfo.failure_type 的分类映射规则仍属于 CAP-TODO-014/040 范围，本轮只冻结结果对象字段，不在头文件里硬编码 ServiceErrorClass 映射实现。