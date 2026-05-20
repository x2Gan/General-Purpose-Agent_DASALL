# TOOL-FIX-013 ResultProjector 生产 payload regression 收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-GAP-011`。
2. 本轮目标：为 `ResultProjector` 冻结真实 ToolResult payload golden set，至少覆盖 runtime live `agent.dataset` query 的 object-array payload 与 workflow receipt payload，并确保关键字段不会在 deterministic projection 中丢失。
3. 完成判定：`tests/unit/tools/ResultProjectorTest.cpp` 已收录上述两类真实 payload；`tools/src/projection/ResultProjector.cpp` 已能从 object-array payload 的首项对象提取顶层 key facts；focused projector unit tests 通过；本轮不依赖 qemu / kvm，也不把结果外推为 machine-isolated release / soak ready。

## 2. 本地证据

1. 既有 `ResultProjectorTest.cpp`、`ResultProjectorTruncationTest.cpp` 与 `ResultProjectorConfidenceTest.cpp` 主要覆盖 small fixture object、plain text truncation 与 failure secrecy，没有锁住真实 production payload 形状。
2. runtime live `agent.dataset` query 的 production-shaped payload 由 `services/src/ServiceLiveComposition.cpp` 生成，形状为 object array，例如 `[{"capability_id":"agent.dataset","target_id":"builtin:agent.dataset","projection":"default"}]`；旧版 `ResultProjector` 对数组只输出 `items=[{...}]` 预览，导致 `capability_id` / `projection` 不进入 key facts。
3. workflow receipt payload 由 `tools/src/execution/WorkflowEngine.cpp` 的 `serialize_receipt()` 生成，包含 `workflow_id`、`status`、`completed_step_ids`、`skipped_step_ids` 与 `compensation_hint_count` 等顶层字段，是另一类需要冻结的真实 payload。
4. 本轮已更新 `tests/unit/tools/ResultProjectorTest.cpp`，新增 live query array payload 与 workflow receipt payload golden case，并先以 failing regression 证明 array-field 丢失确实存在，再用最小实现修复。
5. 本轮已更新 `tools/src/projection/ResultProjector.cpp`：若 payload 为 array 且首项是 object，则优先投影该对象的顶层字段；若不是 object-array，则继续回退为既有 `items=[...]` 预览。
6. `ResultProjectorTruncationTest` 与 `ResultProjectorConfidenceTest` 已在本轮修复后复跑通过，说明共享 projection 逻辑没有回退现有 truncation / confidence 语义。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-GAP-011` 的真实缺口不是 `ResultProjector` 完全缺测试，而是没有用真实 production-shaped payload 做 golden regression。
2. fixture object payload 无法暴露 object-array payload 的语义丢失；因此此前测试即使全绿，也不能说明 live query path 的 key facts 保留是可靠的。
3. live query object-array payload 把 `capability_id` / `projection` 放在数组首项对象内，旧逻辑统一折叠为 `items=[{...}]`，确实会让 key facts 丢失。

### 3.2 本轮决定

1. golden set 固定为两类 payload：live query object-array payload 与 workflow receipt payload。
2. array payload 修复保持最小化：只在首项为 object 时提取其顶层字段，不引入深层递归摘要、新配置或新的 projection owner。
3. summary fallback 语义保持不变；本轮只修 key facts retention，不改 `ResultProjector` 的 deterministic summary contract。

### 3.3 边界与不外推项

1. 本轮不修改 `ServiceLiveComposition`、`WorkflowEngine` 或 `ToolManager` 的 payload owner，只消费它们当前的真实输出形状。
2. 本轮不引入 LLM-assisted summary，也不把 `ResultProjector` 的 deterministic owner boundary 扩张到 response 或 cognition。
3. 本轮不新增 qemu / autopkgtest / soak 证据；`TOOL-GAP-012` 继续保留 machine-isolated release hardening 范围。
4. 若未来 live query payload 从单对象数组扩展为多对象异构数组，需由后续任务显式扩展 golden set，而不是在本轮隐式放宽语义。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | 冻结 live query object-array payload golden set | `tests/unit/tools/ResultProjectorTest.cpp` |
| D2 | 冻结 workflow receipt payload golden set | `tests/unit/tools/ResultProjectorTest.cpp` |
| D3 | object-array payload 需要保留首项对象的顶层关键字段 | `tools/src/projection/ResultProjector.cpp` |
| D4 | 共享 projector 修复后必须复跑 truncation / confidence regression | `tests/unit/tools/ResultProjectorTruncationTest.cpp`、`tests/unit/tools/ResultProjectorConfidenceTest.cpp` |
| D5 | gap closeout 结论必须回写总账、deliverable 索引与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：`ResultProjector` 在真实 production payload 形状上保留关键字段，不再把 live query object-array payload 压扁成无法检索的 `items=[{...}]`。
2. 测试目标：focused unit tests 同时锁住 live query golden payload、workflow receipt payload，以及既有 truncation / confidence regression。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_result_projector_unit_test"])`
   - `RunCtest_CMakeTools(tests=["ResultProjectorTest"])`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_result_projector_unit_test`
   - `Build_CMakeTools(buildTargets=["dasall_result_projector_truncation_unit_test","dasall_result_projector_confidence_unit_test"])`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_result_projector_truncation_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_result_projector_confidence_unit_test`

## 6. Rollout Checklist

1. live `agent.dataset` query array payload 已有 focused golden regression，能显式断言 `capability_id` / `projection` 被保留。
2. workflow receipt payload 已有 focused golden regression，能显式断言 `workflow_id` / `status` / `completed_step_ids` 被保留。
3. shared projector 修复后，`ResultProjectorTruncationTest` 与 `ResultProjectorConfidenceTest` 未回退。
4. 本轮结论只覆盖 build-tree focused projector regression，不外推为 qemu / release / soak ready。

## 7. 风险与回退

1. 当前 array payload 修复只提取首项对象字段；若未来需要对多对象数组做 richer summary，应通过新任务显式设计，而不是继续在本函数内隐式扩张。
2. 若 payload 不是 object-array，当前逻辑仍保留原有 `items=[...]` 回退，不会破坏既有 array preview contract。
3. 若把本轮 closeout 误写成“所有复杂 payload 已有完整 golden set”，会错误吞并尚未覆盖的 MCP / multi-item array / nested object drift 场景。
4. `RunCtest_CMakeTools` 当前仍有仓库已知泛化 `生成失败`，本轮 focused validation 仍需 direct-binary fallback；若文档误写成 CMake Tools runner 已恢复，会掩盖工具态问题。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 golden payload、array-field retention、focused projector regressions 与文档回写。
3. Build 三件套已在本机完成；`RunCtest_CMakeTools` 的已知泛化失败已通过 direct-binary fallback 收口 focused validation。
4. 范围保持在 `ResultProjector` 生产 payload regression，不扩张到 qemu / release / soak。

结论：D Gate = PASS；`TOOL-GAP-011` 已按真实 payload golden regression、array payload 关键字段保留与 focused projector evidence 收口。