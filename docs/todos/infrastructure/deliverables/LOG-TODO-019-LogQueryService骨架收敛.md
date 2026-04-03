# LOG-TODO-019 LogQueryService 骨架收敛

日期：2026-04-03  
任务：LOG-TODO-019  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 已将 `LOG-TODO-019` 定义为“实现 LogQueryService 受控查询与本地 artifact 导出骨架”，验收要求为 trace/session 精确 selector、`PolicyDenied`/`ValidationFieldMissing` 负例与本地 artifact 导出正例可稳定判定。
2. [docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md](docs/todos/infrastructure/deliverables/LOG-BLK-005-LogQueryService设计收敛.md) 已冻结 `LogQueryRequest`、`LogQueryAccessContext`、`LogQueryResult` 与“local artifact only”的边界。
3. [docs/architecture/DASALL_infra_logging模块详细设计.md](docs/architecture/DASALL_infra_logging模块详细设计.md) 6.10.2 已明确 logging 只接受 trace/session 精确 selector、上游 allow 证明与 `infra.logging.export.enable_diag_pull` gate。
4. [docs/architecture/DASALL_infra_diagnostics模块详细设计.md](docs/architecture/DASALL_infra_diagnostics模块详细设计.md) 6.6/6.8/6.9 已冻结 diagnostics 的本地/远程导出分离、policy guard 和 artifact 摘要风格，说明 logging 不应发明第二套远程导出/鉴权体系。
5. 现有代码已提供本轮需要复用的最小对象：
   - `policy::PolicyDecisionRef`
   - `InfraContext`
   - `logging::LoggingConfig` 中的 `enable_diag_pull` 语义
   - `LogEvent.attrs["trace_id"]` / `LogEvent.attrs["session_id"]` 的上下文富化入口

## 2. 研究学习结果

### 2.1 本地证据

1. `LogQueryService` 的职责只是在本地已脱敏日志上做受控检索并返回 artifact 摘要，不能直接返回原始记录容器，也不能接管 diagnostics 的 remote export。
2. allow/deny 判定必须由上游 policy gate 完成；logging 只验证 allow 证明是否完整与可审计，不能在本模块内做二次确认。
3. LoggingFacade 已把 `trace_id` / `session_id` 富化到 `LogEvent.attrs`，因此 019 可以在现有结构化字段上实现精确 selector，而不需要扩张 `LogEvent` 顶层字段。

### 2.2 外部参考

1. OWASP Logging Cheat Sheet 强调：日志与导出属于高风险功能，应限制读取权限、记录导出行为、避免暴露过量敏感数据，并优先采用受控的本地存储/提取路径而不是默认开放远程导出。

### 2.3 可落地启发

1. 查询面必须保持有限形态，只接受 `trace_id`/`session_id` 的精确匹配与有序时间窗，不把自由检索 DSL 带进 logging。
2. 本地 artifact 只返回引用与摘要，不返回原始记录集合，从接口层就把数据暴露面压缩到最小。
3. 本轮先用 internal record reader 抽象承接“本地索引/扫描”缺口，后续若引入真实索引优化，也必须留在同一 internal provider 边界后面。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 `LogQueryRequest` / `LogQueryAccessContext` / `LogQueryResult` 的 internal 形态 | logging 设计 6.10.2；LOG-BLK-005 交付物 | `LogQueryService.h` 内部对象与校验规则 | selector、allow proof、artifact 摘要字段可二值判定 |
| D2 | 收敛本地 record reader 与 artifact 摘要生成边界 | diagnostics 设计 6.6/6.8；OWASP 日志保护建议 | internal `ILogQueryRecordReader` 与 checksum/artifact_ref 约束 | local-only 边界明确，不引入 remote export |
| D3 | 锁定 Build 三件套 | `LOG-TODO-019` 验收命令；现有 unit/integration 注册模式 | 代码目标、测试目标、验收命令 | 三件套完整，允许进入 Build |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| `LogQueryService` 只接受精确 selector + allow proof + local artifact | 新增 `infra/src/logging/LogQueryService.h/.cpp`，不新增 public export/query DSL |
| 本地记录读取走 internal `ILogQueryRecordReader` | unit 与 integration 测试分别用内存 reader 和 LoggingFacade 富化事件回放，不接远程导出依赖 |
| `enable_diag_pull` gate 在 logging 边界内强制执行 | unit 测试覆盖 `PolicyDenied` 与配置 gate 拒绝 |
| 结果只返回 artifact 摘要 | integration 测试验证 `artifact_ref`、`match_count`、`truncated`、`checksum`、`created_at`，不暴露原始记录集合 |

### 4.2 Build 三件套

1. 代码目标：新增 `infra/src/logging/LogQueryService.h` 与 `infra/src/logging/LogQueryService.cpp`，并接入 `infra/CMakeLists.txt`。
2. 测试目标：新增 `tests/unit/infra/logging/LogQueryServiceTest.cpp` 与 `tests/integration/infra/logging/LogQueryIntegrationTest.cpp`，并接入 unit/integration CMake 聚合与 `logging` 标签面。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_log_query_service_unit_test dasall_log_query_integration_test dasall_unit_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`
   - `ctest --test-dir build-ci -N -L integration`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci -N -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L logging`
   - `ctest --test-dir build-ci --output-on-failure -L unit`

