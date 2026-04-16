# TOOL-TODO-024 tests/integration/tools 拓扑收敛

日期：2026-04-16  
任务：TOOL-TODO-024  
状态：D Gate PASS

## 1. 本地证据

1. docs/ssot/InfraIntegrationTopology.md 已冻结 integration 顶层接线要求：新增核心链路组件必须至少补 1 条 smoke integration 用例，且 `ctest -N` 必须可发现。
2. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 `TOOL-BLK-003` 明确指出 `tests/integration/tools` 当前不存在，discoverability 为 0；该阻塞会直接影响后续 025、026 以及 observability / workflow / MCP / skill integration 任务。
3. 现有顶层 tests/integration/CMakeLists.txt 只接入 infra / profiles / platform / services / llm，tools integration 变量导出链不存在，因此 024 的唯一主目标是补齐 topology 与 discoverability，不提前宣称 builtin 或 observability 语义闭环已经完成。

## 2. Design 结论

1. 新增 `tests/integration/tools` 子目录，采用与 services / llm 一致的“本地注册宏 + 导出 target 列表到顶层”的模式，避免后续 025~042 继续手工硬编码顶层 integration target 列表。
2. 以 `ToolServicesSmokeIntegrationTest` 作为 tools integration 的首个 discoverability 锚点，命名符合 SSOT 的 `<Component><Scenario>IntegrationTest` 约定，并挂 `integration;tools` 标签。
3. smoke 用例只验证默认 ToolManager 能通过 builtin route 走到 `ToolResult + Observation + ObservationDigest` 基本闭环，作为 topology 打开后的最小活性证明；query/action 细分语义与 observability 完整字段验收继续留给 025 / 026。
4. 顶层 `tests/integration/CMakeLists.txt` 现已接入 tools 子目录，并将 `${DASALL_TOOLS_INTEGRATION_TEST_EXECUTABLE_TARGETS}` 纳入 `DASALL_INTEGRATION_TEST_EXECUTABLE_TARGETS`，从而正式关闭 `TOOL-BLK-003`。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| tools integration 子目录接线 | tests/integration/tools/CMakeLists.txt |
| 顶层 integration 聚合接入 | tests/integration/CMakeLists.txt |
| 首个 tools smoke discoverability 锚点 | tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp |
| blocker / TODO / worklog 回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md、docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tests/integration/tools/CMakeLists.txt
   - tests/integration/CMakeLists.txt
   - tests/integration/tools/ToolServicesSmokeIntegrationTest.cpp
2. 测试目标：
   - `ToolServicesSmokeIntegrationTest`
   - `ctest -N` discoverability 检查
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N`

## 5. 本地验证

1. 构建：
   - Build_CMakeTools: `dasall_tool_services_smoke_integration_test`
   - Build_CMakeTools: `dasall_integration_tests`
2. 定向执行：
   - RunCtest_CMakeTools: `ToolServicesSmokeIntegrationTest`
3. discoverability：
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "ToolServicesSmokeIntegrationTest|Total Tests"`
4. 结果摘要：
   - `ToolServicesSmokeIntegrationTest` 通过。
   - `ctest -N` 已显式发现 `ToolServicesSmokeIntegrationTest`，总测试数为 `474`。
   - tools integration 现已进入顶层 `dasall_integration_tests` 聚合路径。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，但未影响 discoverability / smoke 结论。

## 6. 风险与回退

1. 当前 `ToolServicesSmokeIntegrationTest` 只作为 discoverability 与最小活性锚点，不代表 025 的 builtin query/action 语义闭环已经完全验收；后续必须在同名测试或其直接扩展上补齐真实字段断言。
2. 024 只打开 tools integration 门，不处理 observability / workflow / MCP / skill 具体语义；这些任务仍需各自的专用 integration 测试完成后才能宣称闭环通过。