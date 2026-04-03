# LOG-TODO-015 Logging 测试注册收敛

日期：2026-04-03  
任务：LOG-TODO-015  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-TODO-015 定义为“注册 logging 单元与契约测试入口”，完成判定是 logging 新增测试在 `ctest -N` 可见且执行通过。
2. 在 LOG-TODO-001 至 LOG-TODO-014 的推进过程中，logging 相关 unit/contract 测试已经逐步落盘，但注册分散在多个 CMake 片段中，缺少组件级归组与统一 discoverability 标签。
3. 当前 [tests/CMakeLists.txt](tests/CMakeLists.txt) 已提供 `dasall_unit_tests` / `dasall_contract_tests` 聚合入口，因此本轮重点不是新增第二套聚合 target，而是把 logging 测试显式归组并提供按组件筛选的标签语义。

## 2. 收敛结论

1. 在 [tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt) 中新增 `DASALL_LOGGING_UNIT_TEST_EXECUTABLE_TARGETS`，把 `log event / logger interface / logging errors / facade / dispatcher / queue / audit adapter / recovery / config / metrics bridge / audit logger interface` 相关 unit 目标显式归为 logging 组件测试集合。
2. 在 [tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt) 中新增 `dasall_register_logging_unit_test(...)`，统一 logging 运行时测试的可执行文件生成、`infra/src` internal header include path、`dasall_infra` 链接和 `unit;logging` 标签。
3. 在 [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 中新增 `dasall_register_logging_contract_test(...)`，统一 logging 边界 contract 测试的 `contract;smoke;logging` 标签，并将 `LogEvent`、`AuditEvent`、`ILogger`、`IAuditLinkAdapter`、`LoggingErrors`、`ILogConfigurator`、`LoggingMetricsBridge`、`SinkDispatcher` 等 contract 用例纳入同一组件发现面。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| logging 测试需要独立于全量 unit/contract 的组件发现入口 | 通过 `unit;logging` / `contract;smoke;logging` 标签，让 `ctest -N -L logging` 可以直接枚举 logging 组件测试 |
| logging 运行时测试注册应复用同一套 CMake 模板，避免块级重复 | 引入 `dasall_register_logging_unit_test(...)` 收敛 logging unit 测试注册 |
| logging contract 测试应保持中央注册，但增加组件级标签 | 在 contract 侧引入 `dasall_register_logging_contract_test(...)` 包装既有 smoke 注册函数 |
| 顶层 unit 目标列表需要能显式反映 logging 组件测试面 | 在 `tests/unit/CMakeLists.txt` 中增加 `DASALL_LOGGING_UNIT_TEST_EXECUTABLE_TARGETS` 分组并插入总目标列表 |

## 4. 验证闭环

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；聚合 unit 套件 110/110 通过，聚合 contract 套件 132/132 通过。
3. `ctest --test-dir build-ci -N -L logging`：发现 21 个 logging 标签测试。
4. `ctest --test-dir build-ci --output-on-failure -L logging`：21/21 通过。
5. 发现性拆分结果：`unit;logging` 共 12 个测试，`contract;smoke;logging` 共 9 个测试。

## 5. 后续边界

1. 本轮只收敛 logging 测试注册与 discoverability，不新增 integration 用例，也不改变任一 public interface 或 contracts 映射。
2. LOG-TODO-016 需要基于本轮和前序任务结果，把 Gate-LOG-01~06、已解阻 blocker 和 CMake Tools 工具态异常统一回写到专项 TODO 中。