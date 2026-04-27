# COG-TODO-018 BeliefUpdateSynthesizer 收敛

状态：Done
日期：2026-04-27
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready belief writeback hint implementation

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.13.3 已冻结 `BeliefUpdateSynthesizer` 的职责：把 `PerceptionResult`、`ActionDecision`、`ReflectionDecision` 与 `latest Observation` 中可验证的事实折叠为 `BeliefUpdateHint`，供 Runtime / Memory 后续写回。
2. 同一章节明确边界：`BeliefUpdateSynthesizer` 不直接写 `MemoryStore`，不重新解释 shared `BeliefState` schema，不替代 `ReflectionEngine` / `Reasoner` 做语义裁定，也不生成未经证据支持的持久结论。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §6.14.4 已冻结写回时序协议：`BeliefUpdateHint` 的返回与 Memory 写回时机都由 Runtime 控制；单次 hint 内所有 delta 必须原子提交；写回失败是 best-effort 事件，不阻塞主链；冲突消解由 Memory 负责，cognition 只提供 `merge_mode` 建议。
4. `cognition/include/belief/BeliefUpdateHint.h` 已把 supporting type 冻结为 delta-oriented 结构：`confirmed_facts_delta`、`hypotheses_delta`、`assumptions_delta`、`evidence_refs_delta`、`missing_evidence_refs`、`confidence_hint`、`merge_mode`。
5. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中的 COG-TC018 明确禁止 cognition 直接写 memory 或直接触发 context reload；COG-TODO-018 的完成判定则聚焦 delta 分类、evidence 去重、merge mode 输出正确，以及不越权触发写回事务。

## 2. 外部参考

1. Microsoft Azure Architecture Center 的 Compensating Transaction pattern 指出：在 eventual consistency 场景中，失败恢复通常需要应用特定规则；系统应优先尝试前向进展或重试，把补偿 / 回滚 / 人工裁定交给持有全局事务与恢复控制权的编排器，而不是让单个步骤自行执行业务恢复：https://learn.microsoft.com/en-us/azure/architecture/patterns/compensating-transaction

本轮借鉴点：`BeliefUpdateSynthesizer` 只生成 best-effort 的 belief writeback hint，并把 `merge_mode`、evidence refs 与 confidence 交给 Runtime / Memory；真正的写回时机、冲突消解与失败处理仍由外层编排控制，不在 cognition 内部自行提交或补偿。

## 3. 主结论

1. 新增私有 `BeliefUpdateSynthesizer`，落在 `cognition/src/belief/BeliefUpdateSynthesizer.h/.cpp`，提供 `synthesize_from_decide()`、`synthesize_from_reflection()`、`merge_deltas()`、`normalize_evidence_refs()`。
2. decide 路径当前收敛为：
   - 仅把具备证据的高置信度 entities 折叠为 `confirmed_facts_delta`；
   - 把 selected tool / selected node 折叠为 best-effort hypotheses；
   - 把 successful observation 折叠为 observation fact；
   - 把 ambiguity 中的 `missing_evidence_refs` 透传给 Runtime。
3. reflection 路径当前收敛为：
   - 解析 `ReflectionDecision` 中显式列出的 invalidated assumptions，并输出 `Retract` 型 `assumptions_delta`；
   - `RetryStep` / `AbortSafe` 等反思结论折叠为弱写回 hypotheses；
   - `Replan` 映射到更强的 `BeliefMergeMode::Replace`，其余反思建议维持 `Merge` 或 `Append`。
4. `merge_deltas()` 负责在多来源 hint 间选择最强 `merge_mode`，保留更强的 `confidence_hint`，并合并各类 delta；`normalize_evidence_refs()` 负责移除空字符串、重复 evidence ref，并在 `missing_evidence_refs` 中剔除已知 evidence refs。
5. `drop_unverified_delta()` 让无任何 evidence ref 的 hint 丢弃 facts / hypotheses / assumptions delta，避免在无证据场景下补造持久结论；整条链路保持 suggestion-only，不直接触发 Memory 写事务。

