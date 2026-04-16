# TOOL-TODO-021 ToolTraceBridge 设计收敛

日期：2026-04-16
任务：TOOL-TODO-021
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 第 6.10、6.12.6 已冻结 tools observability 边界：`ToolTraceBridge` 负责把 tool invoke 的治理阶段映射到统一 tracing 面，且 exporter 故障不得影响主链返回。
2. TOOL-TODO-019、020 已把 `ToolObservabilityIntegrationTest` 收敛为 tools observability 的单一 integration 出口，因此 021 应沿用同一出口补 trace 断言，而不是再新开并行 trace integration。
3. 当前 tools -> services 调用上下文只传递 trace id，没有稳定的 parent span 传播面；因此 021 的可交付边界应保持在 tools 内部 root span、governance stage span 与 builtin lane span，不把跨模块 parent-child 传播伪装成已完成事实。

## 2. Design 结论

1. 新增 tools/src/ops/ToolTraceBridge.h/.cpp 作为 tools 内部 trace bridge，提供 `start_root_span()`、`start_stage_span()`、`with_span()`、`mark_success()`、`mark_error()` 与 bridge status 查询接口。
2. bridge 冻结 tracer scope 为 `tools/v1`，root span 固定为 `tool.invoke`，治理阶段 span 固定落在 `tool.validate`、`tool.policy`、`tool.route`，builtin lane execution span 固定为 `tool.execute.builtin`。
3. `ToolManager` 直接在 invoke pipeline 内同步驱动 trace bridge，而不是复用 audit/metrics hook 缓存。这样 trace span 可以完整拿到 request、profile、route reason、lane key 与 terminal envelope 事实。
4. root span 优先吸收 `ToolInvocationContext::trace` 里的 remote parent 事实；如果上游只给非十六进制 trace/span 标识，bridge 会稳定归一化为 frozen 长度的 lower-hex id，避免把上游输入格式缺陷泄漏给 infra tracing 合同。
5. bridge 采用 fail-open 语义：provider 缺失、tracer 获取失败、start/end span 失败都只进入 degraded 状态与 failure accounting，不改变 `ToolManager` 主返回面。
6. 021 明确保持 tools-local span 树，不在本轮修改 services parent span 传播；跨模块父子 span 对齐保留为后续独立设计/实现议题。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| tools 内部 trace bridge 本体 | tools/src/ops/ToolTraceBridge.h；tools/src/ops/ToolTraceBridge.cpp |
| ToolManager trace 接线 | tools/src/ToolManager.h；tools/src/ToolManager.cpp |
| tools target 接线 | tools/CMakeLists.txt |
| unit trace bridge 验证 | tests/unit/tools/ToolTraceBridgeTest.cpp；tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |
| integration trace bridge 验证 | tests/integration/tools/ToolObservabilityIntegrationTest.cpp |
| traceability 回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md；docs/worklog/DASALL_开发执行记录.md |

## 4. Build 三件套

1. 代码目标：
   - tools/src/ops/ToolTraceBridge.h
   - tools/src/ops/ToolTraceBridge.cpp
   - tools/src/ToolManager.h
   - tools/src/ToolManager.cpp
   - tests/unit/tools/ToolTraceBridgeTest.cpp
   - tests/integration/tools/ToolObservabilityIntegrationTest.cpp
2. 测试目标：
   - `ToolTraceBridgeTest`
   - `ToolManagerPipelineTest`
   - `ToolObservabilityIntegrationTest`
   - `ToolServicesSmokeIntegrationTest`
3. 验收基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L integration`

## 5. 本地验证

1. 构建：
   - Build_CMakeTools: `all`
2. 定向执行：
   - RunCtest_CMakeTools: `ToolTraceBridgeTest`
   - RunCtest_CMakeTools: `ToolManagerPipelineTest`
   - RunCtest_CMakeTools: `ToolObservabilityIntegrationTest`
   - RunCtest_CMakeTools: `ToolServicesSmokeIntegrationTest`
3. 聚合执行：
   - RunCtest_CMakeTools: 全量测试集
4. 结果摘要：
   - 新增定向用例全部通过。
   - 全量 CTest 聚合结果中，tools 相关新增用例全部通过，剩余失败仍是两个既有 infra diagnostics：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`。
   - `ToolObservabilityIntegrationTest` 已覆盖 audit、metrics、trace 三类 bridge 的共存与 fail-open 语义。
   - CMake Tools 仍输出历史 `DartConfiguration.tcl` 噪声，不影响通过结论。

## 6. 风险与回退

1. 021 当前不覆盖跨 tools -> services 的 parent span 传播；若后续要建立真正的跨模块父子链，需要先在 services call context 与 adapter 入口补稳定 parent span 输入口径。
2. trace bridge 当前只对 builtin execution lane 建立专属 execution span；workflow / mcp execution span 仍待对应执行链真正落地后再补，不应提前伪造空 span。
3. tools integration aggregate 目前仍受两个既有 infra diagnostics 失败影响，导致 `dasall_integration_tests` 不能整体报绿；恢复全量绿灯仍需单独处理 infra 用例，而不是回退 tools trace 改动。