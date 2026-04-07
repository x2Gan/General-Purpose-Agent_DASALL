# PLG-TODO-008 plugin 构建入口接线收敛

日期：2026-04-07
任务：PLG-TODO-008
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-008 定义为“接线 infra/src/plugin 与 infra/include/plugin CMake 目标”，完成判定是 plugin 源文件被显式列入 CMake，且 dasall_infra 可编译通过。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 8.1 要求 plugin 以 infra/src/plugin 私有实现与 infra/include/plugin 稳定接口的方式落入 infrastructure 构建图，不向 runtime/tools 反向泄露实现细节。
3. 当前仓库已完成 PLG-TODO-001/002/003/004/007，plugin 公共头文件与 PluginManager.cpp 已存在，但仍散落在 infra/CMakeLists.txt 的全局源/头清单中，缺少组件级统一入口。

## 2. 研究学习结果

### 2.1 本地证据

1. infra/CMakeLists.txt 当前直接在 DASALL_INFRA_CORE_SOURCES 中列出 src/plugin/PluginManager.cpp，并在 DASALL_INFRA_PUBLIC_HEADERS 中逐项列出五个 plugin 头文件，说明 plugin 已接入构建图，但尚未形成独立的组件级列表。
2. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 的 Phase 7.1 与关键路径都把 008/009/010 置于 Phase 3，前置要求是 Phase 1-2 完成；但 008/009 的任务行前置依赖写得过宽，混入了尚未开始的 005/006，属于同轮可修的元数据阻塞。
3. dasall_infra 通过 target_sources(... PRIVATE ...) 和 PUBLIC_HEADER 聚合库输入，因此 008 的最小改动应是把 plugin 源与 public headers 从全局散点改为 plugin 专属列表，而不是新增新的库或安装逻辑。

### 2.2 外部参考

1. CMake 官方 target_sources 文档说明，同一 target 的 sources 可以按作用域和调用顺序逐步追加；这支持把 plugin 源文件和公开头文件收敛为独立变量后再统一挂接到 dasall_infra。
2. CMake 官方 add_subdirectory 文档说明，子目录会立即参与当前构建图处理；这为后续 009 把 unit 注册逻辑下沉到 tests/unit/infra/plugin 子目录提供了稳定入口前提。

### 2.3 可落地启发

1. 008 不需要再新增 plugin 二级 CMakeLists，当前最小交付是先在 infra/CMakeLists.txt 内形成 plugin 专属 source/header 列表。
2. plugin public headers 与 PluginManager.cpp 一旦收敛到独立变量，后续 009/010 的测试入口整理就不必再重复处理 build 层的散点接线。
3. 由于原 TODO 任务行的前置依赖与 Phase 3 说明冲突，本轮应同步修正 008/009/010 的依赖元数据，避免 project-implementation-cycle 在下一轮误判不可执行。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 plugin 构建入口的最小收口范围 | plugin 详细设计 8.1；infra/CMakeLists.txt 现状 | infra/CMakeLists.txt | plugin source/public header 各有独立列表 |
| D2 | 修复 Phase 3 任务元数据与依赖不一致 | plugin 专项 TODO Phase 7.1/关键路径/任务表 | plugin 专项 TODO | 008/009/010 的依赖与状态可被后续轮次正确消费 |
| D3 | 锁定 008 的 Build 三件套 | plugin 专项 TODO 008 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. 008 的代码事实已部分存在，但因为 plugin 条目仍散落在全局清单里，无法满足“组件级构建入口接线”的完成定义，属于同轮可修的 context blocker。
2. 008/009 的任务行前置依赖混入 005/006，与 Phase 3 的执行顺序不一致；若不先修复，后续 project-implementation-cycle 将把 009 误判为不可执行。

最小 blocker-fix：

1. 在 infra/CMakeLists.txt 中新增 DASALL_INFRA_PLUGIN_SOURCES 与 DASALL_INFRA_PLUGIN_PUBLIC_HEADERS，并由全局源/头清单引用这两个 plugin 专属列表。
2. 在 plugin 专项 TODO 中把 008/009/010 的依赖元数据收敛到 Phase 1-2 完成后的实际顺序，并只把 008 标记为 Done。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| plugin 构建入口应以组件级列表治理 | infra/CMakeLists.txt 新增 DASALL_INFRA_PLUGIN_SOURCES / DASALL_INFRA_PLUGIN_PUBLIC_HEADERS |
| 保持 dasall_infra 现有导出风格 | 继续复用 target_sources 与 PUBLIC_HEADER，不新增库目标 |
| 本轮只做 build 接线与证据回写 | 不触碰 unit/contract 注册逻辑，留给 009/010 |

### 4.2 Build 三件套

1. 代码目标：更新 infra/CMakeLists.txt，收敛 plugin 源文件与公开头文件入口。
2. 测试目标：确认 dasall_infra 可完整编译，且 plugin 构建入口变量在 CMake 文件中可检索。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra
   - rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt

### 4.3 D Gate

结论：PASS。

理由：

1. 008 的收口范围严格停留在 infra/plugin 的构建入口与 TODO 元数据纠偏，没有扩张到测试注册、pipeline 或 lifecycle 实现。
2. Build 三件套已锁定，且完成条件可以通过 CMake 文件静态证据加库目标构建结果二值判定。

## 5. Build 落地结果

1. 在 infra/CMakeLists.txt 新增 DASALL_INFRA_PLUGIN_SOURCES，集中列出 src/plugin/PluginManager.cpp。
2. 在 infra/CMakeLists.txt 新增 DASALL_INFRA_PLUGIN_PUBLIC_HEADERS，集中列出 PluginDescriptor.h、PluginCatalog.h、PluginErrorCode.h、IPluginManager.h、IPluginPolicyGate.h。
3. 用 ${DASALL_INFRA_PLUGIN_SOURCES} 替换散落在 DASALL_INFRA_CORE_SOURCES 内的 plugin 源条目，并用 ${DASALL_INFRA_PLUGIN_PUBLIC_HEADERS} 替换散落在 DASALL_INFRA_PUBLIC_HEADERS 内的 plugin 头文件条目。
4. 在 plugin 专项 TODO 中修复 008/009/010 的依赖元数据和映射表错位，并将 PLG-TODO-008 回写为 Done。

## 6. Build 合规复核

1. 边界：本轮只收敛 infra/plugin 的库构建入口，不引入新的 public contract，也不调整 plugin 运行时行为。
2. 根因处理：修复的是“plugin 已接入但入口散落、状态元数据不一致”的根因，而不是只在 TODO 上补一个 Done 标记。
3. 测试出口：008 以 dasall_infra 构建成功和 plugin 入口变量可检索作为二值出口；unit/contract discoverability 将分别在 009/010 单独处理。
4. 兼容性：保留既有 target_sources 与 PUBLIC_HEADER 风格，不改变 install/export 方式。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra：通过。
3. rg -n "DASALL_INFRA_PLUGIN_(SOURCES|PUBLIC_HEADERS)" infra/CMakeLists.txt：通过，可定位 plugin 专属构建入口变量。

## 8. 结论

1. PLG-TODO-008 已完成，plugin 公共头与实现源现在通过 plugin 专属列表统一接入 dasall_infra。
2. 008 完成后，后续 009/010 可以专注测试入口收口与 discoverability，而不再重复处理 build 层散点接线问题。