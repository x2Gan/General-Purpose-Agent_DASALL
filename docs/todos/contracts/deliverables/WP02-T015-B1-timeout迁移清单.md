# WP02-T015 B1 timeout 字段迁移清单

最近更新时间：2026-03-14
关联阻塞项：B1
关联来源：WP02-T010 Time/Deadline 规范、WP02-T014 评审纪要、WP02-T015 M2 冻结包

## 1. 任务理解

本清单只处理 B1：统一历史 `timeout_seconds` 到 `timeout_ms` 的迁移口径，补齐进入 WP-03 前所需的迁移映射、弃用窗口与回退策略。

目标不是引入新时间语义，而是把 T010 已冻结的时间规则落成可执行迁移基线。

## 2. 迁移约束

1. 主语义以 `timeout_ms` 为准，符合 T010 中“单位统一为毫秒”的冻结结论。
2. `deadline_at` 仍是执行判定主字段；`timeout_ms` 是策略输入与诊断字段。
3. 历史 `timeout_seconds` 只允许作为兼容读取入口，不再作为新写入字段。
4. 不允许把 `ttl`、`deadline_at`、`timeout_ms` 混为同一字段语义。

## 3. 字段映射总表

| 历史字段 | 新字段 | 迁移规则 | 写入策略 | 读取策略 |
|---|---|---|---|---|
| timeout_seconds | timeout_ms | `timeout_ms = timeout_seconds * 1000` | 禁止新写入 | 允许兼容读取并归一化 |
| timeout_ms | timeout_ms | 保持不变 | 新写入唯一合法字段 | 正常读取 |
| created_at + timeout_seconds | created_at + timeout_ms + deadline_at | 先换算 `timeout_ms`，再推导 `deadline_at` | 写入时补齐 `deadline_at` | 读取时若缺 `deadline_at` 可按规则推导 |
| explicit deadline_at + timeout_seconds | deadline_at + timeout_ms | `deadline_at` 优先，`timeout_ms` 仅保留诊断值 | 写入时保留 `deadline_at` 主语义 | 读取时不以秒字段覆盖 deadline |

## 4. 迁移步骤

1. 兼容读取阶段：允许读取 `timeout_seconds`，但必须立刻归一化为 `timeout_ms`。
2. 双字段过渡阶段：若输入同时存在 `timeout_seconds` 与 `timeout_ms`，要求二者换算一致；不一致直接判为无效输入。
3. 新写入收敛阶段：所有新对象、新测试、新样例仅允许写 `timeout_ms`。
4. 清理阶段：确认无旧消费方依赖 `timeout_seconds` 后，将其降级为只读兼容字段，后续版本再评估移除。

## 5. 弃用窗口

| 阶段 | 要求 | 退出条件 |
|---|---|---|
| Phase 1 兼容读取 | 允许读取 `timeout_seconds` | 所有入口具备归一化逻辑 |
| Phase 2 写入收敛 | 禁止新增写入 `timeout_seconds` | 契约测试与样例全部切换到 `timeout_ms` |
| Phase 3 清理评审 | 评估是否移除兼容读取 | 无旧消费方依赖，且版本评审通过 |

## 6. 冲突与判定规则

1. 若仅有 `timeout_seconds` 且无 `created_at`、`deadline_at`，对象不得视为完整契约对象。
2. 若同时存在 `timeout_seconds` 与 `timeout_ms` 且换算不一致，判定为输入冲突。
3. 若外部显式提供 `deadline_at`，执行判定必须以 `deadline_at` 为准，不得回退到 `timeout_seconds`。

## 7. 回退策略

1. 若迁移后发现旧链路仍写入 `timeout_seconds`，先恢复兼容读取，不回退主语义。
2. 若发现毫秒/秒混用导致误判，回退到“只读兼容 + 禁止新写入”状态，并补录冲突点。
3. 若执行实现与 T010 冻结语义冲突，以 T010 为准回退实现。

## 8. 交付物映射

1. 本文件即 B1 的“迁移文档评审通过”依据。
2. 与 [WP02-T015-M2冻结包](WP02-T015-M2%E5%86%BB%E7%BB%93%E5%8C%85.md) 联动，用于关闭 B1。