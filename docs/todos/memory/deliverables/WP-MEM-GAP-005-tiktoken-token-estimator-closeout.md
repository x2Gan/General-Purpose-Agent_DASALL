# WP-MEM-GAP-005 tiktoken token estimator closeout

来源任务：WP-MEM-GAP-005
关联缺口：GAP-P1-A / MEM-E08
完成日期：2026-06-03

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-005 / GAP-P1-A`，不把 installed gate、MaintenanceTicker、external_evidence 投影或后续 scoring / retention 演进项混入同一轮。
2. authoritative 问题定义固定为：Memory 是否已经把启发式 token 估算替换为 `cl100k_base` 兼容的真实 tokenizer 计数，并统一作用到 `BudgetAllocator`、`CandidateCollector`、`ContextOrchestrator` 与 `CompressionCoordinator`。
3. 若第三方 tokenizer 需要新增资源查找或 ABI 兼容处理，则只允许在 Memory owner 范围内完成构建、资源路径和 fallback 收口，不把 qemu / installed 证据或 llm/runtime owner 逻辑混进本轮。

## 2. 本轮代码结果

| 目标 | 落盘结果 | 对 closeout 的意义 |
|---|---|---|
| vendored tokenizer 构建与资产装配 | 更新 [memory/CMakeLists.txt](../../../memory/CMakeLists.txt)，引入 pin 到 `9323db528d52e48900c75ce197c3251085b18480` 的 `cpp-tiktoken` source，复用系统 `pcre2-8`，并把 `cl100k_base.tiktoken` 复制到 build-tree / install-tree `share/dasall/memory/tokenizers` | Memory 现可在 build-tree 与安装态用同一份 `cl100k_base` 资产加载真实 tokenizer，不依赖上游默认的可执行文件同级 `tokenizers/` 目录 |
| estimator 抽象 | 更新 [memory/src/util/TokenEstimator.h](../../../memory/src/util/TokenEstimator.h) 并新增 [memory/src/util/TokenEstimator.cpp](../../../memory/src/util/TokenEstimator.cpp)，引入 `ITokenEstimator`、`HeuristicTokenEstimator`、vendored `TiktokenTokenEstimator` 与 shared factory；缺资产或 encode 异常时 fail-soft 回落 heuristic | token 估算不再停留在单个 inline heuristic；同一套 estimator 现可被 factory 统一创建并复用 |
| 统一 token 读数路径 | 更新 [memory/include/config/MemoryConfig.h](../../../memory/include/config/MemoryConfig.h)、[memory/src/config/MemoryConfigProjector.cpp](../../../memory/src/config/MemoryConfigProjector.cpp)、[memory/src/context/BudgetAllocator.cpp](../../../memory/src/context/BudgetAllocator.cpp)、[memory/src/context/CandidateCollector.cpp](../../../memory/src/context/CandidateCollector.cpp)、[memory/src/context/ContextOrchestrator.cpp](../../../memory/src/context/ContextOrchestrator.cpp)、[memory/src/writeback/CompressionCoordinator.cpp](../../../memory/src/writeback/CompressionCoordinator.cpp) 与 [memory/src/MemoryManagerFactory.cpp](../../../memory/src/MemoryManagerFactory.cpp) | budget 规划、candidate 汇总、trim、token budget report 与 summary projection 现使用同一 shared estimator，不再出现不同代码路径各自粗估的分叉 |
| focused 验证门 | 新增 [tests/unit/memory/TiktokenEstimatorAccuracyTest.cpp](../../../tests/unit/memory/TiktokenEstimatorAccuracyTest.cpp)，扩展 [tests/unit/memory/BudgetAllocatorTest.cpp](../../../tests/unit/memory/BudgetAllocatorTest.cpp) 双 backend 跑通，并更新 [tests/unit/memory/CMakeLists.txt](../../../tests/unit/memory/CMakeLists.txt)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)、[tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp) | `cl100k_base` 参考 token 数、BudgetAllocator 双 backend 兼容、MemoryConfig 默认值与 profile projection 现都有自动化证据 |

## 3. 外部与第三方依据

1. `cpp-tiktoken` 上游当前 commit `9323db528d52e48900c75ce197c3251085b18480` 提供 `cl100k_base.tiktoken` 与 `IResourceReader` 扩展点；本轮不复用其默认 executable-sibling 资源查找，而由 Memory 自己控制 build/install 资产路径。
2. 本机 Linux 构建环境已提供系统 `pcre2-8` 头与链接库，因此本轮只 vendor tokenizer 源码，不额外把整套 PCRE2 CMake/install 规则引入 DASALL package 输出。
3. `TiktokenEstimatorAccuracyTest` 使用公开 `cl100k_base` 参考样例：`antidisestablishmentarianism -> 6`、`2 + 2 = 4 -> 7`、`お誕生日おめでとう -> 9`，作为非循环自证的 focused accuracy baseline。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| token 估算必须切换到 `cl100k_base` 兼容真实计数 | `TiktokenEstimatorAccuracyTest` |
| `BudgetAllocator` 不能只依赖 heuristic inline helper | `BudgetAllocatorTest` 双 backend |
| `CandidateCollector` / `ContextOrchestrator` / `CompressionCoordinator` 需要共享同一 estimator 口径 | `CandidateCollectorTest`、`CandidateCollectorVectorOffTest`、`ContextOrchestratorBudgetTest`、`ContextOrchestratorTest`、`CompressionCoordinatorTest`、`CompressionCoordinatorSummarizerTest` |
| profile / public config 必须显式投影 `token_estimator=tiktoken` | `MemoryInterfaceCompileTest`、`MemoryProfileCompatibilityTest` |

## 5. D Gate

1. owner 边界不变：具体 tokenizer 只落在 Memory module；llm/runtime_support 不反向依赖 memory internals。
2. fallback 语义明确：当 `cl100k_base` 资产缺失、加载失败或 encode 抛错时，只允许回落 heuristic，不允许让 `prepare_context()` / `write_back()` 整体 fail-open 或跨模块取临时规则。
3. 资源路径单一：build-tree 与 install-tree 均统一收口到 `share/dasall/memory/tokenizers`；不保留上游 `tokenizers/` 可执行同级目录作为 authoritative 运行时契约。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_memory"])`
   - 结果：通过；vendored tokenizer 与 Memory library 在同一 build 中完成编译与链接闭环。
