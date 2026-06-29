# WP-MEM-GAP-017 historical artifact cleanup closeout

来源任务：WP-MEM-GAP-017
关联缺口：GAP-P3-D
完成日期：2026-06-29

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-017 / GAP-P3-D`，不把 `WP-MEM-GAP-018` 的 release-soak 采样增强或任何 memory 业务逻辑改造混入同一轮。
2. authoritative 问题定义固定为：`memory/src/MemoryBuildSkeleton.cpp` 是否仍然只是历史 bootstrap 期的空翻译单元，以及 `memory/CMakeLists.txt` / compile-surface tests 是否还把它当作当前事实。
3. placeholder.cpp 类残留的评估范围仅限 memory 子树：需要确认 `memory/src` 与 `memory/CMakeLists.txt` 中不存在新的 placeholder-only 回退；历史文档引用与负向断言测试不在本轮代码清理范围内。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%90%BD%E5%9C%B0%E8%AF%84%E4%BC%B0%E4%B8%8E%E7%94%9F%E4%BA%A7%E7%BA%A7%E7%BC%BA%E5%8F%A3%E6%B2%BB%E7%90%86%E4%BB%BB%E5%8A%A1%E8%A7%84%E5%88%92.md) 已把 `WP-MEM-GAP-017` 固定为“删除 `MemoryBuildSkeleton.cpp` 与 CMake 中残留引用；评估 placeholder.cpp 类历史文件”。
2. [docs/todos/memory/DASALL_memory子系统专项TODO.md](../DASALL_memory%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md) 的“当前代码与测试现状证据”仍把 `MemoryBuildSkeleton.cpp` 描述为当前 build anchor，说明历史 bootstrap 事实仍泄漏到专项 TODO 当前态，而不只是历史证据区。
3. [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) 在本轮前仍显式断言 `MemoryBuildSkeleton.cpp` 必须存在，这表明 compile-surface gate 仍把已无语义的历史锚点当成当前构建基线。
4. Martin Fowler 的 Remove Dead Code 指出，已经不再提供行为或约束价值的死代码应被直接移除，而不是继续保留在代码路径里；这与本轮删除空 build anchor 的方向一致。
5. Refactoring.Guru 对 Speculative Generality 的说明强调：未被实际使用的类、方法、字段或参数会增加理解和维护成本；在确认未被测试或真实行为消费后，应直接删除。这直接支撑本轮把 `MemoryBuildSkeleton.cpp` 与相关 CMake/bootstrap 语义从当前事实中移除。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| 删除不再承载语义的历史 build anchor | [memory/CMakeLists.txt](../../../../memory/CMakeLists.txt)、删除 `memory/src/MemoryBuildSkeleton.cpp` |
| compile-surface gate 不再把已删除文件当成当前事实 | [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |
| 增加 focused grep-style regression guard，防止 skeleton/placeholder 残留回退 | [tests/unit/memory/MemoryHistoricalArtifactRemovedTest.cpp](../../../../tests/unit/memory/MemoryHistoricalArtifactRemovedTest.cpp)、[tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) |
| 将当前态与 closeout 证据同步回写到 memory 文档链 | [docs/todos/memory/DASALL_memory子系统专项TODO.md](../DASALL_memory%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)、[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../../DASALL_%E5%AD%90%E7%B3%BB%E7%BB%9F%E6%9F%A5%E6%BC%8F%E8%A1%A5%E7%BC%BA%E4%B8%93%E9%A1%B9%E8%AE%B0%E5%BD%95.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md)、[docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%90%BD%E5%9C%B0%E8%AF%84%E4%BC%B0%E4%B8%8E%E7%94%9F%E4%BA%A7%E7%BA%A7%E7%BC%BA%E5%8F%A3%E6%B2%BB%E7%90%86%E4%BB%BB%E5%8A%A1%E8%A7%84%E5%88%92.md) |

## 4. 设计决策

1. 删除 `MemoryBuildSkeleton.cpp`，而不是把空翻译单元改名或继续保留为“非空库兜底”。当前 `dasall_memory` 已由真实实现源文件承载，保留空锚点只会让当前事实与历史 bootstrap 混淆。
2. CMake 清理不只删除 source 列表中的单行引用，同时把 `DASALL_MEMORY_SKELETON_SOURCES` 重命名为 `DASALL_MEMORY_LIBRARY_SOURCES`，避免继续在当前构建图里保留 skeleton-only 语义。
3. regression guard 单独建成 `MemoryHistoricalArtifactRemovedTest`，而不是继续把这类 grep 断言塞进 `MemoryInterfaceCompileTest`。这样 compile-surface 语义与历史残留清理语义保持分离，后续回退时更容易定位失败原因。
4. placeholder.cpp 类残留本轮只做 memory 子树评估：经检查，当前 memory 子树里已无 `placeholder.cpp` 或 `keep_library_non_empty` 生产残留；保留的相关字符串仅存在于历史文档和负向测试断言中，不构成开放代码缺口。

## 5. D Gate

1. 任务边界清晰：只处理 `MemoryBuildSkeleton.cpp` build anchor、CMake 残留引用、focused regression guard 与文档当前态回写。
2. Build 三件套完整：代码目标、测试目标、验收命令已锁定为 source 删除、focused unit build/test 与 closeout 回写。
3. blocker 结论：无前置 blocker。`dasall_memory` 已有大量真实实现源，删除空 build anchor 不需要先做新的接口或 schema 任务。

## 6. 代码结果

1. 更新 [memory/CMakeLists.txt](../../../../memory/CMakeLists.txt)，移除 `src/MemoryBuildSkeleton.cpp` source 引用，并把 `DASALL_MEMORY_SKELETON_SOURCES` 收口为 `DASALL_MEMORY_LIBRARY_SOURCES`；同文件的缺失提示也改为通用 `dasall_memory source is missing`。
2. 删除历史 4 行 namespace build anchor `memory/src/MemoryBuildSkeleton.cpp`，避免它继续被误判为当前 memory 主链的必要翻译单元。
3. 更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)，将“module 不再 placeholder-only”的断言改为锚定真实实现源 `MemoryManager.cpp`，不再要求 `MemoryBuildSkeleton.cpp` 存在。
4. 新增 [tests/unit/memory/MemoryHistoricalArtifactRemovedTest.cpp](../../../../tests/unit/memory/MemoryHistoricalArtifactRemovedTest.cpp)，并更新 [tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) 注册 `dasall_memory_historical_artifact_removed_unit_test` / `MemoryHistoricalArtifactRemovedTest`，以 grep 风格扫描 `memory/src` 与 `memory/CMakeLists.txt` 中的 `MemoryBuildSkeleton.cpp`、`placeholder.cpp`、`keep_library_non_empty` 残留。
5. 文档链回写同步到当前态：专项 TODO 改写当前状态描述，不再把 `MemoryBuildSkeleton.cpp` 记为现行 build anchor；总账、worklog 与评估规划文档同步把 `WP-MEM-GAP-017 / GAP-P3-D` 标记为已闭合。

## 7. 验证

1. `Build_CMakeTools(buildTargets=["dasall_memory","dasall_memory_interface_compile_unit_test","dasall_memory_historical_artifact_removed_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryHistoricalArtifactRemovedTest","MemoryInterfaceCompileTest"])`
   - 结果：当前 VS Code CMake Tools 环境继续返回泛化 `生成失败`。
3. `ListTests_CMakeTools` + `rg -o "MemoryHistoricalArtifactRemovedTest|MemoryInterfaceCompileTest" .../content.txt`
   - 结果：两条测试均已 discoverable，说明失败点在 CMake Tools test execution 层，而非测试未发现。
4. `./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_historical_artifact_removed_unit_test && ./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_interface_compile_unit_test`
   - 结果：通过；命令零输出返回，说明两个 focused unit binaries 直接执行均成功。

## 8. 完成判定

1. `WP-MEM-GAP-017 / GAP-P3-D` 已闭合：历史 `MemoryBuildSkeleton.cpp` build anchor 已删除，`dasall_memory` 当前构建图只保留真实实现源文件。
2. `MemoryHistoricalArtifactRemovedTest` 已把 memory 子树的 skeleton/placeholder 残留回退固定为自动化守卫；`MemoryInterfaceCompileTest` 也不再依赖已删除文件。
3. memory 当前 P3 焦点已从 `WP-MEM-GAP-017 / -018` 收窄为 `WP-MEM-GAP-018`；后续仅继续推进 release-soak / 运营采样增强。