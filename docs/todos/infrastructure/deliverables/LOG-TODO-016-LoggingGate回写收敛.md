# LOG-TODO-016 Logging Gate 回写收敛

日期：2026-04-03  
任务：LOG-TODO-016  
状态：已完成

## 1. 输入依据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-TODO-016 定义为“回写 logging 质量门与交付证据”，完成判定是每个 gate 都具有通过或失败结论与证据命令。
2. LOG-TODO-001 至 LOG-TODO-015 已全部完成，其中 LOG-TODO-014 收敛了 `dasall_infra` 的 logging 构建接线，LOG-TODO-015 收敛了 logging 组件测试注册与 `logging` 标签 discoverability。
3. 本轮新增验证证据来自以下命令：`ctest --test-dir build-ci -N`、`ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`，并辅以 `tests/integration/infra/logging/` 缺失这一文件级证据判断 integration gate 状态。

## 2. Gate 结论

| Gate ID | 当前状态 | 证据 | 结论 |
|---|---|---|---|
| LOG-GATE-01 | Pass | LOG-TODO-001/002/003/010 已完成；`ctest -L logging` 覆盖 `ILogger` / `IAuditLinkAdapter` / `LogEvent` / `LoggingErrors` 边界测试 | logging public boundary 已冻结并保持可编译、可验证 |
| LOG-GATE-02 | Pass | LOG-TODO-009 已完成；`AuditLinkAdapterTest` 与 `AuditLinkAdapterBoundaryContractTest` 在 logging 标签测试面内通过 | 审计关联与普通日志主链分离仍有测试证明 |
| LOG-GATE-03 | Pass | LOG-TODO-010~013 已完成；`ctest --test-dir build-ci --output-on-failure -L unit` 110/110 通过，`-L contract` 132/132 通过 | queue/sink/config/metrics failure 路径维持错误码与可观测出口 |
| LOG-GATE-04 | Pass | `ctest --test-dir build-ci -N` 发现 249 个测试；`ctest --test-dir build-ci -N -L logging` 发现 21 个 logging 测试 | logging 测试发现性已独立收敛 |
| LOG-GATE-05 | Pass | LOG-TODO-014~016 仅修改 CMake、测试标签、TODO、交付物与 worklog；未改 public headers 或 contracts 映射 | 本轮无 breaking change，评审门未被触发 |
| LOG-GATE-06 | Blocked | `tests/integration/infra/logging/` 仍为空；LOG-BLK-004 仅解除了 integration 拓扑，不代表 logging 组件用例已落盘 | integration 准入门仍未通过，后续需补 logging integration 用例与标签注册 |

## 3. Blocker 状态快照

| Blocker ID | 当前状态 | 是否影响 LOG-TODO-014~016 | 说明 |
|---|---|---|---|
| LOG-BLK-001 | Resolved | 否 | LoggingConfig / source acceptance / audit gate 已冻结并支撑 LOG-TODO-012 |
| LOG-BLK-002 | Resolved | 否 | metrics bridge 接入协议已冻结并支撑 LOG-TODO-013 |
| LOG-BLK-003 | Deferred | 否 | 仅影响后续 LoggingHealthProbe，当前不阻塞 014~016 |
| LOG-BLK-004 | Resolved | 否 | 顶层 integration 拓扑已解阻，但组件用例仍未落盘，因此只影响 LOG-GATE-06 |
| LOG-BLK-005 | Deferred | 否 | 仅影响后续 LogQueryService，当前不阻塞 014~016 |

## 4. 验证与回退记录

1. `ctest --test-dir build-ci -N`：发现 249 个测试。
2. `ctest --test-dir build-ci --output-on-failure -L unit`：110/110 通过。
3. `ctest --test-dir build-ci --output-on-failure -L contract`：132/132 通过。
4. `Build_CMakeTools` 与 `RunCtest_CMakeTools` 仍报“无法配置项目”，本轮继续沿用显式 `cmake`/`ctest` 作为实际验收链路。
5. 014~016 未触发代码回退；唯一需要保留的受限项是 `LOG-GATE-06` 尚未通过。

## 5. 后续边界

1. logging 专项 TODO 的 001~016 已全部完成；后续若继续推进，应转入 integration 用例、LoggingHealthProbe 与 LogQueryService 的新一轮任务拆解。
2. 下一优先 blocker 不是 014~016 的历史前置，而是 `LOG-BLK-003` 与 `LOG-BLK-005` 对 health/log query 设计边界的冻结不足。