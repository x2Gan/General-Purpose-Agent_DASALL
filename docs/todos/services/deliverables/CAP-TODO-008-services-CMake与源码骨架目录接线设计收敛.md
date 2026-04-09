# CAP-TODO-008 services CMake 与源码骨架目录接线设计收敛

日期：2026-04-09  
任务：CAP-TODO-008  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 7.1 已将 services/include、services/src/ServiceFacade.cpp、services/src/ServiceContextBuilder.cpp、services/src/execution、services/src/data、services/src/system 列为 Phase 1 的首批工程落点。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 8.1 明确 services 模块应退出 placeholder-only 状态，转为具备 façade、上下文装配和三类子域目录的真实源码树。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 8.2 Phase 1 的完成判定是 `dasall_services` 可编译、公共头文件可 include，并为后续 unit smoke 留出骨架入口。
4. 当前 services/CMakeLists.txt 仍只编译 src/placeholder.cpp，无法体现 ServiceFacade、ServiceContextBuilder 与 execution/data/system 子域的目录落点。
5. docs/todos/services/DASALL_capability_services子系统专项TODO.md 将 CAP-TODO-008 的验收限定为 CMake 接线与源码骨架目录，不要求在本轮提前实现上下文规范化、façade 委派或 unit 注册。

## 2. 外部参考

1. CMake 官方文档 `add_library` 与 `target_sources` 说明：库目标可先创建，再通过 `target_sources` 追加真实源码；`FILE_SET HEADERS` 与 `PRIVATE` 源文件可以分别承担稳定 public include 面和内部实现接线。这支持本轮保持 public header file set 不变，同时把 ServiceFacade / ServiceContextBuilder / execution-data-system 子域源码接入 `dasall_services`。

## 3. Design 结论

1. 保持 services/include 下三份 public ABI 头文件的 `FILE_SET public_headers` 不变，不在 CAP-TODO-008 扩张新的公共接口面。
2. 把 `dasall_services` 的 `PRIVATE` 源文件从单一 src/placeholder.cpp 升级为 ServiceFacade.cpp、ServiceContextBuilder.cpp 和 execution/data/system 三个子域的骨架翻译单元。
3. execution、data、system 目录在本轮只落最小 placeholder 源文件，用于固定目录与构建落点；具体 lane 逻辑仍留给后续原子任务。
4. services/src 作为模块内部实现根目录，应通过 `PRIVATE` include path 预留后续 internal headers 的接线能力，但不对外暴露给其他模块。
5. 本轮只完成骨架接线，不提前实现 ServiceContextBuilder 的 normalize_context 语义，也不在 ServiceFacade 中落 IDataService / IExecutionService 委派逻辑。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 退出 placeholder-only 状态 | services/CMakeLists.txt 中的 `PRIVATE` 源文件列表 |
| 引入 façade 骨架翻译单元 | services/src/ServiceFacade.cpp |
| 引入上下文装配骨架翻译单元 | services/src/ServiceContextBuilder.cpp |
| 固定 execution 子域目录 | services/src/execution/placeholder.cpp |
| 固定 data 子域目录 | services/src/data/placeholder.cpp |
| 固定 system 子域目录 | services/src/system/placeholder.cpp |
| 预留模块内部头文件接线 | services/CMakeLists.txt 的 `PRIVATE` include 目录 |

## 5. Build 三件套

1. 代码目标：更新 services/CMakeLists.txt，把 `dasall_services` 接到真实源码树；新增 ServiceFacade / ServiceContextBuilder / execution-data-system 骨架源文件，并移除旧的顶层 placeholder-only 源。
2. 测试目标：验证 `dasall_services` 单独构建通过，且 `ctest -N` 在当前仓库状态下仍能正常列出测试发现结果，说明本轮 CMake 接线没有破坏全局 discoverability。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_services
   - ctest --test-dir build-ci -N

## 6. 风险与回退

1. CAP-TODO-009 与 CAP-TODO-010 仍分别拥有 ServiceContextBuilder 和 ServiceFacade 的行为性骨架；本轮若提前写入 normalize_context 或 façade 委派，会破坏原子任务边界。
2. CAP-TODO-011 才负责 services unit 测试拓扑注册；本轮只保证目录与构建入口可承接后续测试，不提前修改 tests/unit。