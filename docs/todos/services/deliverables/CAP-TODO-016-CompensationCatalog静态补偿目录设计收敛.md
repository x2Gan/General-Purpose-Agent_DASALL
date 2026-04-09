# CAP-TODO-016 CompensationCatalog 静态补偿目录设计收敛

日期：2026-04-09
任务：CAP-TODO-016
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 / 6.6.1 已冻结 `CompensationCatalog` 的职责是提供 `capability_id + action + version -> compensation hints` 的静态目录，只输出提示、不承担最终补偿执行。
2. 同一设计文档 6.9.2 明确 `compensation_catalog_mode=static`，要求补偿目录输出幂等要求与动作先后约束，而不是演化成自动回滚器。
3. CAP-TODO-015 已落盘 `ExecutionCommandLane` 与 injected compensation hint lookup 接缝，因此 016 可以把该接缝收敛成稳定的 static catalog，而无需改写 lane 的公共返回语义。
4. CAP-TODO-014 已冻结 `PartialSideEffect`、`evidence_refs` 与 `compensation_hints` 的结果约束，因此 016 只负责提供提示事实，不能新增恢复裁定或修改 `ErrorInfo` 语义。

## 2. 外部参考

1. Azure Compensating Transaction pattern 强调补偿步骤必须是 idempotent commands，且系统应记录足够的信息在失败后继续恢复；这支持本轮把目录条目固定为“补偿提示 + 幂等要求 + 顺序约束”，而不是直接执行补偿。
2. 同一模式还强调补偿不必严格按原始步骤逆序执行，而应由业务规则决定；这支持本轮把动作先后约束作为显式目录输出，而不是硬编码在 lane 的执行流程里。
3. Azure CQRS pattern 对命令/查询职责分离的强调也支持把补偿目录保持在 write-side internal support 组件中，不泄漏到 query-only 路径或 shared contracts。

## 3. Design 结论

1. `CompensationCatalog` 作为 internal-only 组件新增于 `services/src/execution/`，以静态条目表承载 capability/action/version 精确匹配，不做网络访问、不做动态探测。
2. 目录查询返回 `CompensationDescriptor`，其中显式区分 `compensation_hints`、`idempotency_requirements` 与 `ordering_constraints` 三类事实，满足 6.9.2 对静态模式的要求。
3. 为了兼容命令车道当前的 `std::vector<std::string>` hint 输出面，catalog 额外提供 `flatten_hints()`，把幂等与顺序约束序列化为 `idempotency:` / `order:` 前缀字符串，但仍不执行任何补偿动作。
4. `ExecutionCommandLane` 优先允许外部自定义 lookup；若未注入自定义 lookup，则回退到 `CompensationCatalog`，从而把 015 的临时接缝收敛为 016 的静态目录实现。
5. 未命中的 capability/action/version 组合返回空描述符并 fail-safe，不伪造补偿建议，也不触发任何自动补偿行为。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增静态 `CompensationCatalog` 与描述符结构 | services/src/execution/CompensationCatalog.h、services/src/execution/CompensationCatalog.cpp |
| capability/action/version 精确匹配与静态默认条目 | services/src/execution/CompensationCatalog.cpp |
| 为命令车道提供平铺 hint 输出而不执行补偿 | services/src/execution/CompensationCatalog.cpp、services/src/execution/ExecutionCommandLane.h、services/src/execution/ExecutionCommandLane.cpp |
| 覆盖已知条目、未知条目和 lane 消费 catalog 的 partial side effect 场景 | tests/unit/services/execution/CompensationCatalogTest.cpp |
| 将 catalog unit 接入 execution 与顶层 unit 聚合 | services/CMakeLists.txt、tests/unit/services/execution/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/execution/CompensationCatalog.h/.cpp`，实现静态补偿目录、描述符查询与 hint 平铺；更新 `ExecutionCommandLane` 以消费 catalog。
2. 测试目标：新增 `tests/unit/services/execution/CompensationCatalogTest.cpp`，覆盖已知/未知条目和命令车道消费 catalog 时只输出 hints、不执行补偿的行为。
3. 验收命令：
   - `cmake --build build-ci --target dasall_services dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit`

## 6. 风险与回退

1. 当前 catalog 采用 static mode 与 exact match；若未来需要 profile/manifest 驱动的动态条目，必须先更新设计文档与 TODO，不应在本轮直接扩张成策略引擎。
2. `flatten_hints()` 只是对现有 `ExecutionCommandResult.compensation_hints` 输出面的适配；真正的补偿计划编排和执行裁定仍归 Runtime/RecoveryManager，不得回灌到 services。
3. 高风险动作仍受 CAP-GATE-08 约束；即使目录里存在 `safe_mode.enter -> safe_mode.exit` 条目，也不代表当前可以执行高风险命令，只说明目录已具备静态提示事实。