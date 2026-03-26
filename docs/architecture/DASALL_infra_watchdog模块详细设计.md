# DASALL infra/watchdog 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
适用模块：infra/watchdog

## 1. 模块概览

### 1.1 模块定位

infra/watchdog 属于 Infrastructure Layer（Layer 1），负责关键执行线程/任务的心跳与 deadline 监督，并将超时事实转化为可观测事件与恢复建议输入，不直接执行恢复动作。

模块目标：
1. 为关键线程提供统一心跳采集与 deadline 监督能力。
2. 在超时场景下输出结构化 TimeoutEvent、审计事件与指标。
3. 与 infra/health、infra/logging、runtime/recovery 链路形成“证据-建议-执行”解耦闭环。

### 1.2 层级边界与依赖方向

上游调用方：runtime、apps、infra/health、infra/service facade。  
同层协同：infra/logging、infra/metrics、infra/config、infra/health。  
下游依赖：platform 时钟/线程抽象、contracts 错误语义。  
禁止方向：infra/watchdog 不反向依赖 runtime/cognition/tools 等业务实现类。

### 1.3 来源依据

1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、3.7、5.10、8.7、11.1）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2、4.3、7）
3. docs/adr/ADR-005-architecture-review-baseline.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/plans/DASALL_contracts冻结实施计划.md
8. docs/todos/DASALL_contracts冻结TODO总表.md
9. docs/architecture/DASALL_infrastructure子系统详细设计.md（6.3 中 WatchdogAgent 输入输出约束）
10. docs/development/DASALL_工程协作与编码规范.md
11. 代码现状：infra/CMakeLists.txt、infra/src/placeholder.cpp、infra/include（空）、infra/src/health（空）
12. 行业参考：systemd watchdog、Kubernetes liveness/readiness timeout 设计、Azure Retry/Health 责任分离模式

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| WDG-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10 | Must | infra 必须提供基础监控能力，watchdog 属于基础设施统一治理组成 | 子组件/流程 |
| WDG-C002 | DASSALL_Agent_architecture.md 11.1 | Must | 关键线程必须接入 watchdog，且超时可被观测 | 接口/流程 |
| WDG-C003 | DASSALL_Agent_architecture.md 3.7 | Must | 上层依赖下层抽象；infra 不反向依赖业务模块实现 | 依赖关系 |
| WDG-C004 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra -> 业务模块实现 为禁止依赖方向 | include/CMake |
| WDG-C005 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用通过冻结 contracts 或抽象接口，不直连实现细节 | 接口语义 |
| WDG-C006 | ADR-005 | Must | 在已冻结架构与边界前提下落地，不反向改写主架构结论 | 设计治理 |
| WDG-C007 | ADR-006 | Must-Not | watchdog 不承担语义上下文装配与 Prompt 渲染职责，仅记录/上报观测事实 | 职责边界 |
| WDG-C008 | ADR-007 | Must-Not | watchdog 不做失败语义归因与恢复裁定，仅输出 timeout 证据与建议事件 | 异常流程 |
| WDG-C009 | ADR-008 | Must | watchdog 仅服务全局主控和协同链路，不拥有调度主权 | 控制权边界 |
| WDG-C010 | contracts 冻结计划 6/10 + TODO M5 | Must | 以 contracts V1 Ready 为输入，新增字段优先 optional 与向后兼容 | 兼容策略 |
| WDG-C011 | contracts 冻结计划 + TODO 总表 | Must-Not | 不把 watchdog 实现细节（线程模型/扫描策略）写入 contracts 共享对象 | contracts 对齐 |
| WDG-C012 | 工程规范 3.6 | Must | 错误不能吞没；超时失败必须可被日志/指标/审计感知 | 错误语义 |
| WDG-C013 | 工程规范 3.7 | Should | 新增公共接口同步新增 unit/contract/integration 测试 | 测试门禁 |
| WDG-C014 | DASSALL_Agent_architecture.md 8.6 + Blueprint 5.1 | Must | profile 仅裁剪能力/实现，不得绕过审计与主控链路 | 配置策略 |
| WDG-C015 | DASALL_infrastructure子系统详细设计.md 6.3 | Must | WatchdogAgent 输入为心跳与 deadline，输出超时事件/审计事件，超时动作可配置为告警或恢复请求 | 输入输出契约 |
| WDG-C016 | systemd watchdog/K8s timeout 实践 | Should | 心跳丢失检测需包含阈值、抖动容忍、连续失败窗口，避免瞬时误报 | 算法策略 |

