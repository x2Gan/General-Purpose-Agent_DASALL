# COG-TODO-005 cognition 公共 include 布局与 CMake 骨架

状态：Done
日期：2026-04-25
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：Build-ready 骨架 / 公共 include 承载面

## 1. 本地证据

1. `docs/architecture/DASALL_cognition子系统详细设计.md` §7.1 COG-D01 要求先建立 cognition 公共接口面与支撑类型，替换占位实现，且不推进共享契约。
2. `docs/architecture/DASALL_cognition子系统详细设计.md` §8.1 建议 `cognition/include/` 承载 `ICognitionEngine.h`、`IResponseBuilder.h`、`IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h`、`CognitionConfig.h`、`CognitionTypes.h` 等公共头。
3. COG-TODO-001 已冻结 Runtime-facing 入口为 `ICognitionEngine::decide()` / `reflect()` 与 `IResponseBuilder::build()`；COG-TODO-002 已冻结 stage taxonomy；COG-TODO-003/004 已冻结 caller fixture 与测试 seam 设计口径。
4. 当前仓库已存在部分 cognition 头文件与 `src/CognitionFacade.cpp`，但缺少 `IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h` 承载头，`src/placeholder.cpp` 仍残留在文件树，专项 TODO 尚未记录 CMake 骨架完成证据。
5. 首次执行 `dasall_unit_tests` 时暴露两类既有 validation blocker：`tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 存在重复拼接导致编译失败；`tests/unit/CMakeLists.txt` 未把 21 个已注册 unit 测试 target 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，导致 CTest Not Run。

## 2. 外部参考

1. CMake `target_sources()` 官方文档说明目标源文件应绑定到具体 target，并可按 `PRIVATE` / `PUBLIC` / `INTERFACE` 建模 source usage requirements：https://cmake.org/cmake/help/latest/command/target_sources.html
2. CMake `target_include_directories()` 官方文档说明 `PUBLIC` include 目录会进入目标自身 include path 与消费者 usage requirements；本任务据此保留 `cognition/include` 作为 `dasall_cognition` 的唯一公共 include 根：https://cmake.org/cmake/help/latest/command/target_include_directories.html

## 3. 主结论

1. COG-TODO-005 的职责是建立公共 include 根与 CMake 骨架，不冻结 007~010 的完整字段表、PlanGraph 对象族或阶段接口方法签名。
2. `IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h` 本轮只落盘为可继承的承载面，后续由 COG-TODO-010 在 supporting types 完整后补齐 `build_plan()`、`replan()`、`decide()`、`analyze()` 等方法签名。
3. `dasall_cognition` 的构建入口以 `src/CognitionFacade.cpp` 为真实源文件，并通过 CMake public header file set 登记当前公共头；`src/placeholder.cpp` 已删除，避免继续形成 placeholder-only 误判。
4. 本轮不新增 `tests/unit/cognition` 用例；COG-TODO-006 仍负责 `CognitionInterfaceSurfaceTest` 的 discoverability 与单测拓扑。

## 4. 边界与职责

| 对象 | 本轮职责 | 非职责 |
|---|---|---|
| `cognition/include/` | 提供 cognition module-public include 根与当前公共头文件集合 | 不把 supporting types 推入 `contracts/include` |
| `IPlanner.h` / `IReasoner.h` / `IReflectionEngine.h` | 提供后续接口冻结任务可编辑的稳定头文件路径和命名空间 | 不在 COG-TODO-005 中提前冻结依赖 `PlanGraph` / request helper 的完整接口 |
| `cognition/CMakeLists.txt` | 将真实源文件和 public headers 绑定到 `dasall_cognition` target | 不接线 unit / integration 测试 |
| `src/placeholder.cpp` | 从文件树移除，关闭 placeholder 残留 | 不代表 CognitionFacade 主链已生产完备 |

## 5. 数据 / 接口说明

| 头文件 | 当前状态 | 后续任务 |
|---|---|---|
| `ICognitionEngine.h` | 已承载 `decide()` / `reflect()` 工厂入口 | COG-TODO-010 复核接口面；COG-TODO-023 落地 Facade |
| `IResponseBuilder.h` | 已承载 `build()` 工厂入口 | COG-TODO-010 / 019 复核终态构造接口 |
| `IPlanner.h` | 本轮新增承载类与受保护构造 | COG-TODO-008 / 010 补 PlanGraph / ReplanResult 与方法签名 |
| `IReasoner.h` | 本轮新增承载类与受保护构造 | COG-TODO-009 / 010 / 016 补 ActionDecision 消费签名 |
| `IReflectionEngine.h` | 本轮新增承载类与受保护构造 | COG-TODO-010 / 017 补 reflection analysis 签名 |
| `CognitionConfig.h` / `CognitionTypes.h` | 已存在并登记到 public header file set | COG-TODO-007 / 009 继续字段收敛 |

## 6. 流程 / 时序

1. CMake 配置阶段进入 `add_subdirectory(cognition)`。
2. `dasall_cognition` 读取 public header file set，公开 `cognition/include` 作为消费者 include 根。
3. 构建阶段编译 `src/CognitionFacade.cpp`，不再依赖 `src/placeholder.cpp` 保持静态库非空。
4. 后续 COG-TODO-006 可在 `tests/unit/cognition` 中 include 这些公共头并验证 surface discoverability。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 明确本轮只做 include/CMake 骨架，不冻结后续对象字段 | PASS |
| D2 | 对齐 COG-D01 与 §8.1 目录建议 | PASS |
| D3 | 识别当前代码残留与缺口 | PASS：补齐 3 个缺失承载头，删除 placeholder |
| D4 | 锁定 Build 三件套 | PASS：代码目标、测试目标、验收命令已明确 |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| cognition include 根必须先于单测入口建立 | `cognition/include/` 与 `cognition/CMakeLists.txt` | `dasall_cognition` 可构建 |
| 阶段接口路径先落盘，签名后冻结 | `IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h` | 文件存在且不引入 shared contracts |
| placeholder-only 状态必须关闭 | 删除 `cognition/src/placeholder.cpp`，CMake 源列表使用 `src/CognitionFacade.cpp` | CMake 与源码树无 `placeholder.cpp` / `keep_library_non_empty` 残留 |
| 测试 discoverability 由下一任务收口 | 保持 `tests/unit/cognition` 不扩张 | COG-TODO-006 仍为下一步 |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 `IPlanner.h`、`IReasoner.h`、`IReflectionEngine.h` | 头文件可被 CMake public header file set 发现 | `cmake --build build-ci --target dasall_cognition` | 若后续签名需要调整，在 COG-TODO-010 中扩展类方法 |
| B2 | 更新 `cognition/CMakeLists.txt` public header file set | public include 根仍指向 `cognition/include` | `cmake -S . -B build-ci -G "Unix Makefiles"` | 若 CMake 版本不支持 file set，全仓库现有 tools 目标同样需统一策略 |
| B3 | 删除 `cognition/src/placeholder.cpp` | placeholder 残留被负例检索拒绝 | `test ! -e cognition/src/placeholder.cpp && ! rg -n "placeholder.cpp|keep_library_non_empty" cognition/CMakeLists.txt cognition/src cognition/include` | 若删除暴露非空库问题，回退为真实 skeleton source，而不是恢复 placeholder |
| B4 | 修复直接 validation blocker | 聚合 unit gate 可构建全部已注册 unit 测试 | `cmake --build build-ci --target dasall_cognition dasall_unit_tests` | 只删除重复测试拼接并补聚合 target 依赖，不改变业务语义 |

## 10. 验收

代码目标：补齐 cognition 公共 include 承载头、登记 CMake public header file set、移除 placeholder 残留。
测试目标：构建 `dasall_cognition` 与既有 unit 聚合目标，确认 skeleton 不破坏现有测试入口。
验收命令：

```bash
cmake -S . -B build-ci-cog005 -G "Unix Makefiles" && cmake --build build-ci-cog005 --target dasall_cognition dasall_unit_tests
cmake --build build-ci --target dasall_cognition dasall_unit_tests
test ! -e cognition/src/placeholder.cpp && ! rg -n "placeholder.cpp|keep_library_non_empty" cognition/CMakeLists.txt cognition/src cognition/include
```

验收结论：PASS。`build-ci-cog005`（Unix Makefiles）与现有 `build-ci`（Ninja）均完成 `dasall_cognition` 与 `dasall_unit_tests`；463 个 unit 测试全部通过。placeholder 负例检索为零。

环境说明：原始 `cmake -S . -B build-ci -G "Unix Makefiles"` 因 `build-ci` 已由 Ninja 配置而被 CMake 拒绝；本轮用干净的 `build-ci-cog005` 验证 Unix Makefiles 路径，同时用既有 `build-ci` 验证仓库当前构建目录。

Validation blocker recovery：

1. 删除 `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 中重复拼接的半个文件体，恢复单测编译。
2. 补齐 `tests/unit/CMakeLists.txt` 中已注册但未纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 的 21 个 unit test target，使 `dasall_unit_tests` 在运行 CTest 前先构建全部 label=unit 可执行文件。

## 11. D Gate 与合规复核

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 代码注释要求 | PASS：新增接口承载头代码自解释，未加入空泛注释 |
| 正负例覆盖 | 正例：`dasall_cognition` / `dasall_unit_tests` 构建；负例：placeholder 残留检索为零 |
| 测试发现性 / 门禁入口 | 不适用本轮；COG-TODO-006 负责 `CognitionInterfaceSurfaceTest` discoverability |
| TODO / 交付物 / worklog 可追溯 | PASS：本交付物、专项 TODO 与 worklog 均已回写 |
| COG-BLK 状态 | COG-TODO-005 无前置 blocker；COG-BLK-001~004 已由前序任务完成设计侧解阻 |
