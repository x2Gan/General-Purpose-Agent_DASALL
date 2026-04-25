# COG-TODO-006 cognition unit 测试入口接线

状态：Done
日期：2026-04-25
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：测试拓扑 / unit discoverability

## 1. 本地证据

1. `docs/todos/cognition/DASALL_cognition子系统专项TODO.md` 中 COG-TODO-006 的代码目标限定为更新 `tests/unit/cognition/CMakeLists.txt`、`tests/unit/CMakeLists.txt`，并新增 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`。
2. COG-TODO-005 已完成 `dasall_cognition` 真实源文件与 public header file set 接线，且明确 `CognitionInterfaceSurfaceTest` discoverability 由本任务完成。
3. `docs/architecture/DASALL_cognition子系统详细设计.md` §7.1 COG-D01 要求接口面替换 placeholder 后通过 `CognitionInterfaceSurfaceTest` 验收，§8.1 建议 `tests/unit/cognition/` 作为单元测试落点，§9.1 要求单元测试覆盖合法输入与边界输入。
4. `docs/development/DASALL_工程协作与编码规范.md` §3.7 要求新增公共接口同步增加 unit 或 contract 测试，并沿用 `unit` 标签。
5. 当前 `tests/unit/cognition/CMakeLists.txt` 只有 placeholder 注释，顶层 `tests/unit/CMakeLists.txt` 已 `add_subdirectory(cognition)`，但 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 尚未纳入 cognition unit target。

## 2. 外部参考

1. CMake `add_test()` 官方文档说明 `add_test(NAME ... COMMAND ...)` 会添加可由 `ctest` 运行的测试；当 command 是 `add_executable()` 创建的 target 时，CMake 会在构建时替换为可执行文件位置：https://cmake.org/cmake/help/latest/command/add_test.html
2. CTest 官方文档说明 `ctest -N` / `--show-only` 会列出将运行的测试而不执行，适合作为 discoverability 验收入口；`-R` 可按测试名正则筛选：https://cmake.org/cmake/help/latest/manual/ctest.1.html
3. CMake `add_custom_target()` 官方文档说明 custom target 可通过 `DEPENDS` 建立目标依赖；本仓库的 `dasall_unit_tests` 聚合目标需要显式依赖 unit executable target，避免 CTest 发现了测试但可执行文件未构建：https://cmake.org/cmake/help/latest/command/add_custom_target.html

## 3. 主结论

1. 本轮只建立 cognition unit 测试入口与 interface surface smoke，不提前完成 COG-TODO-007 ~ 010 的字段冻结、PlanGraph/ReplanResult 或五段组件接口签名。
2. `CognitionInterfaceSurfaceTest` 应覆盖当前已落盘公共头的可 include、三入口接口签名、factory 可链接，以及旧 `step()` 入口不得回归。
3. cognition unit target 必须进入顶层 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS`，否则 `cmake --build ... --target dasall_unit_tests` 可能不会先构建该可执行文件。
4. CTest 名称使用 `CognitionInterfaceSurfaceTest`，target 名称使用 `dasall_cognition_interface_surface_unit_test`，避免与平台或 llm 的 generic `InterfaceSurfaceTest` 命名冲突。

## 4. 边界与职责

| 对象 | 本轮职责 | 非职责 |
|---|---|---|
| `tests/unit/cognition/CMakeLists.txt` | 注册 cognition unit executable、链接 `dasall_cognition` 与 `dasall_test_support`、添加 `unit;cognition` 标签 | 不注册 integration 测试；不接入 runtime smoke |
| `tests/unit/CMakeLists.txt` | 将 cognition unit target 纳入 `dasall_unit_tests` 聚合依赖 | 不整理其他模块历史 target |
| `CognitionInterfaceSurfaceTest.cpp` | 编译并检查当前公共接口面、factory 链接、legacy `step()` 负例 | 不冻结后续未落盘对象字段；不验证阶段行为 |
| cognition 生产代码 | 本轮不修改 | 不提前实现 planner/reasoner/reflection/response 组件 |