### 2.2 约束抽取结论

Must：边界单向依赖、关键线程接入、超时可观测、contracts 兼容优先。  
Should：抖动容忍、故障注入测试、profile 参数差异化。  
Must-Not：不越权执行恢复、不污染 contracts、不过度扩展到 runtime 主控实现。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| watchdog 模块可编译并承载能力 | 缺失 | infra 当前仅编译 placeholder.cpp，无 watchdog 构建入口 | High | P0 |
| watchdog 接口头文件体系 | 缺失 | infra/include 为空，缺少 IWatchdogService/IHeartbeatSource | High | P0 |
| 心跳注册与超时检测实现 | 缺失 | 无心跳注册表、无 deadline 检测循环、无超时策略 | High | P0 |
| 超时事件与审计输出 | 缺失 | 无 TimeoutEvent 发布，无审计记录路径 | High | P0 |
| 与 health 协同降级路径 | 缺失 | 无 watchdog 状态探针与 degraded 联动接口 | Medium | P1 |
| 配置策略与 profile 差异化 | 缺失 | 无 timeout/grace/window/action 配置项 | Medium | P1 |
| 测试基线（unit/integration/failure） | 缺失 | 无 watchdog 相关测试目标与 Gate | High | P0 |

证据：
1. infra/CMakeLists.txt 仅纳入 src/placeholder.cpp。
2. infra/src/placeholder.cpp 为 keep_library_non_empty 占位实现。
3. infra/include 当前为空目录。
4. infra/src/health 当前为空目录，尚无 watchdog 协同落点。

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 若 watchdog 直接驱动 runtime 恢复动作，将越权到 ADR-007 执行域 | 破坏职责分层 | High |
| 语义重复 | 若 watchdog 重定义 ErrorInfo/ResultCode 共享语义 | contracts 漂移与返工 | High |
| 依赖反转 | 若 runtime 直接依赖 watchdog 具体实现类而非接口 | 难以替换与测试 | Medium |
| 误报风暴 | 无抖动容忍与连续失败窗口，瞬时抖动触发大量告警 | 稳定性与可运维性下降 | Medium |

---

## 4. 候选方案对比

### 4.1 候选方案概述

1. 方案 A：单线程同步轮询 watchdog（按固定周期遍历全部心跳源）。
2. 方案 B：分层 watchdog 引擎（Registry + DeadlineWheel + TimeoutPolicy + EventPublisher）。
3. 方案 C：外部 supervisor 优先（由外部进程监控，进程内仅透传状态）。

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 同步轮询 | 中 | 中 | 低 | 规模增长时检测延迟高，误报与漏报风险增大 | 淘汰：仅适合 PoC |
| B 分层 watchdog 引擎 | 高 | 高 | 中 | 组件增多，需要清晰接口与测试约束 | 保留并采纳 |
| C 外部 supervisor 优先 | 中 | 中 | 高 | 部署耦合强，edge_minimal 成本高，链路追踪弱 | 暂不采纳，列为 v2 |

### 4.3 行业方案匹配结论

1. systemd watchdog 模式支持“喂狗 + 超时阈值 + 严重级别分层”，可映射心跳治理。
2. Kubernetes 探针思想可映射为“liveness 级故障”与“readiness 级降级”分离。
3. Azure Retry/Health 模式强调执行与建议分离，匹配 ADR-007 边界。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层 watchdog 引擎。

### 5.2 放弃其他方案理由

