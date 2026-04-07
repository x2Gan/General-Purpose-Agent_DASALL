# OTA-TODO-020 boot confirm 成功判据设计收敛

日期：2026-04-07
任务：OTA-TODO-020
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-020 定义为“补齐 boot confirm 成功判据设计”，完成判定是 success 判据与失败兜底都可二值判定，并回链解阻 OTA-TODO-011。
2. 同文档将 `OTA-BLK-03` 定义为 OTA-TODO-011 的前置阻塞，解阻条件是明确 success 判据与超时失败默认规则。
3. docs/architecture/DASALL_infra_OTA模块详细设计.md 已冻结 confirm_timeout、rollback token TTL 约束、`ota.boot.confirm.success|timeout|fail` 审计/指标名，以及“未收到明确成功标记则默认失败”的高层原则，但尚未收敛 health / heartbeat / version report 的组合判据。
4. OTA-TODO-010 与 OTA-TODO-012 已提供 SlotPlan、RollbackToken、RollbackController 语义锚点，因此 020 必须反向约束后续 011 的内部适配面，而不是继续保留“由实现自行判断”的模糊空间。

## 2. 设计收敛结论

1. V1 boot confirm success 必须同时满足四类事实：显式 `self_check_ok=true`、`HealthSnapshot.liveness=true && readiness=true`、required heartbeat freshness、slot_bound version report 与当前计划一致；任何单项缺失都不得调用 `mark_boot_success`。
2. health 与 watchdog 的联动规则冻结为：health gate 未通过时在 confirm_deadline 前保持 pending；watchdog reset 或 required heartbeat stale 则立即判定 confirm_fail，不等待超时。
3. version report 只校验当前 plan 的 `slot_bound` 工件目标版本与 `package_id`；`repo_bound` 工件因为尚未切主指针，不纳入 confirm success 判据。
4. 成功路径顺序冻结为：`ota.boot.confirm.success` -> `mark_boot_success(target_slot)` -> `ota.mark_boot_success` -> repo pointer switch -> 消费 rollback token。
5. 失败路径顺序冻结为：`ota.boot.confirm.timeout|fail` -> `mark_boot_failed(target_slot)` -> 按 `infra.ota.rollback.auto_on_confirm_fail` 决定自动回滚或冻结 apply 通道。
6. 外部参考校验：
   - Android A/B 更新明确要求首次启动后由用户空间显式 `markBootSuccessful()`；未标记成功的目标槽位在多次尝试后应自动回退到旧槽位。
   - RAUC 的 bootloader interaction 同样要求新槽位在成功启动后执行 `mark-good`，否则 bootloader 会继续 fallback 或放弃该槽位。

## 3. Design -> 阻塞解锁映射

| 阻塞点 | 设计补丁 | 结果 |
|---|---|---|
| confirm success 仅写到“self-check + health gate” | 增加 heartbeat freshness 与 slot_bound version report 判据 | 011 可直接实现组合判定，不再猜测 success 口径 |
| 超时与即时失败边界不清晰 | 冻结“health pending 到 deadline，其它关键失败立即 fail” | 011 的 timeout/fail 双路径可二值实现 |
| mark_boot_success 调用时机不清 | 固定成功/失败路径动作顺序 | 011 不会把 boot mutation 和 repo switch 顺序写反 |
| 011 被 OTA-BLK-03 阻塞 | TODO 与 architecture 同步回链解阻 | OTA-TODO-011 可进入实现轮次 |

## 4. 过程验证

1. 验收命令：
   - `rg -n "confirm|启动确认|BootConfirmationMonitor|timeout" docs/architecture/DASALL_infra_OTA模块详细设计.md`
2. 验证目标：
   - 文档中存在显式 success 判据；
   - 文档中存在 timeout 与即时失败分流；
   - 文档中存在 watchdog/health/version report 联动条件；
   - OTA-BLK-03 与 OTA-TODO-011 已回链解阻。

## 5. 结论

1. `OTA-BLK-03` 已由本轮设计补丁解阻，`OTA-TODO-011` 现在具备可执行前置条件。
2. 011 后续应直接消费本轮冻结的 success/fail 顺序、health/watchdog/version report 组合判据，不再重新讨论“只看 health ready 是否足够”。