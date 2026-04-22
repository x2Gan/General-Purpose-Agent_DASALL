# KNO-TODO-031 knowledge 专项 Gate 与交付证据收敛

日期：2026-04-22
任务：KNO-TODO-031
状态：Evidence Written Back / Residual Blockers Recorded

## 1. 本地与外部证据

1. `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md` 已把 031 定义为 knowledge 专项串行链最后一个 L2 收口任务，要求回写 build / unit / integration / quality / blocker / rollback 证据，而不是继续扩张 retrieval 或 refresh 生产逻辑。
2. 027/028/029/030/032/033 已分别收敛 lexical smoke、failure/degrade、profile compatibility、quality regression、facade real refresh owner 与 refresh loop integration 的 targeted evidence，因此 031 的职责是统一写回 gate 命令、结果摘要、残余风险与后继动作。
3. 本轮实际采样的 build-ci 命令链包括：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_unit_tests`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -R "Knowledge|dasall_knowledge"`
   - `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure`
4. 采样结果显示：`ctest -N` 总测试数为 `601`，knowledge 作用域下可发现 20 条 `Knowledge*` / `dasall_knowledge*` 测试入口；`ctest -R "Knowledge|dasall_knowledge"` 为 `100% tests passed, 0 tests failed out of 20`；`RetrievalQualityRegressionTest` 为 `1/1 Passed`。
5. 031 同时暴露出两个不能被忽略的残余问题：
   - `cmake --build build-ci --target dasall_unit_tests` 在 `tests/unit/knowledge/FreshnessControllerTest.cpp` 处失败，根因为文件自身存在拼接损坏：`}
     #include <algorithm>` 与重复 `main()`，不是 030/033 引入的新回归；
   - `cmake --build build-ci --target dasall_integration_tests` 仍被既有 `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest` 拖住，但 aggregate 日志中 knowledge 6 条 integration tests 全部 Passed。

### 1.1 外部参考

1. Google SRE《Reliable Product Launches at Scale》强调 launch checklist 的价值在于把 gate、失败模式、流程和残余风险写成可复现证据，而不是依赖口头结论。031 的收口方式沿用这一原则：把 pass、blocked、external noise 三类结果拆开记录，避免误宣称全绿。

## 2. Gate 执行证据

| Gate ID | 结论 | 命令证据 | 结果摘要 |
|---|---|---|---|
| Gate-KNO-01 | PARTIAL | `cmake --build build-ci --target dasall_knowledge dasall_unit_tests` | `dasall_knowledge` 构建通过；`dasall_unit_tests` 被 `tests/unit/knowledge/FreshnessControllerTest.cpp` 的现存语法损坏阻塞，不能宣称 unit aggregate 全绿 |
| Gate-KNO-02 | PASS | `ctest --test-dir build-ci -N` | build-ci discoverability 总测试数为 `601`；knowledge 相关命名入口可发现 20 条 |
| Gate-KNO-03 | PASS | `ctest --test-dir build-ci --output-on-failure -R "Knowledge|dasall_knowledge"` | knowledge 命名范围内 20/20 全部通过，覆盖 facade、health、smoke、failure/degrade、profile compatibility、refresh loop 等主链 |
| Gate-KNO-04 | PASS | `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure` | retrieval quality regression gate 1/1 Passed；030 的 aggregate metric、relative regression 与 `hard_fail` 校验仍有效 |
| Gate-KNO-05 | PARTIAL | `cmake --build build-ci --target dasall_integration_tests` | aggregate integration 共 65 条测试，其中 knowledge 6 条全部 Passed；但 target 仍被无关 `InfraDiagnosticsSmokeTest` / `InfraDiagnosticsIntegrationTest` 失败拖住 |
| Gate-KNO-06 | PASS | `ctest --test-dir build-ci --output-on-failure -R "Knowledge|dasall_knowledge"` 与 033 deliverable | `KnowledgeRefreshLoopTest` Passed，说明 refresh success、busy reject 与 rollback 语义仍保持可回归 |
| Gate-KNO-07 | PASS | 本文件、专项 TODO、worklog | 031 已把 gate 命令、结果摘要、残余 blocker 与后继动作回写到可追溯文档 |