1. 方案 A：实现简单但不具备规模扩展与精细化治理能力。
2. 方案 C：当前阶段会引入额外运维面与部署复杂度，超出本轮最小交付目标。

### 5.3 一致性说明

1. 与架构一致：watchdog 位于 infra，仅负责基础监督与观测输出。
2. 与 ADR 一致：
   - 不接管上下文与 Prompt 语义（ADR-006）。
   - 不执行恢复裁定，仅提供 timeout 证据（ADR-007）。
   - 不拥有全局调度权（ADR-008）。
3. 与 contracts 一致：消费 V1 Ready 的 ErrorInfo/ResultCode/EventEnvelope 语义，不新增实现泄漏字段。

---

## 6. 详细设计

### 6.1 职责边界

watchdog 职责：
1. 注册/注销被监督实体（线程、任务执行器、后台循环）。
2. 采集心跳并维护 deadline 状态。
3. 在超时时输出 TimeoutEvent、审计事件、指标与恢复建议请求（仅建议）。
4. 提供查询接口用于 health/runtime 读取当前监督快照。

watchdog 非职责：
1. 不直接执行线程重启、任务重试、回滚等恢复动作。
2. 不维护 runtime 主状态机。
3. 不定义或改写 contracts 共享对象。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| WatchdogServiceFacade | 对外统一入口，负责生命周期与 API 暴露 |
| HeartbeatRegistry | 管理被监督实体注册、唯一性、元信息 |
| HeartbeatIngestor | 接收 heartbeat 打点并更新 last_seen |
| DeadlineWheel | 维护 deadline 索引并周期扫描到期实体 |
| TimeoutPolicyEngine | 基于阈值/宽限窗口/重试窗口判定 timeout 等级 |
| TimeoutEventPublisher | 发布 timeout 事件到事件总线/日志系统 |
| WatchdogAuditBridge | 关键超时写审计记录 |
| WatchdogMetricsBridge | 输出超时计数、扫描延迟、心跳丢失率 |
| RecoveryRequestEmitter | 仅发出 RecoveryHintRequest，不执行恢复 |
| WatchdogHealthProbe | 向 health 暴露 watchdog 自身健康状态 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| WatchdogServiceFacade | init/start/stop/query 请求 | 监督快照/结果码 | 所有失败返回 ResultCode + ErrorInfo |
| HeartbeatRegistry | register/update/unregister | EntityDescriptor 集合 | entity_id 全局唯一（模块内） |
| HeartbeatIngestor | entity_id + heartbeat_ts + deadline_hint | 状态存储更新 | heartbeat 乱序需容忍并丢弃过旧样本 |
| DeadlineWheel | Registry 索引、当前时钟 | 到期候选列表 | 扫描复杂度受上限控制 |
| TimeoutPolicyEngine | 到期候选、历史失败窗口 | TimeoutDecision | 支持 grace 与连续失败阈值 |
| TimeoutEventPublisher | TimeoutDecision | TimeoutEvent | 必须附 trace_id/session_id/task_id（可 unknown） |
| WatchdogAuditBridge | 高风险 timeout 决策 | AuditEvent | 超时事件不可静默丢弃 |
| WatchdogMetricsBridge | 扫描/超时统计 | Metrics Exporter | 指标名和标签白名单治理 |
| RecoveryRequestEmitter | TimeoutDecision | RecoveryHintRequest | 仅建议，不含执行句柄 |
| WatchdogHealthProbe | 内部线程与队列状态 | HealthProbeResult | watchdog 自身故障可被 health 感知 |

### 6.4 子组件依赖关系

1. WatchdogServiceFacade -> HeartbeatRegistry、HeartbeatIngestor、DeadlineWheel、TimeoutPolicyEngine。
2. DeadlineWheel -> TimeoutPolicyEngine。
3. TimeoutPolicyEngine -> TimeoutEventPublisher、WatchdogAuditBridge、WatchdogMetricsBridge、RecoveryRequestEmitter。
4. WatchdogServiceFacade -> WatchdogHealthProbe（用于 health 集成）。