## 5. 数据 / 接口说明

| 接口 / 数据 | 检查方式 | 目的 |
|---|---|---|
| `ICognitionEngine::decide()` / `reflect()` | `static_assert` 方法指针签名 | 保护 COG-TODO-001 三入口口径 |
| `IResponseBuilder::build()` | `static_assert` 方法指针签名 | 保护 response builder 独立入口 |
| `create_cognition_engine()` / `create_response_builder()` | factory 签名与非空实例断言 | 验证 `dasall_cognition` 链接入口可用 |
| `IPlanner` / `IReasoner` / `IReflectionEngine` | virtual destructor 与非公开默认构造断言 | 验证承载面存在且不会被误当作已完成 concrete API |
| legacy `step()` | SFINAE 负例断言 | 防止旧架构草图入口回流 |

## 6. 流程 / 时序

1. CMake 配置阶段进入 `tests/unit/cognition/CMakeLists.txt`。
2. 子目录创建 `dasall_cognition_interface_surface_unit_test`，并注册 CTest 名称 `CognitionInterfaceSurfaceTest`。
3. 子目录把 target 列表回传给 `tests/unit/CMakeLists.txt`。
4. 顶层 `dasall_unit_tests` custom target 依赖该 target，再执行 `ctest -L unit`。
5. `ctest -N` 可在不运行测试的情况下发现 `CognitionInterfaceSurfaceTest`。

## 7. D 原子项完成情况

| 原子项 | 目标 | 结果 |
|---|---|---|
| D1 | 校验 COG-TODO-005 前置已完成且无未解 blocker | PASS |
| D2 | 明确本轮只做 unit topology 与 surface smoke | PASS |
| D3 | 锁定 CTest 名称、target 名称、标签与顶层聚合关系 | PASS |
| D4 | 锁定 Build 三件套与发现性验收 | PASS |

## 8. Design -> Build 映射

| 设计结论 | Build 落点 | 验收点 |
|---|---|---|
| cognition unit 目录不能继续为空 | `tests/unit/cognition/CMakeLists.txt` | CMake 注册至少 1 个 cognition unit target |
| interface surface 需要可编译锚点 | `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp` | `CognitionInterfaceSurfaceTest` 可构建并通过 |
| 顶层 unit gate 必须构建该 target | `tests/unit/CMakeLists.txt` | `dasall_unit_tests` 依赖 cognition unit target |
| discoverability 是本轮核心验收 | CTest `add_test(NAME CognitionInterfaceSurfaceTest ...)` | `ctest -N` 可发现测试名 |

## 9. Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 替换 `tests/unit/cognition/CMakeLists.txt` placeholder 为真实注册逻辑 | target 可被构建，测试带 `unit;cognition` 标签 | `cmake --build build-ci-cog006 --target dasall_unit_tests` | 若聚合变量未回传，则补 `PARENT_SCOPE` |
| B2 | 更新 `tests/unit/CMakeLists.txt` 聚合列表 | `dasall_unit_tests` 先构建 cognition executable | `cmake --build build-ci-cog006 --target dasall_unit_tests` | 若顶层已有固定清单，则只加入本 target |
| B3 | 新增 `CognitionInterfaceSurfaceTest.cpp` | 正例：三入口 / factory；负例：legacy `step()` 不存在 | `ctest --test-dir build-ci-cog006 -R "CognitionInterfaceSurfaceTest" --output-on-failure` | 若后续签名变化，应在对应 TODO 中同步改 test |
| B4 | 验证发现性 | `ctest -N` 可发现测试名 | `ctest --test-dir build-ci-cog006 -N | rg "CognitionInterfaceSurfaceTest"` | 若 build-ci 已被其他 generator 占用，使用干净目录并记录环境说明 |

## 10. D Gate

| 检查项 | 结论 |
|---|---|
| D 文档已落盘 | PASS |
| Design->Build 映射完整 | PASS |
| Build 三件套已锁定 | PASS |
| 范围未越界 | PASS：不修改 production cognition、不接入 integration/runtime |
| 是否允许进入 Build | PASS |

