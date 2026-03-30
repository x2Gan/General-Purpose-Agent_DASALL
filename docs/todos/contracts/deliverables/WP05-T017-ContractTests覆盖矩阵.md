# WP05-T017 Contract Tests 覆盖矩阵

最近更新时间：2026-03-22
任务状态：Done
任务编号：WP05-T017
上游输入：WP05-T013-D、WP05-T014-D、WP05-T015-D、WP05-T016-D

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. `tests/contract/serialization/SerializationCompatibilityContractTest.cpp` 已覆盖 AgentRequest/EventEnvelope 的序列化兼容主路径与越权字段负例。
2. `tests/contract/error/ErrorCodeEnumCompatibilityContractTest.cpp` 已覆盖 ResultCode 编号冻结与 enum lifecycle 降级兼容。
3. `tests/contract/event/EventEnvelopeCompatibilityContractTest.cpp` 已覆盖 EventEnvelope 核心头部稳定性、扩展字段放行与白名单越权拒绝。
4. `tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp` 已覆盖 ADR-006/007/008 三类边界对象与禁区字段回归。
5. `contracts/include/boundary/ADRFieldMappingGuards.h` 已提供 ADR 关键对象和禁区字段目录，可作为覆盖矩阵守卫的输入来源之一。

### 外部参考清单

1. Martin Fowler, "Evolutionary Architecture"（强调架构约束应通过可执行 fitness function 持续验证）：https://martinfowler.com/articles/evolutionary-architecture.html
2. Google Testing Blog, "Testing on the Toilet: Test Matrix"（强调通过矩阵化覆盖降低遗漏风险）：https://testing.googleblog.com/

### 对本任务的可落地启发

1. 覆盖矩阵必须把“高风险对象”与“稳定测试入口”建立显式映射，而不是依赖口头约定。
2. 自动守卫应能返回首个缺口对象，便于 gate 在失败时快速定位。
3. 覆盖矩阵守卫应与测试执行快照解耦，允许后续 CI 门禁复用同一判定函数。
4. 需要同时覆盖正例（全部覆盖）和负例（缺少关键测试）以保证守卫本身可靠。

## 1. 任务理解

本任务只处理 WP05-T017：

1. 完成 Contract Tests 覆盖矩阵文档（Design）。
2. 新增 `contracts/include/boundary/CoverageMatrixGuards.h` 自动检查守卫（Build）。
3. 新增 `tests/contract/smoke/CoverageMatrixContractTest.cpp` 并接入 contract tests（Build）。

本任务不处理：

1. 改写 T013-T016 既有测试语义与断言实现。
2. 扩展到 T018 版本变更模板与 schema 校验。

## 2. Design 原子清单

1. D1：定义高风险对象覆盖目录。
- 输入依据：T013-D 至 T016-D 覆盖矩阵。
- 产出路径：`CoverageMatrixGuards.h` 中 `kCoverageMatrixCatalog`。
- 完成判定：每个高风险对象至少有 1 条测试映射。
- 风险与回退：若目录缺口，优先补目录与映射，不扩张对象语义。

2. D2：定义执行快照与自动判定结果模型。
- 输入依据：WP05-T017-B 目标“覆盖缺口可自动发现”。
- 产出路径：`CoverageMatrixGuards.h` 中 snapshot/result 结构与 `validate_coverage_matrix`。
- 完成判定：能输出首个缺口对象与失败原因。
- 风险与回退：若失败定位不稳定，退回按目录顺序返回首个缺口。

3. D3：锁定 Build 三件套与发现性证据。
- 输入依据：`tests/contract/CMakeLists.txt`。
- 产出路径：`CoverageMatrixContractTest.cpp` + CMake 注册项。
- 完成判定：构建后 `ctest --test-dir build-ci -N -R CoverageMatrixContractTest` 可发现 1 个测试。
- 风险与回退：若不可发现，先修注册再复跑验收。

## 3. Contract Tests 覆盖矩阵

| 序号 | 高风险对象 | 风险面 | 对应 contract test | 覆盖要求 |
|---|---|---|---|---|
| M1 | AgentRequest | 序列化稳定性/前向兼容 | SerializationCompatibilityContractTest | 至少 1 条映射 |
| M2 | EventEnvelope | 序列化稳定性 | SerializationCompatibilityContractTest | 至少 1 条映射 |
| M3 | EventEnvelope | 事件封套兼容 | EventEnvelopeCompatibilityContractTest | 至少 1 条映射 |
| M4 | ResultCode | 错误码编号冻结 | ErrorCodeEnumCompatibilityContractTest | 至少 1 条映射 |
| M5 | EnumLifecycle | 枚举生命周期兼容 | ErrorCodeEnumCompatibilityContractTest | 至少 1 条映射 |
| M6 | ContextPacket | ADR-006 边界字段 | ADRBoundaryRegressionContractTest | 至少 1 条映射 |
| M7 | ReflectionDecision | ADR-007 边界字段 | ADRBoundaryRegressionContractTest | 至少 1 条映射 |
| M8 | MultiAgentResult | ADR-008 边界字段 | ADRBoundaryRegressionContractTest | 至少 1 条映射 |

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | 高风险对象必须有测试映射目录 | 新增覆盖目录与对象枚举 | contracts/include/boundary/CoverageMatrixGuards.h | CoverageMatrixContractTest 校验目录完整性 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CoverageMatrixContractTest --output-on-failure |
| D2 | 覆盖缺口必须自动定位 | 新增 snapshot 判定与首缺口返回 | contracts/include/boundary/CoverageMatrixGuards.h | CoverageMatrixContractTest 校验正负例 | 同上 |
| D3 | 测试必须可发现 | 新增 smoke 测试注册项 | tests/contract/CMakeLists.txt | `ctest -N -R CoverageMatrixContractTest` 可发现 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：`contracts/include/boundary/CoverageMatrixGuards.h`
- 测试目标：`tests/contract/smoke/CoverageMatrixContractTest.cpp`
- 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CoverageMatrixContractTest --output-on-failure`
4. 范围未越界，满足进入 Build 条件。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：实现覆盖目录、对象映射与测试类别映射。
2. B2：实现执行快照、对象覆盖判定与首缺口输出。
3. B3：实现守卫总验证函数与通过/失败原因。
4. B4：实现正例、负例 contract tests 并注册。
5. B5：执行构建、测试、可发现性验收并回写 TODO 证据。

## 7. Build 合规复核

1. 新增代码中的对象目录、判定流程、测试场景均补充语义注释。
2. 测试覆盖正负例：完整覆盖快照正例 + 缺失序列化/ADR 测试负例。
3. 触及测试注册，已纳入可发现性验证。
4. TODO 状态与证据已同步回写。

## 8. Blocker 状态

当前无 blocker。