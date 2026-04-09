# CAP-TODO-018 ExecutionSubscriptionHub 订阅骨架设计收敛

日期：2026-04-09
任务：CAP-TODO-018
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 6.3 / 6.6.1 / 6.9.2 已冻结 `ExecutionSubscriptionHub` 的职责是承接 internal-only 订阅缓冲与重同步标记，而公共 ABI 只暴露 cursor/batch、`next_cursor`、`resync_required` 与 `dropped_count`。
2. 同一设计文档 6.8 / 9.3 已把 `SubscriptionOverflow` 定义为需要稳定输出的运行时错误事实，并要求 overflow 时必须显式报告 `resync_required` 与 `dropped_count`。
3. [docs/ssot/InfraConcurrencyPolicy.md](../../../ssot/InfraConcurrencyPolicy.md) 已把 `drop_oldest` 统一收口为合法 overflow 语义，并要求 drop/reject/timeout 路径具备可观测计数；这为 018 的订阅缓冲选择 `drop_oldest` 提供了 SSOT 约束。
4. CAP-TODO-010 / 011 已提供 ServiceFacade 组合根与 unit discoverability，CAP-TODO-017 已为 query-only 路径建立只读事实输出，因此 018 可以先落 internal subscription hub 骨架，而不等待 integration fixture 或诊断路径完成。

## 2. 外部参考

1. Azure CQRS pattern 强调状态订阅应与命令执行分离，只暴露读取侧增量事实而不混入写侧治理；这支持本轮把订阅实现保持为独立 internal lane，而不是挤进 `ExecutionCommandLane`。
2. Azure Bulkhead pattern 对缓冲区和拥塞隔离的强调支持本轮把订阅路径做成独立内存缓冲，并在 overflow 时显式 fail observable，而不是静默吞掉旧事件。
3. Cache-Aside / event-stream 常见实践都要求游标续传与重同步信号清晰分离；这支持本轮在结果面上同时保留 `next_cursor` 与 `resync_required`，让调用方能决定是否重新拉快照。

## 3. Design 结论

1. `ExecutionSubscriptionHub` 作为 internal-only 组件新增于 `services/src/execution/`，维护按 `capability_id + target_id + stream_kind` 分组的流状态与内存缓冲。
2. `publish()` 只接收事件 JSON 批次并为其分配单调递增 sequence；缓冲超过上限时执行固定单值 `drop_oldest`，同时递增 `dropped_count` 并置 `resync_required=true`。
3. `subscribe()` 只接受 cursor/batch 风格请求；非法 cursor 立即 fail-closed 为 validation error，避免无定义的续传语义。
4. 公共结果严格保持在 `ExecutionSubscriptionResult`：返回 event batch JSON、`next_cursor`、`resync_required` 与 `dropped_count`，不泄漏内部缓冲、锁或 lease 实现。
5. 若调用方 cursor 已落后于当前最老可用 sequence，订阅 hub 立即把该流标记为需要 resync，并以结构化 runtime error 报告 overflow；若无 overflow，则允许返回空 batch 或增量 batch。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 internal `ExecutionSubscriptionHub` 与流状态结构 | services/src/execution/ExecutionSubscriptionHub.h、services/src/execution/ExecutionSubscriptionHub.cpp |
| cursor 解析、event batch JSON 输出与 `next_cursor` 推进 | services/src/execution/ExecutionSubscriptionHub.cpp |
| 固定 `drop_oldest` overflow 与 `resync_required` / `dropped_count` 可观测输出 | services/src/execution/ExecutionSubscriptionHub.cpp |
| 覆盖正常订阅、overflow resync 与 invalid cursor 三类 unit 场景 | tests/unit/services/execution/ExecutionSubscriptionHubTest.cpp |
| 将 subscription hub unit 接入 execution 与顶层 unit 聚合 | services/CMakeLists.txt、tests/unit/services/execution/CMakeLists.txt、tests/unit/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 `services/src/execution/ExecutionSubscriptionHub.h/.cpp`，实现 internal subscription buffer、cursor/batch 拉取、`drop_oldest` overflow 与结构化 resync 输出。
2. 测试目标：新增 `tests/unit/services/execution/ExecutionSubscriptionHubTest.cpp`，覆盖正常订阅、overflow 后 `resync_required` / `dropped_count` 与 invalid cursor validation。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_services dasall_unit_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `rg -n "drop_oldest|resync_required|InfraConcurrencyPolicy" docs/architecture/DASALL_capability_services子系统详细设计.md docs/ssot/InfraConcurrencyPolicy.md`

## 6. 风险与回退

1. 当前 hub 只是一层 internal-only 内存骨架，不负责 adapter stream 生命周期、snapshot refresh 或跨进程持久 cursor；这些能力若要引入，必须通过后续 TODO 和 integration 证据逐步推进。
2. overflow 目前采用单值 `drop_oldest`，这是 SSOT 约束下的最小实现；若未来 profile 需要 reject/block 等切换能力，必须先调整 ServicePolicyView 与设计文档，不能在本轮直接扩张。
3. 018 已提供订阅事实输出，但不代表 integration smoke、health 或 metrics 已闭环；后续 observability 和 failure integration 仍需在 024~031 中分别补齐。