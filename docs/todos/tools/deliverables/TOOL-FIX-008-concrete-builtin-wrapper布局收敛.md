# TOOL-FIX-008 concrete builtin wrapper 布局收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-008`。
2. 本轮目标：把 `agent.terminal` 与 `agent.dataset` 从“BuiltinCatalog descriptor + ToolServiceBridge 通用映射”的散落模式收口为 concrete builtin wrapper 布局，并保持注册入口仍统一收敛到 `BuiltinCatalog`。
3. 完成判定：`tools/src/builtin/terminal`、`tools/src/builtin/dataset` 已落地；descriptor、schema ref、read-only / compensation 语义与 services request mapping 由 wrapper 固化；focused unit tests 已覆盖 dataset read-only query、terminal 高风险 confirmation 与 schema / argument mapping；本轮不扩张到 runtime production 可见工具面或 installed / qemu / kvm 证据。

## 2. 本地证据

1. `docs/architecture/DASALL_tools子系统详细设计.md` 6.2.1 已冻结 builtin 放置约定：descriptor、参数约束、ToolIR 到 services request 的映射应位于 `tools/src/builtin/` 内部目录，注册入口继续收敛到 `tools/src/registry/BuiltinCatalog.cpp`。
2. 同文档 6.2.2 已明确 `agent.terminal` 的推荐分层：tools 保留工具包装层，services 持有执行语义，platform 持有进程级 OS 细节。
3. 本轮前 `tools/src/registry/BuiltinCatalog.cpp` 内联声明 `agent.terminal` / `agent.dataset` descriptor，`tools/src/bridge/ToolServiceBridge.cpp` 只保留 generic `build_action_request()` / `build_query_request()`，说明 builtin-specific schema / argument mapping 仍无专属落点。
4. 本轮已新增 `tools/src/builtin/terminal/AgentTerminalTool.h/.cpp` 与 `tools/src/builtin/dataset/AgentDatasetTool.h/.cpp`，分别固定 terminal action wrapper 与 dataset query wrapper。
5. `tools/src/registry/BuiltinCatalog.cpp` 现改为直接消费 wrapper descriptor；`tools/src/bridge/ToolServiceBridge.cpp` 现保留 context / deadline / budget / freshness 等通用映射，同时把 `agent.terminal` action 与 `agent.dataset` query 的具体 request 组装委托给 wrapper。
6. 本轮已新增 `tests/unit/tools/AgentDatasetToolTest.cpp` 与 `tests/unit/tools/AgentTerminalToolPolicyTest.cpp`，并在 `tests/unit/tools/CMakeLists.txt` 注册；二者分别验证 dataset read-only + schema / query mapping，以及 terminal schema / action mapping + confirmation gate。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-GAP-008` 的根因不是 builtin descriptor 缺失，而是 builtin-specific 定义没有专属源码落点，后续新增 schema、参数约束或 services request mapping 时只能继续堆进 BuiltinCatalog 与 generic bridge。
2. 正确收口方式不是把执行细节下沉到 tools，也不是把注册逻辑扩散到 runtime/apps，而是在 tools 内建立 internal wrapper：wrapper 持有 descriptor 与 request mapping，BuiltinCatalog 只负责聚合注册，ToolServiceBridge 只负责公共上下文与通用派发。
3. `ToolPolicyGate` 与 `ToolManager` 的高风险确认边界无需重写；terminal wrapper 只要继续发布 `Action + !is_read_only` descriptor，既有 confirmation gate 就会保持生效。

### 3.2 边界与不外推项

1. 本轮不把 `agent.terminal` 加入 runtime production 默认可见工具面；`TOOL-GAP-009` 仍保持独立缺口。
2. 本轮不改写 services execution 后端，也不在 tools 内新增进程控制实现。
3. 本轮只收口 build-tree focused 单测证据；禁止使用 qemu / kvm，也不把当前结果外推为 installed / release 证据。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | builtin-specific descriptor 与 request mapping 需要 internal wrapper 落点 | `tools/src/builtin/terminal/AgentTerminalTool.h/.cpp`、`tools/src/builtin/dataset/AgentDatasetTool.h/.cpp` |
| D2 | builtin 注册入口继续集中在 BuiltinCatalog | `tools/src/registry/BuiltinCatalog.cpp` |
| D3 | ToolServiceBridge 保留通用 context/budget/freshness 组装，把 per-tool request mapping 委托给 wrapper | `tools/src/bridge/ToolServiceBridge.cpp` |
| D4 | 用 focused unit tests 锁定 read-only query、schema / argument mapping 与 confirmation gate | `tests/unit/tools/AgentDatasetToolTest.cpp`、`tests/unit/tools/AgentTerminalToolPolicyTest.cpp`、`tests/unit/tools/BuiltinExecutorLaneTest.cpp`、`tests/unit/tools/ToolServiceBridgeTest.cpp` |
| D5 | 将完成结论回写到 tools 总账、交付索引与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：新增 terminal / dataset builtin wrapper，并让 BuiltinCatalog 与 ToolServiceBridge 统一消费 wrapper 定义。
2. 测试目标：新增 `AgentDatasetToolTest`、`AgentTerminalToolPolicyTest`，同时复跑 `ToolServiceBridgeTest` 与 `BuiltinExecutorLaneTest` 证明 wrapper 接线没有打破既有 bridge / lane 行为。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_agent_dataset_tool_unit_test","dasall_agent_terminal_tool_policy_unit_test","dasall_tool_service_bridge_unit_test","dasall_builtin_executor_lane_unit_test"])`
   - `RunCtest_CMakeTools(tests=["AgentDatasetToolTest","AgentTerminalToolPolicyTest","ToolServiceBridgeTest","BuiltinExecutorLaneTest"])`
   - 若 `RunCtest_CMakeTools` 继续返回仓库已知泛化“生成失败”，则 direct-binary fallback：`build/vscode-linux-ninja/tests/unit/tools/dasall_agent_dataset_tool_unit_test`、`build/vscode-linux-ninja/tests/unit/tools/dasall_agent_terminal_tool_policy_unit_test`、`build/vscode-linux-ninja/tests/unit/tools/dasall_tool_service_bridge_unit_test`、`build/vscode-linux-ninja/tests/unit/tools/dasall_builtin_executor_lane_unit_test`