## 4. 边界与职责

| 组件 | 职责 | 非职责 |
|---|---|---|
| `BeliefUpdateSynthesizer` | 把 stage outputs 折叠为 `BeliefUpdateHint` | 不直接写 Memory；不裁定写回时机；不做 context reload |
| `BeliefUpdateHint` | 向 Runtime / Memory 提供 best-effort 写回建议 | 不等于 shared BeliefPatch；不承载事务控制与冲突解决 |
| Runtime / Memory | 决定写回时机、原子提交、冲突消解和失败审计 | 不把 belief delta 分类 owner 反向塞回 cognition |

## 5. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| decide 路径需要把证据支持的 stage output 折叠为 delta | `cognition/src/belief/BeliefUpdateSynthesizer.cpp` + `BeliefUpdateSynthesizerTest.cpp` | verified entity / observation / tool hint 被折叠为事实、假设与 evidence refs |
| reflection replan 需要产生更强的 belief 覆盖建议 | `BeliefUpdateSynthesizer.cpp` + `BeliefUpdateMergeModeTest.cpp` | invalidated assumptions 输出 `Retract`，且 `merge_mode=Replace` |
| evidence refs 必须去重并清理缺口列表 | `BeliefUpdateSynthesizer.cpp` + `BeliefUpdateEvidenceDedupTest.cpp` | 空值、重复值与已知 evidence refs 不再残留 |
| belief writeback 仍是 best-effort hint | 组件 API 只处理 stage objects 与 hint | 无任何 Memory / Context 写入调用或事务控制代码 |

## 6. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增私有 `BeliefUpdateSynthesizer` 与 decide / reflection 折叠逻辑 | 决策 / 反思 delta 分类可局部断言 | `Build_CMakeTools(buildTargets=["dasall_belief_update_synthesizer_unit_test","dasall_belief_update_merge_mode_unit_test","dasall_belief_update_evidence_dedup_unit_test"])` | 若 delta 分类越权，优先回退到更保守的空 hint 或 hypothesis-only hint |
| B2 | 落 `merge_deltas()` 与 `normalize_evidence_refs()` | strongest merge mode 与 evidence dedup 可直接验证 | `./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_synthesizer_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_merge_mode_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_evidence_dedup_unit_test` | 若 merge/evidence 规则漂移，只修同一 slice 的 hint 归一化逻辑 |
| B3 | 注册 belief source 与三条 focused unit targets | discoverability 与直接执行成立 | 同上 | 若接线扩大，只保留 cognition belief slice 内的最小改动 |

## 7. 验证证据

1. `Build_CMakeTools(buildTargets=["dasall_belief_update_synthesizer_unit_test","dasall_belief_update_merge_mode_unit_test","dasall_belief_update_evidence_dedup_unit_test"])`
   - 第一次结果：失败；局部编译错误为 `append_unique_delta()` 模板分支误落到 `evidence_ref` 字段，导致非 `EvidenceRefDelta` 类型也尝试访问该字段。
   - 修补同一 slice 后复跑：通过。
2. `./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_synthesizer_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_merge_mode_unit_test && ./build/vscode-linux-ninja/tests/unit/cognition/dasall_belief_update_evidence_dedup_unit_test`
   - 结果：通过；三条 belief-update-focused unit tests 均零输出退出。

## 8. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 范围控制 | PASS：只新增 belief 私有组件、三条 focused tests 与最小 CMake 接线 |
| suggestion-only 边界 | PASS：只输出 `BeliefUpdateHint`，未引入 Memory 写事务或 context reload 调用 |
| 正例覆盖 | PASS：覆盖 decide-path facts/hypotheses、reflection-path retract / replace |
| 去重与归一化 | PASS：覆盖 evidence 去重、空值清理与 missing-evidence overlap 清理 |
| best-effort 语义 | PASS：无 evidence 场景下会丢弃未经证据支持的 delta，而不是补造事实 |
| ADR-006 / 交互契约一致性 | PASS：写回时机与冲突消解仍保留在 Runtime / Memory |