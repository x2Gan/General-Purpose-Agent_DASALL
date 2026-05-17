# LLM-FIX-005 llm/source boundary regression guard

状态：Done
日期：2026-05-17
来源 TODO：[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../DASALL_%E5%AD%90%E7%B3%BB%E7%BB%9F%E6%9F%A5%E6%BC%8F%E8%A1%A5%E7%BC%BA%E4%B8%93%E9%A1%B9%E8%AE%B0%E5%BD%95.md)、[docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)
任务类型：llm/source boundary 自动化回归守护

## 1. 任务边界

1. 本任务只为 llm/source boundary 增加自动化回归守护，防止 `llm/` 源码或 `dasall_llm` 构建接线悄悄越界到 `memory/`、`runtime/`、`tools/`、`apps/` 私有实现。
2. 本任务不新增 llm 产品行为，不改写 ADR-006 / 007 / 008 结论，也不把 module-local supporting objects 推进到 shared contracts。
3. 本任务同时要求守住 `PromptPipeline` / `PromptComposer` 的 owner 边界：它们继续只做 llm 内部的选择、装配与治理，不承担 memory / knowledge retrieval 职责。

## 2. 收敛内容

1. 新增源码边界单测
   - 新增 [tests/unit/llm/LLMBoundaryGuardComplianceTest.cpp](../../../tests/unit/llm/LLMBoundaryGuardComplianceTest.cpp)。
   - 单测扫描 `llm/include/**`、`llm/src/**` 的 `#include` 行，拒绝 `memory/`、`runtime/`、`tools/`、`apps/` 私有实现路径以及 `ContextOrchestrator`、`RecoveryManager`、`AgentOrchestrator` 等边界 owner 名称出现在 include 边界上。
2. 新增构建/链接守护
   - 单测同时扫描 [llm/CMakeLists.txt](../../../llm/CMakeLists.txt)，拒绝 `dasall_llm` 链接 `dasall_memory`、`dasall_runtime`、`dasall_tools`、`dasall_apps` 或把 `${PROJECT_SOURCE_DIR}/memory|runtime|tools|apps` 私有源码根加进 include path。
3. 新增 Prompt retrieval 责任守护
   - 单测对 `PromptComposer.*` 与 `PromptPipeline.*` 做针对性 token 扫描，拒绝 `ContextOrchestrator`、`MemoryStore`、`KnowledgeStore`、`VectorStore`、`retrieve_memory`、`retrieve_knowledge`、`query_knowledge`、`memory/`、`knowledge/` 等 retrieval / cross-subsystem 耦合信号进入 prompt owner 文件。
4. 测试接线与文档回写
   - 更新 [tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)，新增 `dasall_llm_boundary_guard_compliance_unit_test`、`LLMBoundaryGuardComplianceTest` 与 `DASALL_REPO_ROOT` 编译定义。
   - 同步详设、专项 TODO、总账与 worklog，显式把 `LLM-FIX-005` 标记为 Done，并把 llm 当前剩余开放项收敛为 current release candidate rerun / L6 evidence，而不再是 source boundary guard。

## 3. 验证证据

1. `cmake -S . -B build-ci && cmake --build build-ci --target dasall_llm_boundary_guard_compliance_unit_test dasall_llm_interface_surface_unit_test dasall_contract_tests -j4 && ctest --test-dir build-ci --output-on-failure -R '(LLMBoundaryGuardCompliance|LLMInterfaceSurface|LLMRequestResponseContract)Test'`
   - 结果：通过；`LLMBoundaryGuardComplianceTest`、`LLMInterfaceSurfaceTest` 与 `LLMRequestResponseContractTest` 共 `3/3` 通过。首次构建前需要重配 `build-ci` 才能发现新 target，因此最终验收命令显式包含 `cmake -S . -B build-ci`。
2. `rg -n 'LLM-FIX-005 \| Todo \||LLM-FIX-005.*边界回归防线|llm/source boundary regression guard \| 缺失自动化守护|当前剩余开放项只剩 `LLM-FIX-005`|source boundary 自动化守护；后续' docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm/DASALL_llm子系统专项TODO.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/worklog/DASALL_开发执行记录.md`
   - 结果：当前态锚点已更新为 Done / 已完成；历史 sections 中允许保留“下一步进入 005”的旧时态描述，继续作为时间快照存在。

## 4. 完成判定

LLM-FIX-005 已完成。

1. llm/source boundary 已具备自动化回归守护，不再只靠评审口头约束来阻止越界 include / link。
2. `PromptPipeline` / `PromptComposer` 的职责边界已被 focused test 固化为“只做 llm 内部选择、装配与治理，不做 memory / knowledge retrieval”。
3. 本轮完成后，llm 当前剩余开放项不再包含 source boundary guard，而是继续保留 current release candidate rerun、external provider 长稳态与 L6 soak 证据。