### 4.3 D Gate

结论：PASS。

理由：

1. query schema、allow proof、local artifact only 和 internal reader 的出口已经明确，不再依赖额外 blocker。
2. Build 三件套已锁定，且范围仍限制在 `infra/src/logging`、`tests/unit/infra/logging`、`tests/integration/infra/logging`、专项 TODO/交付物/worklog 之内。
3. 本轮不会引入 remote export、全文检索 DSL、audit 主存储 join 或 runtime 侧二次授权。

## 5. Build 合规提醒

1. `LogQueryService` 只能返回 artifact 摘要，不能把原始 `LogEvent` 集合暴露给调用方。
2. 测试必须至少覆盖 1 个正例和 1 个负例；本轮以 trace/session 查询命中作为正例，以 `ValidationFieldMissing` 与 `PolicyDenied` 作为负例。
3. 由于本轮会触及 unit/integration 注册与 logging 标签 discoverability，Build 阶段除定向测试外还必须补 `ctest -N -L integration`、`ctest -L integration`、`ctest -N -L logging` 与 `ctest -L logging`。

## 6. Build 落地结果

1. 新增 [infra/src/logging/LogQueryService.h](infra/src/logging/LogQueryService.h) 与 [infra/src/logging/LogQueryService.cpp](infra/src/logging/LogQueryService.cpp)，收敛 `LogQueryRequest`、`LogQueryAccessContext`、`LogQueryResult`、internal `ILogQueryRecordReader` 与 `LogQueryService`，并固定：
   - 只接受 `trace_id` / `session_id` 精确 selector
   - 只接受有序时间窗与正整数 `max_records`
   - 只接受上游 `PolicyDecision::Allow` 证明
   - 只返回 local artifact 摘要，不暴露原始记录集合
2. `LogQueryService::query()` 已把主要分支压缩为 contracts 语义内的可判定出口：
   - request/access 缺字段 -> `ValidationFieldMissing`
   - `enable_diag_pull` 关闭或 allow proof 非 `Allow` -> `PolicyDenied`
   - 缺少 local record reader 或 artifact timestamp 非法 -> `ToolExecutionFailed`
   - 正例命中 -> `artifact_ref` / `match_count` / `truncated` / `checksum` / `created_at`
3. 新增 [tests/unit/infra/logging/LogQueryServiceTest.cpp](tests/unit/infra/logging/LogQueryServiceTest.cpp)，覆盖：
   - request 形态非法
   - allow proof 缺失/非 Allow
   - `enable_diag_pull` gate 拒绝
   - 缺少 local record reader
   - trace selector 正例与 local artifact 摘要返回
4. 新增 [tests/integration/infra/logging/LogQueryIntegrationTest.cpp](tests/integration/infra/logging/LogQueryIntegrationTest.cpp)，通过 `LoggingFacade` 富化 `trace_id` / `session_id`，验证 trace 与 session selector 都能命中已富化本地记录，并验证 `max_records` 截断与 artifact 摘要字段。
5. 更新 [infra/CMakeLists.txt](infra/CMakeLists.txt)、[tests/unit/infra/CMakeLists.txt](tests/unit/infra/CMakeLists.txt)、[tests/unit/CMakeLists.txt](tests/unit/CMakeLists.txt)、[tests/integration/infra/logging/CMakeLists.txt](tests/integration/infra/logging/CMakeLists.txt) 与 [tests/integration/CMakeLists.txt](tests/integration/CMakeLists.txt)，将 `LogQueryService` 与新增 unit/integration target 纳入 `dasall_infra`、`unit;logging`、`integration;logging` 与顶层聚合目标。

## 7. 验证结果

1. `ctest --test-dir build-ci -N -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`：发现 2 个定向测试。
2. `ctest --test-dir build-ci --output-on-failure -R "(LogQueryServiceTest|LogQueryIntegrationTest)"`：2/2 通过。
3. `ctest --test-dir build-ci -N -L integration`：发现 10 个 integration 测试。
4. `ctest --test-dir build-ci --output-on-failure -L integration`：10/10 通过，其中 logging integration 3/3 通过。
5. `ctest --test-dir build-ci -N -L logging`：发现 26 个 logging 标签测试。
6. `ctest --test-dir build-ci --output-on-failure -L logging`：26/26 通过。
7. `ctest --test-dir build-ci --output-on-failure -L unit`：112/112 通过。
8. `ctest --test-dir build-ci -N`：发现 254 个全量测试。
9. `Build_CMakeTools` 在本仓库仍报“无法配置项目”，本轮验收继续沿用显式 `cmake` / `ctest` 链路完成，未触发代码回退。

## 8. 结论

1. `LOG-TODO-019` 已从“边界冻结但实现未落盘”推进到“受控 selector + allow proof 校验 + local artifact 摘要导出骨架已可执行验证”。
2. `LogQueryService` 没有新增 public export/query DSL，也没有把 remote export、二次授权或审计主存储 join 带回 logging 子域，仍然满足 diagnostics/policy/ADR 边界约束。
3. 019 完成后，当前 logging 专项 TODO 中已不存在未完成的原子任务；若继续推进，应新开围绕 retention、真实索引或运行时 wiring 的后续原子任务，而不是回退当前骨架边界。