## 11. Build 结果

| 原子项 | 结果 |
|---|---|
| B1 | PASS：`tests/unit/cognition/CMakeLists.txt` 已注册 `dasall_cognition_interface_surface_unit_test`，链接 `dasall_cognition` / `dasall_test_support`，并设置 `unit;cognition` 标签 |
| B2 | PASS：`tests/unit/CMakeLists.txt` 已将 `${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}` 注入顶层 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` |
| B3 | PASS：`CognitionInterfaceSurfaceTest.cpp` 已覆盖三入口接口签名、factory 链接、承载头可 include、旧 `step()` 不存在负例 |
| B4 | PASS：`ctest -N` 在 `build-ci-cog006` 与既有 `build-ci` 均可发现 `CognitionInterfaceSurfaceTest` |

## 12. 验收

代码目标：

1. 更新 `tests/unit/cognition/CMakeLists.txt`，替换 placeholder 为真实 unit target 注册。
2. 更新 `tests/unit/CMakeLists.txt`，把 cognition unit target 纳入 `dasall_unit_tests` 聚合依赖。
3. 新增 `tests/unit/cognition/CognitionInterfaceSurfaceTest.cpp`。

测试目标：

1. `CognitionInterfaceSurfaceTest` 可构建、可运行。
2. `ctest -N` 可发现 cognition unit 入口。
3. 顶层 `dasall_unit_tests` 聚合包含 1 个 `cognition` 标签测试。

验收命令与结果：

```bash
cmake -S . -B build-ci-cog006 -G "Unix Makefiles"
cmake --build build-ci-cog006 --target dasall_unit_tests
ctest --test-dir build-ci-cog006 -N | rg "CognitionInterfaceSurfaceTest"
ctest --test-dir build-ci-cog006 -R "CognitionInterfaceSurfaceTest" --output-on-failure
cmake -S . -B build-ci
cmake --build build-ci --target dasall_unit_tests
ctest --test-dir build-ci -N | rg "CognitionInterfaceSurfaceTest"
```

结果摘要：

1. `build-ci-cog006` 配置与 `dasall_unit_tests` 通过，464 个 unit 测试全绿，label summary 包含 `cognition = 1 test`。
2. `ctest --test-dir build-ci-cog006 -N | rg "CognitionInterfaceSurfaceTest"` 输出 `Test  #21: CognitionInterfaceSurfaceTest`。
3. `ctest --test-dir build-ci-cog006 -R "CognitionInterfaceSurfaceTest" --output-on-failure` 通过，1/1 passed。
4. 既有 `build-ci`（Ninja）增量配置与 `dasall_unit_tests` 通过，464 个 unit 测试全绿，并可通过 `ctest -N` 发现 `CognitionInterfaceSurfaceTest`。

环境说明：仓库现有 `build-ci` 使用 Ninja 生成器，因此本轮用干净的 `build-ci-cog006` 覆盖 TODO 中的 Unix Makefiles 路径，同时保留既有 `build-ci` 的增量 Ninja 验证。

## 13. Build 合规复核

| 检查项 | 结论 |
|---|---|
| 代码注释 | PASS：新增 CMake 和 surface test 结构自解释，无需额外叙述式注释 |
| 正负例覆盖 | PASS：正例覆盖三入口签名、factory 非空、target 构建；负例覆盖 legacy `step()` 不存在、generic `InterfaceSurfaceTest` 命名不被复用 |
| 测试发现性 / 门禁入口 | PASS：`ctest -N` 可发现 `CognitionInterfaceSurfaceTest`，`dasall_unit_tests` 聚合运行 464 个 unit 测试 |
| TODO / 交付物 / worklog 证据 | PASS：本交付物、专项 TODO 与 worklog 均回写 |
| 提交前状态隔离 | PASS：本轮文件范围限定为 cognition unit 接线、测试和对应文档证据 |
| Blocker 状态 | PASS：COG-TODO-006 无未解 blocker；COG-TODO-005 前置已完成 |