## 6. Rollout Checklist

1. `tools/src/builtin/` 目录已存在真实 wrapper，而不是只停留在设计建议。
2. `BuiltinCatalog` 不再内联 terminal / dataset descriptor 常量；descriptor 统一来自 wrapper。
3. `ToolServiceBridge` 不再把 terminal / dataset 继续走完全 generic mapping；wrapper 现在拥有 concrete schema / argument mapping 落点。
4. terminal 的 high-risk confirmation 仍由既有 policy gate 二值裁定，不在 wrapper 内引入新的交互流程。
5. 本轮不修改 runtime production visibility，也不把 build-tree 单测证据外推到 installed / qemu / kvm / release。

## 7. 风险与回退

1. 若继续把 builtin-specific 逻辑堆回 `BuiltinCatalog.cpp` / `ToolServiceBridge.cpp`，后续新增 builtin 时会再次出现 descriptor、schema 和 request mapping 同步漂移；本轮通过 wrapper 目录面避免该回归。
2. 若把高风险确认逻辑下沉到 wrapper，会与 `ToolPolicyGate` / `ToolManager` 既有主控边界冲突；本轮不采用。
3. 若把本轮结果误判为 runtime production terminal 已打通，会提前吞并 `TOOL-GAP-009`；本轮明确保持该缺口独立。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 wrapper、bridge、registry、tests 与文档回写。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 concrete builtin wrapper 布局，不扩张到 runtime visibility、installed package 或 services backend 改造。

结论：D Gate = PASS，可进入 `TOOL-FIX-008` Build 阶段并以 focused wrapper tests 收口。