# INT-TODO-024 系统集成 Gate 与交付证据回写收口

状态：Done
日期：2026-05-06
来源 TODO：docs/todos/integration/DASALL_系统集成专项TODO.md

## 1. 任务边界与前置检查

1. 本任务只处理系统级文档证据闭环，不新增代码 Gate 或测试实现。
2. 前置依赖复核：`INT-TODO-023` 已完成 `Gate-INT-09` 的 discoverability 与 one-shot acceptance 命令层；`INT-TODO-030` 已完成 `Gate-INT-08` 的 Access focused ingress 证据分层。
3. 本任务的正式输出面固定为三类长期资产：
   - `docs/todos/integration/DASALL_系统集成专项TODO.md`
   - `docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md`
   - `docs/worklog/DASALL_开发执行记录.md`

## 2. 命令证据

> 说明：VS Code CMake Tools 当前 active preset 为 `vscode-linux-ninja`，当前 build 的 discoverability 以 `build/vscode-linux-ninja` 为准；聚焦 Gate 命令继续沿用 `Build_CMakeTools` 作为正式入口。

1. `Build_CMakeTools(target=dasall_gate_int_09)`
2. `Build_CMakeTools(target=dasall_gate_int_08)`
3. `ctest --test-dir build/vscode-linux-ninja -N`
4. `rg -n "Gate-INT-|INT-TODO-024|dasall_gate_int_08|dasall_gate_int_09|记录 #56[4-9]|记录 #570|记录 #571" docs/todos/integration/DASALL_系统集成专项TODO.md docs/todos/integration/deliverables/INT-TODO-024-系统集成Gate与交付证据回写收口.md docs/worklog/DASALL_开发执行记录.md docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md docs/ssot/SystemIntegrationGateMatrix.md`

结果摘要：

1. `dasall_gate_int_09` 已在 2026-05-06 通过，负责当前阶段 `Gate-INT-03~07` 的 discoverability + one-shot acceptance。
2. `dasall_gate_int_08` 已在 2026-05-06 通过，负责 Access v1 unary focused ingress 的独立 release gate。
3. 当前 build `ctest -N` 可以统一发现 `Gate-INT-03~09` 相关正式 test-name / labels / targets，不再依赖手工散写测试名。
4. TODO、deliverable、worklog、review doc 与 `SystemIntegrationGateMatrix` 的 Gate 命名、正式命令与残余风险口径已由本任务统一回写。

## 3. Gate 回写结论

