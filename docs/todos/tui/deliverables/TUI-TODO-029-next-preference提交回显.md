# TUI-TODO-029 next preference 提交与回显

状态：Done
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只验证 `NextTurnPreference` 在已冻结的 daemon-backed submit seam 上能形成真实的提交与回显证据：selector draft 必须进入 `submit_turn` payload，回显必须来自 owner projection，而不是 UI 本地猜测。
2. 本任务不扩写 `TuiApp` 的 interactive submit loop，也不把 route refresh、session lifecycle、命令迁移或 packaging gate 混入同轮处理；这些分别仍由 `TUI-TODO-026`、`TUI-TODO-030~034` 负责。
3. 本任务完成标准是：`Auto`、`PreferDepth`、`PinModel` 三模式都能通过 `TuiNextPreferenceIntegrationTest` 给出稳定二值结果，其中 `PreferDepth` 未命中时回显 effective route，`PinModel` 失败时通过 owner projection/receipt 回显稳定 `route.*` 原因，不绕过 `ModelRouter` 语义。

## 2. 本地事实与设计依据

1. `docs/todos/tui/deliverables/TUI-TODO-004-next-turn-preference承载决策.md` 已冻结 `NextTurnPreference` 的真实链路：TUI draft -> `TuiIpcRequestEnvelope.submit_turn.payload.next_preference` -> access/runtime typed request-scope carrier -> llm-local route input -> `ModelRouter` -> `TuiModelRouteProjection` / `TuiTurnReceipt` 回显。
2. 同一 deliverable 已冻结失败语义：`Auto` 不因 selector 自身触发 fail-closed；`PreferDepth` 是 advisory，未命中时必须回显 effective route；`PinModel` 在 disallowed/unavailable/not-supported 时必须 fail-closed，稳定提交语义至少覆盖 `route_disallowed` / `route_unavailable`。
3. `TUI-TODO-027` 已补齐 route catalog projection 的 `verification_state` / `health` / `profile_allowlisted` 边界，`TUI-TODO-028` 已把这些字段接入 `TuiModelSelector` 真实消费；因此 029 的缺口不在 selector option 展示，而在“选中的 draft 是否进入 submit payload，以及 owner projection 是否把 effective route / fail-closed reason 回显回来”。
4. 本轮起点代码已经具备分段证据：`TuiIpcControllerTest` 与 `DaemonTuiDataSourceContractTest` 已分别证明 `submit_turn()` 会保留 `next_preference` payload、route catalog roundtrip 会保留 projection 字段；缺的只是把 selector draft、submit payload 和 route echo 串成一条 integration 证据链。

## 3. 落盘结论

### 3.1 新增跨层集成证据

1. 已新增 `tests/integration/tui/TuiNextPreferenceIntegrationTest.cpp`，使用 real `DaemonTuiDataSource` + scripted IPC + `TuiModelSelector` 覆盖三条真实提交/回显路径：
   - `Auto`：`submit_turn` payload 保持 `auto`，回显 route 仍显示当前 effective provider/model，且不伪造 route-level fail-closed reason。
   - `PreferDepth`：selector draft 会把 `preferred_depth_tier=deep` 写入 submit payload；回显 route 明确显示 effective depth 仍为 `standard`，证明 advisory 偏好未命中时走 effective route 回显，而不是假装 depth 已强制生效。
   - `PinModel`：selector draft 会把指定 provider/model 写入 submit payload；当 owner 返回 `route_unavailable` 时，`TuiTurnReceipt.reason_code` 与 `TuiModelRouteProjection.disabled_reasons` 会一并回显 fail-closed 语义，同时 effective route 保持旧 provider/model，不发生 silent fallback 冒充 pin 成功。

### 3.2 discoverability 与 focused 验证接线

1. `tests/integration/tui/CMakeLists.txt` 现已注册 `dasall_tui_next_preference_integration_test` 与 `TuiNextPreferenceIntegrationTest`，并把新目标接入 TUI integration target 聚合。
2. `tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp` 已同步把 `TuiNextPreferenceIntegrationTest` 纳入 discoverability guard，避免后续只留下单文件测试而失去顶层可发现性。

### 3.3 owner 边界保持不变

1. 本轮没有改写 `apps/tui/src/data/DaemonTuiDataSource.cpp` 的生产逻辑；现有 thin adapter 已足以把 next preference 原样交给 controller，并把 owner projection 原样返回给上层测试。
2. 本轮也没有改写 `apps/tui/src/view/TuiModelSelector.cpp` 的展示/归一化规则；029 的闭合点是补齐真实 integration evidence，证明 028 已完成的 selector draft 可以和 023/027 的 daemon-backed seam 共同支撑 submit echo。
3. 由于 `TuiApp` 当前仍无 interactive submit loop，本轮结论只证明 daemon-backed selector truth chain 已具备 focused integration evidence，不代表 full app submit UX 已完成，也不代表命令迁移可以提前放行。

## 4. Design -> Build 映射

| Build 项 | 锁定代码目标 | 锁定测试目标 | 锁定验收命令 |
|---|---|---|---|
| next preference submit echo integration | tests/integration/tui/TuiNextPreferenceIntegrationTest.cpp | TuiNextPreferenceIntegrationTest | Build_CMakeTools(buildTargets=["dasall_tui_next_preference_integration_test"]) |
| integration discoverability guard | tests/integration/tui/CMakeLists.txt、tests/integration/tui/TuiIntegrationTopologySmokeTest.cpp | TuiTestTopologyDiscoverability | Build_CMakeTools(buildTargets=["dasall_tui_integration_topology_smoke_integration_test"]) |
| focused test execution | tests/integration/tui/TuiNextPreferenceIntegrationTest.cpp | TuiNextPreferenceIntegrationTest | RunCtest_CMakeTools(tests=["TuiNextPreferenceIntegrationTest"]) |

## 5. 结果

1. `Build_CMakeTools(buildTargets=["dasall_tui_next_preference_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` 通过；新集成测试与 topology smoke target 均成功编译并链接。
2. `RunCtest_CMakeTools(tests=["TuiNextPreferenceIntegrationTest","TuiTestTopologyDiscoverability"])` 仍命中仓库已知泛化 `生成失败`；已按 repo 当前回退口径直接执行 `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_next_preference_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test`，通过。
3. 当前结果证明：`NextTurnPreference` 现在已有 focused integration evidence，能从 selector draft 进入 `submit_turn` payload，并由 owner projection/receipt 把 effective route 或 fail-closed reason 回显回来；`PreferDepth` 保持 advisory，`PinModel` 保持 fail-closed，且没有 silent fallback 冒充 `ModelRouter` 已接受 pin。

结论：`TUI-TODO-029` 已闭合。TUI route selector 真链路现在不再只停留在分段 unit/contract 证据，而是具备了 `draft -> submit_turn -> route/receipt echo` 的 focused integration 验证；后续工作应转入 `TUI-TODO-035` 的阶段证据收口，以及仍保持 Blocked 的 `TUI-TODO-030~034` 命令迁移门禁。