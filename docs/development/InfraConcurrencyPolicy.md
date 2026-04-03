# Infra Concurrency Policy

状态：入口文档（Redirect to SSOT）
Owner：Infra 架构组 + 平台组

## 1. 目的

本文件仅用于承接 docs/architecture 与 docs/todos 中对 docs/development/InfraConcurrencyPolicy.md 的历史引用。

唯一有效的并发、backpressure 与 lock order 规范见：

1. docs/ssot/InfraConcurrencyPolicy.md

若本文件与 SSOT 存在任何差异，以 docs/ssot/InfraConcurrencyPolicy.md 为准。

## 2. 执行期最小规则

1. logging 异步队列默认使用 block，可按 profile 切换为 overrun_oldest。
2. reject、drop_oldest、overrun_oldest、block 路径都必须具备可观测计数或等价证据出口。
3. 锁顺序固定为 L0 -> L1 -> L2；持有 L2 时不得进入 L3 I/O、sink 或 exporter。
4. flush 或 shutdown 必须先冻结接收新数据，再在无 L2 锁条件下 drain 或 export。

## 3. 008 任务落地约束

1. LOG-TODO-008 只固化 block 与 overrun_oldest 的 backpressure 边界，不在本轮引入真实异步线程池。
2. 若队列策略为 block，满队列时必须返回可判定失败并增加 blocked/backpressure 计数。
3. 若队列策略为 overrun_oldest，满队列时必须替换最旧记录并增加 drop 计数。

## 4. 参考

1. docs/ssot/InfraConcurrencyPolicy.md
2. docs/architecture/DASALL_infra_logging模块详细设计.md
3. docs/architecture/DASALL_infrastructure子系统详细设计.md