依赖约束：
1. 仅依赖抽象接口，不依赖 runtime/cognition 具体实现。
2. 与 infra/logging、infra/metrics、infra/health 通过接口协作。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| WatchedEntityDescriptor | entity_id, entity_type, owner_module, criticality, timeout_ms, grace_ms | entity_id 不可复用；criticality 固定枚举可扩展 | 仅在 infra 私有域，外部仅见引用标识 |
| HeartbeatSample | entity_id, heartbeat_ts, deadline_ts, seq_no | 单调序号优先；乱序样本可丢弃 | 与 contracts 时间/ID 规则兼容 |
| TimeoutDecision | entity_id, timeout_level, consecutive_miss, reason_code, evidence_ref | timeout_level 至少含 Warning/Critical/Fatal | reason_code 映射 ResultCode/ErrorInfo |
| WatchdogSnapshot | total_entities, timed_out_entities, degraded_entities, scan_lag_ms, ts | 快照可回放、版本单调递增 | 不新增 contracts 共享字段 |
| RecoveryHintRequest | reason_code, target_ref, suggested_action, evidence_ref | 仅建议，不含执行参数细节 | 对齐 ADR-007 的建议-执行分离 |

### 6.6 核心接口语义定义

建议头文件分布：infra/include/

1. IWatchdogService
   - init(config): 初始化 watchdog 子系统。
   - start(): 启动扫描循环。
   - stop(timeout_ms): 优雅停机并刷出未发送事件。
   - register_entity(descriptor): 注册监督实体。
   - unregister_entity(entity_id): 注销监督实体。
   - heartbeat(sample): 提交心跳。
   - snapshot(): 获取当前监督快照。

2. IHeartbeatSource
   - emit_heartbeat(entity_id): 主动上报心跳。
   - describe(): 返回实体静态元信息。

3. ITimeoutPolicy
   - evaluate(candidate, history): 产出 TimeoutDecision。

前置条件：
1. init 成功且配置加载完成。
2. register_entity 先于 heartbeat。

后置条件：
1. timeout 判定后必须至少产出日志或指标之一，关键超时必须包含审计记录。
2. snapshot 可读取且与最近一次扫描结果一致。

错误语义（infra 私有错误码域，映射 contracts::ResultCode）：
1. INF_E_WATCHDOG_ENTITY_DUPLICATE
2. INF_E_WATCHDOG_ENTITY_NOT_FOUND
3. INF_E_WATCHDOG_HEARTBEAT_STALE
4. INF_E_WATCHDOG_SCAN_OVERDUE
5. INF_E_WATCHDOG_TIMEOUT_CRITICAL
6. INF_E_WATCHDOG_EVENT_PUBLISH_FAIL
7. INF_E_WATCHDOG_AUDIT_WRITE_FAIL

### 6.7 主流程时序（正常）

1. InfraServiceFacade 调用 WatchdogServiceFacade.init。
2. runtime/infra 子组件通过 register_entity 注册关键实体。
3. HeartbeatIngestor 持续接收心跳并更新状态。
4. DeadlineWheel 周期扫描到期候选。
5. TimeoutPolicyEngine 对候选进行判定。
6. 若无超时，更新 WatchdogSnapshot 并输出常规指标。
7. 若超时，发布 timeout 事件并按策略输出审计与建议请求。
8. health/runtime 通过 snapshot 或事件流消费监督状态。

### 6.8 异常与恢复时序

异常分类：
1. 瞬时延迟：心跳偶发迟到但落在 grace 窗口。
2. 持续丢失：连续 N 次扫描未收到心跳。
3. 发布失败：timeout 事件或审计写入失败。
4. 监控自故障：watchdog 扫描线程延迟超阈值。

恢复动作：
1. 瞬时延迟：标记 warning，不触发恢复建议，仅记录指标。
2. 持续丢失：标记 critical/fatal，发布 timeout 事件并发出 RecoveryHintRequest。
3. 发布失败：本地 fallback ring-buffer 缓存 + fail 指标 + 强告警。
4. 监控自故障：WatchdogHealthProbe 输出 degraded，触发 infra_safe_mode 建议事件。