2. `Build_CMakeTools(buildTargets=["dasall_memory_tiktoken_estimator_accuracy_unit_test","dasall_memory_budget_allocator_unit_test","dasall_memory_interface_compile_unit_test","dasall_memory_profile_compatibility_integration_test"])`
   - 结果：通过。
3. `RunCtest_CMakeTools(tests=["TiktokenEstimatorAccuracyTest","BudgetAllocatorTest","MemoryInterfaceCompileTest","MemoryProfileCompatibilityTest"])`
   - 结果：通过，`100% tests passed, 0 tests failed out of 4`。
4. `Build_CMakeTools(buildTargets=["dasall_memory_candidate_collector_unit_test","dasall_memory_candidate_collector_vector_off_unit_test","dasall_memory_context_budget_unit_test","dasall_memory_context_orchestrator_unit_test","dasall_memory_compression_coordinator_unit_test","dasall_memory_compression_summarizer_unit_test"])`
   - 结果：通过。
5. `RunCtest_CMakeTools(tests=["CandidateCollectorTest","CandidateCollectorVectorOffTest","ContextOrchestratorBudgetTest","ContextOrchestratorTest","CompressionCoordinatorTest","CompressionCoordinatorSummarizerTest"])`
   - 结果：通过，`100% tests passed, 0 tests failed out of 6`。

## 7. 完成判定

1. `WP-MEM-GAP-005 / GAP-P1-A / MEM-E08` 已闭合。
2. Memory 现已把 token 估算从启发式 inline helper 收口为 shared `ITokenEstimator`，并以 vendored `cl100k_base` 兼容 tokenizer 驱动 budget / trim / report / summary 相关路径。
3. 当前剩余高优先级增量项已转为 installed gate 的 broader release 绿色记录，以及 `WP-MEM-GAP-006 / -007 / -008` 等后续 P1 任务；token 估算不再是 P1 blocker。