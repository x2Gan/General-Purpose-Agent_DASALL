# CAP-TODO-040 ResultMapper 结果映射组件设计收敛

日期：2026-04-09  
任务：CAP-TODO-040  
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 已冻结 `ResultMapper` 的职责是“把 adapter receipt 映射为公共 result、错误分类与侧效应摘要”，并要求其成为 `AdapterReceipt -> ErrorInfo` 的唯一 owner。
2. 同一设计文档 6.8.1 已给出九类 `ServiceErrorClass` 的分类表、`ErrorInfo.failure_type` 映射、`retryable` / `safe_to_replan` 口径，以及 `PartialSideEffect` 的 evidence / side_effects / compensation_hints 约束。
3. CAP-TODO-036 已提供稳定的 `AdapterReceipt` fixture，CAP-TODO-037~039 已把 local platform / local service / remote service 三类路径的 timeout、unreachable、route_unavailable 等事实落盘，因此 040 可以直接在 internal mapping 层收口错误语义。
4. CAP-TODO-014 已明确 `ErrorInfo` 语义不得被重新定义，040 只能填充既有 contracts 中的 `failure_type`、`retryable`、`safe_to_replan`、`details` 与 `source_ref`。

## 2. 外部参考

1. Azure Compensating Transaction pattern 强调部分成功路径必须保留可追溯的已执行事实与补偿线索，这支持本轮要求 `PartialSideEffect` 仅在 receipt 携带 durable `evidence_refs` 且补偿提示存在时才对外释放 `side_effects` 与 `compensation_hints`。
2. OWASP Authorization Cheat Sheet 的 fail safely / deny by default 原则支持本轮把不完整的 partial receipt 视为非法输入并 fail-closed，而不是把缺少 evidence 的部分成功伪装成可恢复错误。
3. 同一原则也支持本轮继续复用既有 `ErrorInfo` 合同，而不是新增 services 私有错误对象或扩张 shared contracts。

## 3. Design 结论

1. `ResultMapper` 作为 internal-only 组件新增于 `services/src/mapping/`，提供 `map_result(receipt, compensation_hints)` 的统一收口，并给 execution / data 结果对象提供最小公共构造 helper。
2. 代码内新增 internal `ServiceErrorClass` 九分类，按设计表把它们稳定映射到既有 `ErrorInfo.failure_type` 分类域：validation / policy / provider / runtime。
3. `PartialSideEffect` 只有在 `side_effects`、`evidence_refs`、`compensation_hints` 三者齐备时才允许对外暴露；若 receipt 事实不完整，则 fail-closed 为 validation failure，并清空未验证的 side effects / compensation hints。
4. `ResultMapper` 只填值，不重定义 `ErrorInfo` 合同；`details.code` 继续复用既有 `ResultCode` 种子，`source_ref` 只在 receipt / evidence / policy / snapshot 等既有来源上选取 primary ref。
5. success path 仍保持保守：mapper 只负责透传 `payload_json` 与 side effect facts，为后续 lane 实现提供基础，不借此扩张 shared contracts 或发明新的 success code 合同。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 固定 `ServiceErrorClass -> ErrorInfo.failure_type` 分类分支 | services/src/mapping/ResultMapper.h、services/src/mapping/ResultMapper.cpp |
| 固定 partial side effect 的 evidence / hints fail-closed 规则 | services/src/mapping/ResultMapper.cpp |
| 为 execution / data 结果对象提供最小构造 helper | services/src/mapping/ResultMapper.h、services/src/mapping/ResultMapper.cpp |
| 覆盖九类错误映射、partial evidence、subscription overflow、data stale 与 success path | tests/unit/services/mapping/ResultMapperTest.cpp |
| 将 ResultMapper unit 接入 services 与顶层 unit 聚合 | services/CMakeLists.txt、tests/unit/services/CMakeLists.txt、tests/unit/services/mapping/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/mapping/ResultMapper.h/.cpp`，实现 internal `ServiceErrorClass`、统一 `ResultMapping` 与 execution / data 结果构造 helper。
2. 测试目标：新增 `tests/unit/services/mapping/ResultMapperTest.cpp`，覆盖九类错误映射、partial side effect evidence 约束、subscription overflow 与 data stale 行为。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_services dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest`

## 6. 风险与回退

1. 当前 `ResultCode` 仍只有最小失败种子；040 只按既有 category 种子回填 `details.code`，不在本轮扩张 shared contracts 的 success / failure 代码全集。
2. `ResultMapper` 仅为 execution / data / system 车道提供 internal foundation，不应被当作新的跨模块 ABI；任何 shared-contract 升格仍必须经过 CAP-TODO-033。
3. 若后续新增 `ServiceErrorClass` 或新的 adapter provider status，必须先回写 [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 与 Receipt Mapping Gate，再扩展本组件实现与 integration 证据。