# TOOL-TODO-008 tools 源码骨架与 unit 测试入口接线收敛

日期：2026-04-15  
任务：TOOL-TODO-008  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-008 的验收条件明确要求：`tools/src` 不再是 placeholder-only，`tests/unit/tools/CMakeLists.txt` 不再是空注释，且 `ctest -N` 能发现 tools unit 入口。
2. docs/architecture/DASALL_tools子系统详细设计.md 8.1、8.2 Phase 0 把本轮目标收敛为“建立 tools/include 公共接口根与最小 header layout + ToolManager 占位实现 + unit discoverability”。
3. 同一设计文档 1412~1422 的 delivery map 明确 TOOL-D1 / TOOL-D11 需要把 `tests/unit/tools` 接入 CMake/CTest，并让公共接口 surface test 成为可发现的 unit test，而不是停留在 compile-only 孤岛。
4. 001~007 已经冻结 `ToolInvocationContext`、`ToolInvocationEnvelope`、`ITool / IToolManager`、`IPolicyGate / ICapabilityCache`、`IMCPAdapter / IMCPTransport`、`IToolPluginProvider / ToolPluginExtensionCatalog` 六组公共 ABI；008 的职责是把这些 surface test 统一纳入 discoverability。
5. `tools/CMakeLists.txt` 在 001 时仍只编译 `tools/src/placeholder.cpp`；因此 008 的最小收口动作是引入 `ToolManager.cpp` 和 `tools/src/*` 子目录骨架，并让 `dasall_tools` 编译真实 skeleton tree。

## 2. 外部参考

1. CMake 官方 `add_test` 文档明确：`add_test(NAME <name> COMMAND <command> ...)` 会把测试添加到项目中供 `ctest` 运行；当测试通过 `add_test` 注册后，CTest 才能发现并执行它。这支持本任务使用 `add_executable + add_test + LABELS unit;tools` 来完成 tools unit discoverability，而不是继续停留在手工 `fsyntax-only` 的孤立验证方式。

## 3. Design 结论

1. `tools/src` 首版 skeleton 必须是真实源码树，而不是单文件 placeholder；最小方案是在 `ToolManager.cpp` 与各子目录下落空命名空间占位翻译单元，让后续实现任务可以直接替换而不再先做目录重构。
2. `tests/unit/tools/CMakeLists.txt` 需要统一注册前七轮已经落地的 six surface tests，使它们都成为可执行 unit target 和可发现的 CTest case。
3. six surface tests 继续保持“轻量 ABI 锁定”风格：静态断言 + 样例初始化，不引入 gtest；为进入 CTest，只需补 `main()` 包装层并复用 `dasall_tools` 的公共 include/link 关系。
4. `tests/unit/CMakeLists.txt` 需要把新的 tools unit executable targets 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 聚合列表，否则 `dasall_unit_tests` 不会依赖它们。
5. 本轮只做 skeleton / discoverability，不前移 009+ 的真正实现逻辑；registry / policy / mcp / plugin bridge 等子目录内仍保持空骨架文件。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 建立 tools/src skeleton tree | tools/CMakeLists.txt；tools/src/ToolManager.cpp；tools/src/*/placeholder.cpp |
| 接线 tools unit discoverability | tests/unit/tools/CMakeLists.txt；tests/unit/CMakeLists.txt |
| 把 six surface tests 转为可执行单测 | tests/unit/tools/*.cpp |

## 5. Build 三件套

1. 代码目标：让 `dasall_tools` 编译真实 skeleton 源码树，并让 `tests/unit/tools` 注册 six surface unit tests。
2. 测试目标：通过 Build_CMakeTools 构建 `dasall_tools` 与 `dasall_unit_tests`，随后使用 `ctest -N` 验证 tools unit 测试已被发现。
3. 验收命令：
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`
   - `ctest --test-dir /home/gangan/DASALL/build/vscode-linux-ninja -N`
   - 关注 `ToolInvocationContextSurfaceTest`、`ToolInvocationEnvelopeSurfaceTest`、`ToolInterfaceSurfaceTest`、`ToolPolicyCapabilitySurfaceTest`、`MCPInterfaceSurfaceTest`、`ToolPluginProviderSurfaceTest` 已进入 CTest 列表

## 6. 风险与回退

1. 当前 skeleton `.cpp` 文件只提供空命名空间翻译单元；后续实现任务应直接替换这些文件或在同目录新增真实实现，不要再恢复到单文件 placeholder 布局。
2. six surface tests 目前仍是 ABI surface test，不承载行为断言；后续真正行为测试应在 009+ 的组件任务中新增，而不是把 008 的入口测试膨胀成伪集成测试。
3. 若后续某个 surface test 需要额外 include path 或 fixture，优先在 `tests/unit/tools/CMakeLists.txt` 局部补充，不要打破 `dasall_tools` 的公共 include 边界。