| Gate | 当前状态 | 正式命令 / 证据 | 长期交付物路径 | 当前状态说明 | 后继任务 | 残余风险 |
|---|---|---|---|---|---|---|
| Gate-INT-01 | Pass | `rg -n "Gate-INT-|fixture gate|true integration|discoverability|worklog" docs/ssot/SystemIntegrationGateMatrix.md docs/todos/integration/DASALL_系统集成专项TODO.md docs/worklog/DASALL_开发执行记录.md` | `docs/ssot/SystemIntegrationGateMatrix.md` | design / SSOT 边界已冻结，命令权威来源与分层规则已明示 | 仅在 SSOT 或 TODO 发生新漂移时再开任务 | 若后续文档漂移且未同步回写，系统 Gate 口径会重新失真 |
| Gate-INT-02 | Pass | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "ContextPacketMainFlowContractTest|ContextPacketFieldContractTest|RetrievalEvidenceRefContractTest" --output-on-failure` | `docs/ssot/RetrievalEvidenceProjectionV1.md` | RetrievalEvidenceRef shared surface 与 memory/runtime seam additive contract 已落地 | 仅在 shared evidence surface 再演进时追加 contract 回写 | 若后续扩面超出 additive + optional 边界，会冲击 contracts 稳定性 |
| Gate-INT-03 | Pass | `Build_CMakeTools(target=dasall_gate_int_03)` | `docs/worklog/DASALL_开发执行记录.md` | default unary 主 Gate 已固化为 `dasall_gate_int_03` + `gate-int-03` / `default-unary-gate` | 维持 focused gate 绿态，防止后续实现漂移 | 若 true integration 再次红灯，fixture 证据不得冒充系统 ready |
| Gate-INT-04 | Pass | `Build_CMakeTools(target=dasall_gate_int_04)` | `docs/worklog/DASALL_开发执行记录.md` | structured evidence preservation 主链与 reject path 已统一收敛到 `dasall_gate_int_04` | 后续 evidence surface 调整时保持 gate 名称与命令不漂移 | 不得用 knowledge / memory 局部 smoke 替代主链 preservation gate |
| Gate-INT-05 | Pass | `Build_CMakeTools(target=dasall_gate_int_05)` | `docs/worklog/DASALL_开发执行记录.md` | diagnostics retained snapshot round-trip 已固化为独立系统 Gate | 后续 diagnostics 变更必须保留 retained snapshot focused gate | diagnostics façade 或局部单测不能替代 retained snapshot Gate |
| Gate-INT-06 | Pass | `Build_CMakeTools(target=dasall_gate_int_06)` | `docs/worklog/DASALL_开发执行记录.md` | required/optional 与 degraded semantics 已由 `dasall_gate_int_06` 统一守住 | 持续用 focused gate 守住 profile / readiness 语义 | supporting smoke 绿不等于 default-ready 语义自动成立 |
| Gate-INT-07 | Pass | `Build_CMakeTools(target=dasall_gate_int_07)` | `docs/worklog/DASALL_开发执行记录.md` | tools/services success-error-code 三元语义已有 contract + unit 双层 Gate | 后续若扩充 result triad 字段，必须同步更新 gate 与 contract | 局部 smoke 绿不能覆盖 triad contract 漂移 |
| Gate-INT-08 | Pass | `Build_CMakeTools(target=dasall_gate_int_08)` | `docs/todos/access/DASALL_access子系统专项TODO.md` | Access v1 unary focused ingress 已收敛到 formal alias、gate label 与独立 target | 后续仅在 Access focused matrix 扩容时更新对应 TODO / worklog / review doc | mock pipeline、ping liveness、局部 envelope 字段不得冒充 release evidence |
| Gate-INT-09 | Pass | `Build_CMakeTools(target=dasall_gate_int_09)` + `ctest --test-dir build/vscode-linux-ninja -N` + 本任务文档回写 | `docs/todos/integration/deliverables/INT-TODO-024-系统集成Gate与交付证据回写收口.md` | discoverability、one-shot acceptance、TODO/worklog/review doc 已形成长期证据闭环 | 当前用户请求的系统 Gate 固化序列已完成；后续只在出现新 Gate 或 review 漂移时再开新任务 | `dasall_gate_int_09` 当前仍只收敛 Gate-INT-03~07 + regression smoke；Access Gate 保持独立 focused 入口 |

## 4. Review 回写结论

1. `docs/architecture/DASALL_全局子系统集成评审报告-2026-05-06.md` 保留初始评审问题叙述，但新增 `Gate` 证据闭环附录，明确 2026-05-06 当天完成的 Gate-INT-03~09 正式命令、当前状态、交付物路径、下一步与残余风险。
2. 当 review doc 前半部分的初始问题描述与附录中的当前 Gate 状态发生冲突时，以附录为准；这保证评审文档既保留“为什么会有这些任务”的历史上下文，又不会继续输出过时状态。
3. 当前系统级结论已从“仅靠口头评审判断”升级为“可由 TODO / deliverable / worklog / review doc / `ctest -N` / focused targets 共同追溯”的长期证据资产。

## 5. 风险残留与后续动作

1. `Gate-INT-09` 的 one-shot acceptance 仍只覆盖 `Gate-INT-03~07` 与 regression smoke；`Gate-INT-08` 继续作为独立 focused release gate 维护。
2. 当前用户请求的串行范围 `INT-TODO-018 ~ 023、030` 及其文档闭环 `INT-TODO-024` 已全部完成；若后续继续系统集成专项，需要重新指定未完成任务，而不是继续重复 Gate 回写。
3. 若后续任一 Gate 的正式命令、test-name、label 或 target 发生变化，必须同步更新：
   - integration TODO
   - 对应专项 TODO
   - worklog
   - 本 deliverable 或下一份接续 deliverable
   - review doc 的 Gate 证据附录
