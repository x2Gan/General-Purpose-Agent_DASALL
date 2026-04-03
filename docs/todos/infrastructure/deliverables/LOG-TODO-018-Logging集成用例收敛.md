# LOG-TODO-018 Logging 集成用例收敛

日期：2026-04-03  
任务：LOG-TODO-018  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 在 `LOG-GATE-06` 中要求“tests 顶层已完成 integration 注册策略，且 logging 组件用例已落盘”。
2. [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt) 与 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 已具备顶层 integration 聚合入口，但此前 `tests/integration/infra/logging/` 为空。
3. 现有 logging 骨架已经具备可集成验证的最小组件：`LoggingFacade`、`SinkDispatcher`、`AsyncQueueController`、`AuditLinkAdapter`。

## 2. 收敛结论

1. 新增 [tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt)，统一注册 `integration;logging` 标签的 logging integration 测试。
2. 新增 [tests/integration/infra/logging/LoggingPipelineIntegrationTest.cpp](tests/integration/infra/logging/LoggingPipelineIntegrationTest.cpp)，覆盖：
   - 正例：`LoggingFacade -> SinkDispatcher -> AsyncQueueController` 主链可以写入 basic route，并保留上下文 enrichment。
   - 负例：block 策略下队列回压会返回结构化失败，且不会产生额外 partial side effect。
3. 新增 [tests/integration/infra/logging/LoggingAuditLinkIntegrationTest.cpp](tests/integration/infra/logging/LoggingAuditLinkIntegrationTest.cpp)，覆盖：
   - 正例：高风险日志携带完整 audit ref 后会经 `AuditLinkAdapter` 路由到 audit sink。
   - 负例：不完整 audit ref 会被拒绝，并保持 attrs surface 无部分写入。
4. 更新 [tests/integration/infra/CMakeLists.txt](tests/integration/infra/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，把两个新目标纳入顶层 integration 聚合入口。

## 3. Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| logging 组件必须具备独立 integration 用例落点 | 新增 `tests/integration/infra/logging/` 子目录和独立 CMake 入口 |
| integration 用例应沿用组件标签做 discoverability | 两个新测试统一标记为 `integration;logging` |
| integration 至少覆盖一条正例和一条负例 | `LoggingPipelineIntegrationTest` 与 `LoggingAuditLinkIntegrationTest` 各自同时覆盖 success/failure 路径 |
| Gate-06 关闭需要组件用例真实纳入聚合入口 | 顶层 `dasall_integration_tests` 依赖列表新增两个 logging integration target |

## 4. 验证闭环

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_logging_pipeline_integration_test dasall_logging_audit_link_integration_test dasall_integration_tests`：通过。
3. `ctest --test-dir build-ci -N -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)"`：发现 2 个 logging integration 用例。
4. `ctest --test-dir build-ci --output-on-failure -R "(LoggingPipelineIntegrationTest|LoggingAuditLinkIntegrationTest)"`：2/2 通过。
5. `ctest --test-dir build-ci -N -L integration`：发现 9 个 integration 测试。
6. `ctest --test-dir build-ci --output-on-failure -L integration`：9/9 通过，其中 logging 标签测试 2 个。
7. `ctest --test-dir build-ci -N -L logging`：发现 23 个 logging 测试，已覆盖 unit/contract/integration 三类标签面。

## 5. 后续边界

1. 本轮只关闭 `LOG-GATE-06`，不实现 `LoggingHealthProbe` 或 `LogQueryService`。
2. 后续若继续扩展 logging integration，应继续复用 `tests/integration/infra/logging/` 与 `integration;logging` 标签，而不是把 logging 场景散落到顶层无组件归属的 smoke 用例里。