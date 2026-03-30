# WP05-T016 ADR 边界回归测试矩阵

最近更新时间：2026-03-22
任务状态：Done
任务编号：WP05-T016
上游输入：WP-04 冻结包、ADR-006、ADR-007、ADR-008、WP05-T016 主任务定义

## 0. Phase 0 研究学习证据链

### 本地证据清单

1. `contracts/include/context/ContextPacket.h` 明确 ContextPacket 不得承载 `rendered_prompt`、`provider_payload` 等消息层字段。
2. `contracts/include/prompt/PromptBoundaryContracts.h` 冻结 ADR-006 的 ContextPacket 边界拒绝表，`evaluate_context_packet_prompt_field_boundary` 是稳定判定入口。
3. `contracts/include/checkpoint/ReflectionDecision.h` 与 `contracts/include/checkpoint/ReflectionDecisionGuards.h` 明确 ReflectionDecision 只表达语义建议，不承载调度字段。
4. `contracts/include/boundary/RecoveryBoundaryGuards.h` 冻结 ADR-007 禁区字段，`retry_after_ms`、`backoff_strategy` 等必须被拒绝。
5. `contracts/include/agent/MultiAgentResult.h` 与 `contracts/include/agent/MultiAgentResultGuards.h` 明确 MultiAgentResult 不能替代顶层 AgentResult。
6. `contracts/include/boundary/MultiAgentBoundaryGuards.h` 冻结 ADR-008 禁区字段，`agent_result`、`final_agent_response` 必须被拒绝。
7. `contracts/include/boundary/ADRFieldMappingGuards.h` 提供 ADR 映射目录，可用于回归校验“对象边界 + 禁区字段 + guard 分发”链路。

### 外部参考清单

1. Martin Fowler, "Evolutionary Architecture"（强调架构决策需通过可执行回归保护避免边界回退）：https://martinfowler.com/articles/evolutionary-architecture.html
2. Thoughtworks Technology Radar（将 automated governance 与 architectural fitness functions 作为长期演进实践）：https://www.thoughtworks.com/radar

### 对本任务的可落地启发

1. ADR 边界需要用“对象 guard + 字段边界 guard + 映射目录”三层组合回归，而不仅是单一对象正例。
2. 回归测试应覆盖主链条三类对象：ContextPacket（ADR-006）、ReflectionDecision（ADR-007）、MultiAgentResult（ADR-008）。
3. 负例要优先选用 ADR 明确禁区字段，避免测试语义漂移。
4. 回归测试应额外验证目录映射仍包含关键对象和关键字段，阻断“代码还在、映射丢失”的软回退。

## 1. 任务理解

本任务只处理 WP05-T016：

1. 产出 ADR 边界测试矩阵（Design）。
2. 新增 `tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp`（Build）。
3. 注册到 contract tests 并验证可执行与可发现。

本任务不处理：

1. 改写 ADR-006/007/008 文本决策。
2. 改写 ContextPacket、ReflectionDecision、MultiAgentResult 的对象定义。
3. 扩展到 WP05-T017 覆盖矩阵自动检查任务。

## 2. Design 原子清单

1. D1：定义 ADR-006/007/008 三对象边界回归矩阵。
- 输入依据：上述本地证据 1-7。
- 产出路径：本文件第 3 节矩阵 + `ADRBoundaryRegressionContractTest.cpp`。
- 完成判定：每个 ADR 至少 1 个正例与 1 个负例被回归覆盖。
- 风险与回退：若负例覆盖不足，优先补充“冻结禁区字段”断言而非扩张对象范围。

2. D2：锁定映射目录一致性回归。
- 输入依据：`ADRFieldMappingGuards.h`。
- 产出路径：`ADRBoundaryRegressionContractTest.cpp`。
- 完成判定：关键对象与关键禁区字段映射存在且 guard 分发能拒绝对应字段。
- 风险与回退：若目录或分发表回退，测试应直接失败并给出对象/字段定位。

3. D3：锁定 Build 三件套与发现性证据。
- 输入依据：`tests/contract/CMakeLists.txt`。
- 产出路径：`tests/contract/CMakeLists.txt`。
- 完成判定：构建后 `ctest --test-dir build-ci -N -R ADRBoundaryRegressionContractTest` 可发现 1 个测试。
- 风险与回退：若不可发现，先修注册项再复跑验收命令。

## 3. ADR 边界回归测试矩阵

| 序号 | ADR | 对象 | 回归场景 | 断言类型 | 期望结果 |
|---|---|---|---|---|---|
| M1 | ADR-006 | ContextPacket | 合法语义字段对象通过 field-rules | 正例 | `validate_context_packet_field_rules` 通过 |
| M2 | ADR-006 | ContextPacket | `rendered_prompt` 消息层字段注入 | 负例 | `evaluate_context_packet_prompt_field_boundary` 拒绝 |
| M3 | ADR-007 | ReflectionDecision | 合法语义建议对象通过 field-rules | 正例 | `validate_reflection_decision_field_rules` 通过 |
| M4 | ADR-007 | ReflectionDecision | `retry_after_ms` 调度字段注入 | 负例 | `validate_reflection_decision_contract_field_boundary` 拒绝 |
| M5 | ADR-008 | MultiAgentResult | 合法协作结果对象通过 field-rules | 正例 | `validate_multi_agent_result_field_rules` 通过 |
| M6 | ADR-008 | MultiAgentResult | `agent_result` 顶层结果替代字段注入 | 负例 | `validate_multi_agent_result_forbidden_field` 拒绝 |
| M7 | ADR-006/007/008 | 映射目录 | 关键对象与关键禁区字段仍在目录中 | 回归守卫 | `is_adr_object_mapped` 与 `has_adr_forbidden_field_mapping` 均为 true |

## 4. Design -> Build 映射

| D 原子项 | 设计结论 | 对应 Build 动作 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| D1 | 三个 ADR 边界需同时回归 | 新增 3 正例 + 3 负例测试函数 | tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp | 可阻断 ContextPacket/ReflectionDecision/MultiAgentResult 越界 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ADRBoundaryRegressionContractTest --output-on-failure |
| D2 | 目录映射回退需要被捕获 | 新增映射完整性回归测试函数 | tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp | 可阻断映射目录或 guard 分发表回退 | 同上 |
| D3 | 测试必须可发现 | 新增 CMake 注册项 | tests/contract/CMakeLists.txt | 构建后 `ctest -N -R` 可发现 1 个测试 | 同上 |

## 5. D Gate 结果

1. D 文档已落盘。
2. Design 原子清单具备二值完成判定。
3. Build 三件套已锁定：
- 代码目标：`tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp`
- 测试目标：`tests/contract/smoke/ADRBoundaryRegressionContractTest.cpp`
- 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ADRBoundaryRegressionContractTest --output-on-failure`
4. 范围未越界，满足进入 Build 条件。

Gate 结论：PASS。

## 6. Build 执行清单

1. B1：实现 ContextPacket/ReflectionDecision/MultiAgentResult 的稳定合法样本构造函数。
2. B2：实现三对象正例（field-rules pass）回归。
3. B3：实现三对象 ADR 禁区字段负例回归。
4. B4：实现 ADR 映射目录关键对象与关键字段存在性回归。
5. B5：注册测试并执行验收命令。

## 7. Build 合规复核

1. 新增测试 helper、场景意图、runner 均补齐语义注释。
2. 覆盖正负例（本轮 3 正例 + 4 负例）。
3. 触及测试注册，已纳入可发现性验证要求。
4. TODO 状态与证据将同步回写。

## 8. Blocker 状态

当前无 blocker。