## 3. 阻塞变化与最小解阻

1. 030 与 033 已经关闭 quality regression gate 与 refresh loop integration 的实现缺口；031 启动时，knowledge 专项范围内不再缺少新的 build-ready 功能任务。
2. 本轮新增识别到一个 knowledge 内部残余 blocker：`tests/unit/knowledge/FreshnessControllerTest.cpp` 存在文件拼接损坏，导致 `dasall_unit_tests` 聚合目标在编译阶段失败。
3. 该 blocker 的表现形式是：
   - 文件中出现 `return 0; }#include <algorithm>` 的非法拼接；
   - 后半段重复定义 `main()`；
   - 因此这不是运行期 flaky，也不是 030/033 行为回归，而是需要单独修复的静态语法破损。
4. integration aggregate 侧仍保留两条既有跨模块失败：`InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest`。它们不登记为新的 knowledge blocker，但必须在 031 中继续显式记录，避免误把 aggregate red 解释成 knowledge regression。
5. 本轮没有尝试在 031 内部直接修复上述 blocker，因为 031 的职责是证据收口；后续若要恢复 full-green，需要单独立题修复 `FreshnessControllerTest.cpp` 并复跑 `dasall_unit_tests`。

## 4. 评审结论

1. KNO-TODO-031 完成。knowledge 专项 Gate 与交付证据已被集中回写，不再依赖口头描述。
2. 当前 knowledge 结论不是“全绿 PASS”，而是“证据完备、残余 blocker 已显式登记”：
   - PASS：discoverability、knowledge 命名范围回归、quality regression、refresh/rollback integration；
   - PARTIAL：unit aggregate 被 `FreshnessControllerTest.cpp` 语法破损阻塞；integration aggregate 被仓库既有 infra diagnostics 失败污染。
3. 因此，031 的完成态表示 evidence chain 已闭合，不表示 knowledge 专项已经具备无残余 blocker 的 full-green 发布结论。

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 8.2 Phase K5 / 9.3 Gate 与 blocker 收口 | 本文件、docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md |
| quality / refresh / integration 既有 targeted evidence 汇总 | 030、033 deliverables 与本轮 ctest 采样结果 |
| 残余 blocker 与后继动作 | docs/worklog/DASALL_开发执行记录.md |

## 6. Build 三件套

1. 代码目标：回写 knowledge 专项 Gate 的命令证据、结果摘要、残余 blocker 与后继动作；不在 031 内扩张生产语义。
2. 测试目标：明确区分 knowledge 自身 PASS 信号与聚合 target 的外部噪音，并把 unit aggregate blocker 记录为显式未解问题。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_unit_tests`
   - `cmake --build build-ci --target dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -R "Knowledge|dasall_knowledge"`
   - `ctest --test-dir build-ci -R RetrievalQualityRegressionTest --output-on-failure`

## 7. 风险与回退

1. `ctest -R "Knowledge|dasall_knowledge"` 不会自动覆盖 `RetrievalQualityRegressionTest`，因为该测试名不含 `Knowledge` 前缀；因此 quality gate 必须单独执行，不能误以为被 regex 覆盖。
2. 若后续仅看 `dasall_integration_tests` aggregate target，容易把仓库级 infra diagnostics 失败误记为 knowledge regression；后续汇报必须继续拆分“knowledge slice 全绿”和“aggregate target 被外部失败拖住”两个结论。
3. `FreshnessControllerTest.cpp` 的语法破损如果不单独修复，knowledge 无法宣称 full-green unit gate；后续需要独立 atomic task 修复该文件并复跑 `dasall_unit_tests`。
4. 回退策略：031 不引入代码行为变更，因此不存在生产回退；后续只需在 blocker 修复后重跑第 6 节命令链并更新本文件的 Gate 结论即可。