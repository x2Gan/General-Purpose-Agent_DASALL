# CAP-TODO-011 services unit 测试拓扑注册设计收敛

日期：2026-04-09  
任务：CAP-TODO-011  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_capability_services子系统详细设计.md 7.1 已把 `tests/unit/services/**` 明确为 services 模块单元测试落点。
2. docs/architecture/DASALL_capability_services子系统详细设计.md 8.1 的目录建议明确包含 `tests/unit/services/**`，说明 services unit 不应长期散落在 tests/unit 顶层。
3. docs/architecture/DASALL_capability_services子系统详细设计.md 9.1 的测试矩阵要求至少覆盖 public header layout、ServiceContextBuilder 和 ServiceFacade 行为，因此 tests/unit/services 需要成为可扩展的稳定注册槽位。
4. CAP-TODO-008~010 已分别落下骨架源码、ServiceContextBuilderTest 与 ServiceFacadeTest；CAP-TODO-011 需要把这些最小验证入口收口为统一 topology，而不是继续在 tests/unit/CMakeLists.txt 顶层追加散点 target。
5. CAP-TODO-011 的完成判定要求 services 用例可被 `ctest -N` 与 `-L unit` 发现，因此本轮除了整理 CMake 目录结构，还需要补一条最小 ServiceHeaderLayoutTest 作为 public include 面 smoke。

## 2. 外部参考

1. CMake 官方文档 `add_test` 与 `set_tests_properties` 说明：测试 discoverability 依赖在同一目录作用域内用 `add_test(NAME ...)` 注册测试，再用 `set_tests_properties(... LABELS ...)` 赋予标签，`ctest -N` 与 `ctest -L unit` 才能稳定发现和筛选这些测试。本轮据此把 services 单测统一收口到 tests/unit/services/CMakeLists.txt 中注册，并保持 `unit` 标签一致。

## 3. Design 结论

1. services unit 拓扑应集中在 tests/unit/services/CMakeLists.txt 中管理，不再在 tests/unit/CMakeLists.txt 顶层散点注册 services test target。
2. 顶层 tests/unit/CMakeLists.txt 只负责 `add_subdirectory(services)` 和把 services test target 纳入 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 聚合列表。
3. ServiceHeaderLayoutTest 作为 public include 面 smoke，验证 `IExecutionService.h`、`IDataService.h`、`ServiceTypes.h` 能通过稳定 include 根引用，并保持签名与基础类型不漂移。
4. CAP-TODO-009 的负例和 CAP-TODO-010 的负例继续复用，不在 011 重复制造新的负路径测试；本轮重点是 discoverability 与 topology 收口。
5. 本轮不改动 services 生产代码逻辑，只整理单测拓扑与注册入口。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 收口 services unit 子目录 | tests/unit/services/CMakeLists.txt |
| 顶层 unit 聚合只保留子目录入口 | tests/unit/CMakeLists.txt |
| 增加 public header layout smoke | tests/unit/services/ServiceHeaderLayoutTest.cpp |
| 保留 ServiceContextBuilder unit 入口 | tests/unit/services/CMakeLists.txt + ServiceContextBuilderTest.cpp |
| 保留 ServiceFacade unit 入口 | tests/unit/services/CMakeLists.txt + ServiceFacadeTest.cpp |

## 5. Build 三件套

1. 代码目标：新增 tests/unit/services/CMakeLists.txt 和 ServiceHeaderLayoutTest.cpp，把 services 相关 unit target 从 tests/unit 顶层迁入 services 子目录。
2. 测试目标：验证 ServiceHeaderLayoutTest、ServiceContextBuilderTest、ServiceFacadeTest 都可被 `ctest -N` 发现，并继续通过 `ctest -L unit`。
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_unit_tests
   - ctest --test-dir build-ci -N
   - ctest --test-dir build-ci --output-on-failure -L unit

## 6. 风险与回退

1. tests/unit/services/CMakeLists.txt 目前只覆盖 header layout、context builder、facade 三类入口；后续 services 子域测试应继续按 execution/data/system/adapters/ops 分层扩展，而不是重新回到顶层散点注册。
2. 本轮只是拓扑收口，不应借机修改 ServiceContextBuilder 或 ServiceFacade 行为；若后续 unit 失败，应回到对应行为任务修复，而不是在 011 中偷偷混入逻辑变更。