兜底策略：
1. 若事件总线不可用，至少落本地审计与错误日志，禁止静默丢失。
2. 若扫描线程连续 M 次超期，watchdog 进入 safe_observe_mode，仅保留快照读取和错误上报。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.watchdog.enabled | true | 默认/Profile/部署 | 是否启用 watchdog |
| infra.watchdog.scan.interval_ms | 500 | Profile/部署 | 扫描周期 |
| infra.watchdog.timeout_ms | 15000 | Profile/部署 | 默认超时阈值 |
| infra.watchdog.grace_ms | 2000 | Profile/部署 | 抖动宽限窗口 |
| infra.watchdog.consecutive_miss_threshold | 3 | Profile/部署 | 连续丢失阈值 |
| infra.watchdog.timeout.level.policy | warn_then_critical | Profile/部署 | 超时升级策略 |
| infra.watchdog.event.queue_size | 2048 | Profile/部署 | 事件队列容量 |
| infra.watchdog.event.overflow_policy | block | Profile/部署 | block/overrun_oldest |
| infra.watchdog.recovery_hint.enabled | true | 默认/Profile | 是否发送恢复建议 |
| infra.watchdog.audit.required | true | 默认/Profile | 关键超时必须审计 |
| infra.watchdog.max_entities | 1024 | Profile/部署 | 监督实体上限 |
| infra.watchdog.safe_mode.scan_interval_ms | 2000 | 默认/Profile | 安全模式扫描周期 |

### 6.10 可观测性设计（日志/指标/追踪/审计）

日志点：
1. watchdog init/start/stop 生命周期。
2. 实体注册/注销/重复注册拒绝。
3. 心跳过期、超时升级、safe_mode 进入/退出。
4. 事件发布失败与 fallback 启用。

指标：
1. infra_watchdog_entities_total
2. infra_watchdog_heartbeat_ingest_total{entity_type}
3. infra_watchdog_timeout_total{level,entity_type}
4. infra_watchdog_consecutive_miss{entity_id}
5. infra_watchdog_scan_lag_ms
6. infra_watchdog_event_publish_fail_total
7. infra_watchdog_safe_mode_total

追踪：
1. 每次扫描周期创建 watchdog.scan span。
2. timeout 决策附加 trace_id/span_id，支持与 runtime 恢复链路关联。

