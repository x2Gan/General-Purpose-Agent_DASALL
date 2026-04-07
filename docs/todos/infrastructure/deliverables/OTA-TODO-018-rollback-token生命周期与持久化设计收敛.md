# OTA-TODO-018 rollback token 生命周期与持久化设计收敛

日期：2026-04-07
任务：OTA-TODO-018
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md 将 OTA-TODO-018 定义为“补齐 rollback token 生命周期与持久化设计”，完成判定是 token 生命周期表必须具备字段、状态、过期与恢复语义。
2. 同文档把 `OTA-BLK-01` 定义为 OTA-TODO-012 的前置阻塞，解阻条件是明确 token 存储位置、过期策略与重启恢复规则。
3. docs/architecture/DASALL_infra_OTA模块详细设计.md 6.5/6.8/6.9 已冻结 `RollbackToken` 公共字段，并要求 token 在切槽前生成、过期后只能人工恢复，但尚未定义持久化与恢复矩阵。
4. OTA-TODO-009、010 已分别落盘 InstallExecutor 与 SlotSwitchCoordinator 骨架，因此本轮设计必须反向约束后续 012 的 rollback controller，而不是继续保留“只在内存态有效”的模糊表述。

## 2. 设计收敛结论

1. V1 rollback token 持久化介质固定为“platform 文件系统抽象上的单 active token 原子文件”，不引入 sqlite；Linux 映射到 `${platform.linux.fs.root_prefix}/ota/rollback/active-token.json`。
2. `RollbackToken` 公共字段保持不变；持久化层引入 OTA 私有的 `state` 与 `updated_at` 管理 token 生命周期，不倒灌到 contracts 或 public OTATypes。
3. token 生命周期固定为 `prepared -> armed -> consumed | expired | invalid`：
   - `prepared`：切槽前已落盘，但尚未成功执行 `set_next_boot`；
   - `armed`：`set_next_boot` 成功后进入可回滚窗口；
   - `consumed`：confirm 成功或 rollback 成功后清理；
   - `expired`：超过 TTL 仍未确认成功或完成回滚；
   - `invalid`：文件解码、校验或 schema 不合法。
4. TTL 固定为 `infra.ota.rollback.token_ttl_sec`，默认 900 秒，且必须满足 `token_ttl_sec >= confirm_timeout_sec + 60`；过期后禁止自动回滚，只允许人工恢复。
5. 重启恢复规则固定如下：
   - 启动时读取 active token 文件；
   - `prepared + active_target == previous_boot_target`：视为切槽前中断，删除 token，不触发 rollback；
   - `armed + active_target != previous_boot_target + 未过期`：继续暴露给 BootConfirmationMonitor / RollbackController；
   - `armed + active_target == previous_boot_target`：视为旧目标已恢复或切槽未生效，写审计后清理 token；
   - `expired`：移动到 expired/corrupt 证据路径并将 OTAHealthProbe 标记为 degraded；
   - `invalid`：重命名为 `.corrupt.<ts>`，禁止自动 apply，要求人工介入。

## 3. Design -> 阻塞解锁映射

| 阻塞点 | 设计补丁 | 结果 |
|---|---|---|
| token 存储位置未定 | 固定为 platform state root 下单文件 `ota/rollback/active-token.json` | 不再需要在 012 中选择 file/sqlite |
| 过期策略未定 | 新增 `infra.ota.rollback.token_ttl_sec` 和最小约束 `>= confirm_timeout_sec + 60` | 012 可直接判断 token 是否可自动回滚 |
| 重启恢复规则未定 | 新增 prepared/armed/expired/invalid 恢复矩阵 | 012 可在同一边界内处理开机恢复 |
| 012 被 OTA-BLK-01 阻塞 | TODO 与 architecture 同步回链解阻 | OTA-TODO-012 可进入实现 |

## 4. 过程验证

1. 验收命令：
   - `rg -n "RollbackToken|rollback token|expires_at|持久化" docs/architecture/DASALL_infra_OTA模块详细设计.md`
2. 验证目标：
   - 文档中存在明确的 token 存储位置；
   - 文档中存在生命周期状态与恢复矩阵；
   - 文档中存在 TTL/过期规则；
   - OTA-BLK-01 与 OTA-TODO-012 已回链解阻。

## 5. 结论

1. `OTA-BLK-01` 已由本轮设计补丁解阻，`OTA-TODO-012` 现在具备可执行前置条件。
2. 012 应直接消费本轮冻结的 state file、TTL 和恢复矩阵，不再重新讨论 file/sqlite 介质选择。