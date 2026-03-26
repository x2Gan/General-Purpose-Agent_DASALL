# Infra Concurrency Policy

状态：Active
Owner：Infra 架构组 + 平台组
关联任务：INF-CONCUR-001

## 1. 目的

本文件是 infra 域并发、backpressure 与 lock order 约束的单一真相来源（SSOT），用于统一 queue overflow_policy 选择、锁顺序规则与组件级实现边界。

## 2. 适用范围

1. infra/logging、infra/audit、infra/metrics、infra/tracing、infra/watchdog。
2. platform/linux 队列、定时器、I/O 与 provider 级同步原语。
3. 后续所有引用 overflow_policy、backpressure、lock order 的设计文档、TODO 与 gate 说明。

## 3. 行业参考

1. spdlog 异步日志文档明确将满队列策略区分为 block 与 overrun_oldest，并指出单 worker 更容易保持消息顺序；本仓库 logging/audit 类异步队列遵循同类取舍，但通过仓库策略进一步约束何时允许 block、何时必须丢弃或拒绝。
2. 该参考用于校准队列 backpressure 选择，不直接替代本仓库的分层与故障语义约束。

参考：
1. https://github.com/gabime/spdlog/wiki/Asynchronous-logging

## 4. 术语统一

1. backpressure：调用方因队列、导出器或下游 sink 达到容量上限而被迫等待、降级或拒绝的机制。
2. overflow_policy：队列满时的策略选择。
3. lock order：多把锁的固定获取顺序，用于避免死锁和锁反转。
4. 术语归一：overrun_oldest 与 drop_oldest 视为同一类语义，统一归入 drop_oldest；平台层 reject 单独表示“显式拒绝，不替调用方等待”。

## 5. backpressure 决策矩阵

| 场景 | 默认策略 | 可选策略 | 适用条件 | 禁止事项 |
|---|---|---|---|---|
| platform 通用队列 | reject | block | 仅当调用方自带 deadline 且阻塞不会形成环路等待时允许 block | 不允许在平台默认路径隐式 block |
| audit 事件队列 | block | overrun_oldest | 合规证据优先，必须保留 caller 可判定结果与 fallback | 不允许 silent drop |
| logging 异步队列 | block | overrun_oldest | 默认保序与证据优先；edge/high-burst 可切换 overrun_oldest | 不允许无计数丢弃 |
| watchdog 事件队列 | block | overrun_oldest | 超时与健康信号优先，允许短时 backpressure | 不允许因队列满丢失 critical 超时事件且无审计 |
| metrics 导出队列 | drop_oldest | block | 观测信号可损失但必须保留新鲜样本与 drop 计数 | 不允许无限 block 主链路 |
| tracing 批处理队列 | drop_oldest | block | tracing 属于 lossy observability，优先保留较新的 span | 不允许在 exporter 热路径持有生产者锁长时间等待 |

## 6. 组件策略映射

| 组件 | 配置键 | 统一策略 | 原因 |
|---|---|---|---|
| platform/linux | platform.linux.queue.overflow_policy | reject | 基础层必须把 backpressure 暴露给调用方，不隐式吞掉容量风险 |
| infra/audit | infra.audit.queue.overflow_policy | block | 审计证据优先，不允许 silent loss |
| infra/logging | logging.async.overflow_policy | block | 默认保持写入顺序与证据完整；极端高峰允许按 profile 改为 overrun_oldest |
| infra/watchdog | infra.watchdog.event.overflow_policy | block | 健康/超时事件具备控制面语义，需优先保留 |
| infra/metrics | metrics.queue.overflow_policy | drop_oldest | metrics 可损失但需避免主链路被 backpressure 放大 |
| infra/tracing | tracing.overflow.policy | drop_oldest | tracing 与 metrics 一样属于 lossy observability，优先保留新鲜链路片段 |

## 7. lock order 规则

### 7.1 锁分层

1. L0：生命周期与配置锁。示例：init/shutdown、reconfigure、profile merge。
2. L1：注册表与元数据锁。示例：registry、identity map、entity catalog。
3. L2：队列、buffer、ring、snapshot 状态锁。示例：queue depth、batch buffer、pending list。
4. L3：外部 I/O 与 sink/exporter 句柄锁。示例：file sink、socket/exporter、IPC peer。

### 7.2 固定顺序

1. 允许的 lock order 只有 L0 -> L1 -> L2。
2. L3 不得在持有 L2 时获取；进入 I/O/exporter/sink 调用前，必须先从 L2 拷贝或转移快照并释放 L2。
3. 不允许从高层锁回取低层锁，例如 L2 -> L1、L3 -> L2。
4. 不允许持锁执行外部回调、日志格式化、审计写入、exporter 网络 I/O。
5. 若需要跨组件协作，统一采用“快照出锁后调用”模式，而不是跨模块嵌套持锁。

### 7.3 最小实现规则

1. queue push/pop 仅在 L2 内更新容量与状态，不做 exporter/sink 调用。
2. flush/shutdown 先通过 L0 冻结接收新数据，再在无 L2 锁条件下 drain 与 export。
3. 所有 drop_oldest、reject、timeout 路径必须递增失败计数，确保 backpressure 可观测。

## 8. 设计到 Build 的约束

1. 新增 queue/buffer 设计时，必须显式声明 overflow_policy 默认值与可选值。
2. 新增并发实现时，必须在设计或 TODO 中写出 lock order，至少说明会使用的锁层级。
3. 若组件选择 block，必须同时给出 timeout/deadline 或 bounded producer 依据。
4. 若组件选择 drop_oldest 或 reject，必须给出 dropped_total/reject_total 等可观测计数。

## 9. 规范检查清单

1. 是否明确 overflow_policy 默认值与切换条件。
2. 是否明确 backpressure 对主链路的影响边界。
3. 是否明确 lock order，且不存在 L2 持锁执行 I/O。
4. 是否为 reject/drop_oldest/block 三类路径提供可观测计数或审计出口。
5. 是否在组件设计或 TODO 中回链本文件。

## 10. 最小验证命令

```bash
rg -n "overflow_policy|lock order|backpressure" docs
rg -n "InfraConcurrencyPolicy" docs/architecture docs/todos
```