审计：
1. critical/fatal 级 timeout 必须写审计。
2. 审计字段至少包含 actor(system/watchdog)、action(timeout_detected)、target(entity_id)、outcome(level)、evidence_ref。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 建立 watchdog 统一入口与生命周期 | 新增 WatchdogServiceFacade + IWatchdogService | 收敛监督能力到单入口，避免散落实现 | infra/include/IWatchdogService.h; infra/src/watchdog/WatchdogServiceFacade.cpp | unit: WatchdogServiceFacadeTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R WatchdogServiceFacadeTest | 依赖 contracts ResultCode/ErrorInfo |
| 建立心跳注册与采集通道 | 新增 HeartbeatRegistry + HeartbeatIngestor | 保证实体管理与心跳接收解耦 | infra/src/watchdog/HeartbeatRegistry.cpp; infra/src/watchdog/HeartbeatIngestor.cpp | unit: HeartbeatRegistryTest; unit: HeartbeatIngestorTest | ctest --test-dir build-ci -R "HeartbeatRegistryTest|HeartbeatIngestorTest" | 阻塞：时钟抽象接口待 platform 对齐 |
| 建立 deadline 扫描与超时判定 | 新增 DeadlineWheel + TimeoutPolicyEngine | 形成可配置超时判定路径 | infra/include/ITimeoutPolicy.h; infra/src/watchdog/DeadlineWheel.cpp; infra/src/watchdog/TimeoutPolicyEngine.cpp | unit: DeadlineWheelTest; unit: TimeoutPolicyTest | ctest --test-dir build-ci -R "DeadlineWheelTest|TimeoutPolicyTest" | 依赖 profile 配置键冻结 |
| 建立超时事件与审计闭环 | 新增 TimeoutEventPublisher + WatchdogAuditBridge | 满足“超时事件/审计事件”双输出约束 | infra/src/watchdog/TimeoutEventPublisher.cpp; infra/src/watchdog/WatchdogAuditBridge.cpp | integration: WatchdogTimeoutFlowIntegrationTest | ctest --test-dir build-ci -R WatchdogTimeoutFlowIntegrationTest | 阻塞：事件总线最小接口待统一 |
| 建立恢复建议接口（不执行） | 新增 RecoveryRequestEmitter | 保持与 ADR-007 一致的建议-执行分离 | infra/src/watchdog/RecoveryRequestEmitter.cpp | contract: WatchdogRecoveryBoundaryContractTest | ctest --test-dir build-ci -R WatchdogRecoveryBoundaryContractTest | 依赖 runtime 恢复请求最小结构冻结 |
| 建立 watchdog 测试与 Gate 基线 | 新增 unit/integration/failure 测试目录与标签 | 把设计约束转化为自动化门禁 | tests/unit/infra/watchdog/*; tests/integration/infra/watchdog/*; tests/contract/infra/* | unit/contract/integration/failure injection | ctest --test-dir build-ci -L "watchdog|infra" | 依赖测试标签标准化 |

不可立即映射项：
1. 外部 supervisor 协同（systemd/k8s sidecar）属于部署演进项，当前不纳入最小交付。
2. 跨进程 watchdog 聚合总线需等待统一事件中台接口冻结。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/
2. infra/src/watchdog/
3. tests/unit/infra/watchdog/
4. tests/integration/infra/watchdog/
5. tests/contract/infra/

建议文件：
1. infra/include/IWatchdogService.h
2. infra/include/IHeartbeatSource.h
3. infra/include/ITimeoutPolicy.h
4. infra/src/watchdog/WatchdogServiceFacade.cpp
5. infra/src/watchdog/HeartbeatRegistry.cpp
6. infra/src/watchdog/HeartbeatIngestor.cpp
7. infra/src/watchdog/DeadlineWheel.cpp
8. infra/src/watchdog/TimeoutPolicyEngine.cpp
9. infra/src/watchdog/TimeoutEventPublisher.cpp
10. infra/src/watchdog/WatchdogAuditBridge.cpp
11. infra/src/watchdog/RecoveryRequestEmitter.cpp
12. infra/src/watchdog/WatchdogHealthProbe.cpp
13. tests/unit/infra/watchdog/*Test.cpp
14. tests/integration/infra/watchdog/*Test.cpp

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| WDG-M1 接口冻结 | Not Started | 新增并冻结 IWatchdogService/IHeartbeatSource/ITimeoutPolicy | Blueprint 3.12/4.3 + 工程规范 3.7 | infra/include/*.h | unit: WatchdogInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 头文件可编译、无实现耦合 |
| WDG-M2 监督核心闭环 | Not Started | 新增 Registry/Ingestor/DeadlineWheel/Policy | 架构 11.1 + 基础设施设计 6.3 | infra/src/watchdog/Core*.cpp | unit: Registry/Ingestor/Policy | ctest --test-dir build-ci -R "HeartbeatRegistryTest|TimeoutPolicyTest" | 支持注册、心跳、超时判定 |
| WDG-M3 事件审计闭环 | Not Started | 新增 TimeoutEventPublisher/AuditBridge 并接 metrics | 架构 5.10 + 工程规范 3.6 | infra/src/watchdog/Event*.cpp | integration: WatchdogTimeoutFlowIntegrationTest | ctest --test-dir build-ci -R WatchdogTimeoutFlowIntegrationTest | 超时事件可观测且可审计 |
| WDG-M4 恢复建议闭环 | Not Started | 新增 RecoveryRequestEmitter 并与 runtime 接口对齐 | ADR-007 + contracts 冻结策略 | infra/src/watchdog/RecoveryRequestEmitter.cpp | contract: WatchdogRecoveryBoundaryContractTest | ctest --test-dir build-ci -R WatchdogRecoveryBoundaryContractTest | 仅输出建议、不执行恢复 |
| WDG-M5 Gate 接入 | Not Started | 补齐 watchdog 标签、CI 脚本与失败注入用例 | 工程规范 3.7 + TODO Gate | tests/* + scripts/ci/* | unit/contract/integration/failure | ctest --test-dir build-ci -L "watchdog" | Gate 全绿且可重复执行 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| WDG-T001 | Not Started | 新增 IWatchdogService 接口并接入 infra 构建 | Blueprint 4.3 + 工程规范 | infra/include/IWatchdogService.h; infra/CMakeLists.txt | WatchdogInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 构建通过且可链接 |
| WDG-T002 | Not Started | 新增 HeartbeatRegistry 并实现注册/去重/注销 | 架构 11.1 | infra/src/watchdog/HeartbeatRegistry.cpp | HeartbeatRegistryTest | ctest --test-dir build-ci -R HeartbeatRegistryTest | 重复注册可拒绝并返回错误码 |
| WDG-T003 | Not Started | 新增 HeartbeatIngestor 并实现乱序样本处理 | 行业实践 + 工程规范 3.6 | infra/src/watchdog/HeartbeatIngestor.cpp | HeartbeatIngestorTest | ctest --test-dir build-ci -R HeartbeatIngestorTest | 过旧样本被识别且可观测 |
| WDG-T004 | Not Started | 新增 DeadlineWheel 与 TimeoutPolicyEngine | 架构 5.10 + infra 设计约束 | infra/src/watchdog/DeadlineWheel.cpp; infra/src/watchdog/TimeoutPolicyEngine.cpp | DeadlineWheelTest; TimeoutPolicyTest | ctest --test-dir build-ci -R "DeadlineWheelTest|TimeoutPolicyTest" | 超时等级判定符合策略 |
| WDG-T005 | Not Started | 新增 TimeoutEventPublisher 与 WatchdogAuditBridge | 架构 8.7 + 工程规范 3.6 | infra/src/watchdog/TimeoutEventPublisher.cpp; infra/src/watchdog/WatchdogAuditBridge.cpp | WatchdogTimeoutFlowIntegrationTest | ctest --test-dir build-ci -R WatchdogTimeoutFlowIntegrationTest | 超时事件和审计事件均可观测 |
| WDG-T006 | Not Started | 新增 RecoveryRequestEmitter 并限制为建议语义 | ADR-007 | infra/src/watchdog/RecoveryRequestEmitter.cpp | WatchdogRecoveryBoundaryContractTest | ctest --test-dir build-ci -R WatchdogRecoveryBoundaryContractTest | 无恢复执行副作用 |
| WDG-T007 | Not Started | 新增 watchdog 失败注入测试 | 工程规范 3.7 | tests/integration/infra/watchdog/WatchdogFailureInjectionTest.cpp | WatchdogFailureInjectionTest | ctest --test-dir build-ci -R WatchdogFailureInjectionTest | 至少覆盖3类故障注入 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | Registry/Ingestor/DeadlineWheel/Policy | 重复注册、心跳乱序、deadline 到期、等级升级 | 断言全通过，错误码稳定 |
| Contract | watchdog 与 contracts 边界 | ResultCode/ErrorInfo 映射、RecoveryHint 不含执行字段 | 无越权字段，兼容性检查通过 |
| Integration | watchdog + health/logging/runtime | 超时检测、事件发布、审计落盘、快照查询 | 关键链路可重复执行 |
| Failure Injection | 事件总线故障、审计写失败、扫描线程滞后、心跳风暴 | fallback、生效告警、safe_mode 触发 | 每类故障有证据与兜底动作 |
| Compatibility | profile 差异行为 | desktop_full/edge_balanced/edge_minimal 参数行为一致性 | 无 breaking 回归 |

### 9.2 质量 Gate 建议清单

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| WDG-G1 | watchdog 单元测试全绿 | 任一 unit 失败即阻断 |
| WDG-G2 | watchdog 集成测试全绿 | 任一 integration 失败即阻断 |
| WDG-G3 | watchdog 失败注入关键用例全绿 | 任一故障路径缺兜底动作即阻断 |
| WDG-G4 | contracts 边界检查通过 | 出现越权字段或语义漂移即阻断 |
| WDG-G5 | profile 兼容检查通过 | 任一 profile 行为不一致即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/apps/infra 监督状态消费者 | 新增接口 + 默认实现兼容占位逻辑，调用方逐步迁移到 IWatchdogService | 先 desktop_full，后 edge_balanced，最后 edge_minimal | 预留多租户实体分组、远程事件导出 |
| Medium（接口签名变更时） | 所有 watchdog 消费者与测试用例 | v1/v2 接口并存 + 适配器过渡，迁移窗口内双写事件 | 先双写指标与事件，稳定后切换 | 预留跨进程 supervisor 桥接 |

演进原则：
1. 默认向后兼容，新增字段优先 optional。
2. breaking 变更必须先过 ADR/评审并提供迁移说明。
3. profile 维度先灰度后全量。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-WDG-01 时钟/线程抽象未冻结 | WDG-T003/T004，WDG-M2 | platform 提供稳定 monotonic clock + scheduler 抽象 | 先以单线程定时循环打通最小闭环 | 关闭并发扫描，仅保留 evaluate_now |
| B-WDG-02 事件总线接口未统一 | WDG-T005，WDG-M3 | 统一 TimeoutEvent 发布接口签名 | 先落日志+审计并缓存事件 | 延后总线发布到下一迭代 |
| B-WDG-03 runtime 恢复建议输入结构未冻结 | WDG-T006，WDG-M4 | RecoveryHintRequest 字段冻结 | 先输出最小建议字段（reason/target/evidence） | 暂停建议发送，仅保留 timeout 证据输出 |
| B-WDG-04 profile 配置键命名未冻结 | WDG-T004，WDG-M2 | profiles 下 watchdog 配置键冻结 | 先支持默认值+部署层覆盖 | 暂停运行时覆盖 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 越权执行风险 | High | watchdog 直接触发恢复执行 | 代码层面禁止执行接口；contract 测试校验仅建议语义 |
| 误报风暴 | Medium | 抖动期间大量瞬时迟到被判定超时 | 引入 grace + 连续失败阈值 + 去重发布 |
| 观测丢失 | High | 事件总线/审计写失败且无 fallback | 强制 fallback ring-buffer + fail 指标 + 审计兜底 |
| 资源超限 | Medium | edge_minimal 实体数与扫描频率配置过高 | 按 profile 限流并做压力测试 |

### 11.3 回退策略

1. 功能回退：关闭 recovery_hint 与高频扫描，仅保留基础超时检测与日志审计。
2. 配置回退：回滚到默认 timeout/grace 策略，禁用运行时覆盖。
3. 发布回退：灰度失败时仅回退 watchdog 子模块，不影响其他 infra 子域编译产物。

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. WatchdogSnapshot 是否需要跨重启持久化以支撑长期诊断。
2. entity_type 与 criticality 枚举是否在本轮冻结到统一头文件。
3. timeout_level 与 runtime RecoveryPolicy 的映射粒度是否需要标准化表。
4. watchdog 与 health 的边界是否在本轮以独立接口冻结，还是先由 facade 聚合适配。

### 12.2 后续任务

1. 在 docs/todos 新增 watchdog 专项 TODO，按 WDG-T001~WDG-T007 指派负责人和时间窗。
2. 优先推进 WDG-M1 与 WDG-M2，先替换占位实现并打通最小监督闭环。
3. 对接 tests/contract 增加 watchdog 边界守卫，阻断“建议-执行”越权回归。
4. 在 edge_balanced 与 edge_minimal 执行一轮心跳压力测试，校准默认扫描与阈值参数。