# DASALL infra/health 模块详细设计（Detailed Design）

版本：v1.0  
日期：2026-03-24  
阶段：Detailed Design  
适用模块：infra/health

## 1. 模块概览

### 1.1 模块定位

infra/health 属于 Infrastructure Layer（Layer 1），负责基础健康状态评估、探针编排、退化判定与健康事件输出，为 runtime 与上层模块提供可观测、可判定、可回放的健康信号。

模块目标：
1. 统一 liveness/readiness/degraded 三态评估语义。
2. 为 infra 子组件提供标准健康探针接入点，不侵入业务语义判断。
3. 为 runtime 主控与运维链路提供可消费的 HealthSnapshot 与事件流。

### 1.2 层级边界与依赖方向

上游调用方：apps、runtime、infra/ota、infra/config、infra/logging、infra/metrics、infra/secret、infra/tracing。  
同层协同：infra/watchdog（后续实现）、infra/logging、infra/metrics。  
下游依赖：platform 时间/线程抽象、contracts 错误语义。  
禁止方向：infra/health 不反向依赖 runtime/cognition/tools 等业务模块实现类。

### 1.3 来源依据

1. docs/architecture/DASSALL_Agent_architecture.md（3.4.7、3.7、5.10、8.7）
2. docs/architecture/DASALL_Engineering_Blueprint.md（3.12、4.1、4.2、4.3、7）
3. docs/adr/ADR-005-architecture-review-baseline.md
4. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
5. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
6. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
7. docs/plans/DASALL_contracts冻结实施计划.md
8. docs/todos/contracts/DASALL_contracts冻结TODO总表.md
9. docs/architecture/DASALL_infrastructure子系统详细设计.md（已给出 HealthMonitor 入口语义）
10. docs/development/DASALL_工程协作与编码规范.md
11. 代码现状：infra/CMakeLists.txt、infra/src/placeholder.cpp、infra/src/health/（空目录）
12. 行业参考：Azure Health Endpoint Monitoring pattern、Kubernetes probes、systemd watchdog 模式

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not 约束表

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| HLT-C001 | DASSALL_Agent_architecture.md 3.4.7/5.10/8.7 | Must | infra/health 必须提供健康检查能力并纳入基础设施统一治理 | 子组件/流程 |
| HLT-C002 | DASSALL_Agent_architecture.md 3.7 | Must | 仅允许上层依赖下层抽象，health 不反向依赖业务模块实现 | 依赖关系 |
| HLT-C003 | DASALL_Engineering_Blueprint.md 4.2 | Must-Not | infra/ -> 业务模块实现为禁止依赖方向 | include/CMake |
| HLT-C004 | DASALL_Engineering_Blueprint.md 4.3 | Must | 跨模块调用必须通过冻结 contracts 或抽象接口，不直连实现细节 | 接口语义 |
| HLT-C005 | ADR-005 | Must | 在 contracts 与主边界冻结前提下落地，不能反向改写架构结论 | 设计治理 |
| HLT-C006 | ADR-006 | Must-Not | health 不承担上下文装配或 Prompt 渲染语义，仅输出观测事实 | 职责边界 |
| HLT-C007 | ADR-007 | Must-Not | health 不拥有失败语义判定和恢复裁定权，仅提供健康证据与事件 | 异常流程 |
| HLT-C008 | ADR-008 | Must | health 只上报主控与协同链路健康，不拥有全局调度权 | 边界职责 |
| HLT-C009 | contracts 计划 5/6 + TODO 总表 | Must-Not | 不把探针线程模型、采样实现细节写入 contracts 对象 | contracts 对齐 |
| HLT-C010 | contracts 冻结策略 | Must | 新增字段默认 optional 与向后兼容，优先引用 ResultCode/ErrorInfo 语义 | 版本演进 |
| HLT-C011 | 工程规范 3.6 | Must | 错误不可吞没，探针失败必须可观测（日志/指标/审计） | 异常处理 |
| HLT-C012 | 工程规范 3.7 | Should | 新增公共接口同步新增 unit/contract/integration 测试 | 测试门禁 |
| HLT-C013 | DASSALL_Agent_architecture.md 8.6 + Blueprint 5.1 | Must | 健康检查配置应支持 Profile 裁剪和分层覆盖，不绕过审计链路 | 配置策略 |
| HLT-C014 | Azure Health pattern + Kubernetes probes | Should | liveness/readiness 分离，degraded 作为可运行但受限状态 | 状态模型 |
| HLT-C015 | systemd watchdog pattern | Should | 心跳超时应有显式阈值与升级路径，避免静默失活 | 探针与超时 |

### 2.2 约束抽取结论

Must：边界单向依赖、健康三态可判定、失败可观测、contracts 兼容优先。  
Should：分层探针模型、Profile 差异化参数、测试门禁与故障注入。  
Must-Not：不越权做恢复裁定、不污染 contracts、不侵入业务模块。

---

## 3. 现状与缺口

### 3.1 现状识别

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| health 模块可编译并承载能力 | 占位 | infra 仅编译 placeholder.cpp，health 未接入构建 | High | P0 |
| health 目录与实现文件 | 缺失 | infra/src/health/ 为空，缺少核心实现 | High | P0 |
| health 对外接口头文件 | 缺失 | infra/include 为空，未提供 IHealthMonitor/IHealthProbe | High | P0 |
| 健康状态对象与事件语义 | 缺失 | 无 HealthSnapshot/ProbeResult 定义与错误码映射 | High | P0 |
| liveness/readiness/degraded 流程 | 缺失 | 无周期评估、阈值判定、事件发布链路 | High | P0 |
| 与 logging/metrics 协同 | 缺失 | 无健康指标、无异常日志、无审计锚点 | Medium | P1 |
| 配置策略与 Profile 差异化 | 缺失 | 无探针周期/超时/阈值配置 | Medium | P1 |
| 测试基线 | 缺失 | 无 unit/integration/failure injection 覆盖 | High | P0 |

证据：
1. infra/CMakeLists.txt 仅包含 src/placeholder.cpp。
2. infra/src/placeholder.cpp 为 keep_library_non_empty 占位。
3. infra/src/health/ 当前为空目录。

### 3.2 现状-目标冲突

| 冲突类型 | 描述 | 影响 | 风险等级 |
|---|---|---|---|
| 边界冲突 | 若 health 直接拉取 runtime 内部状态机，会破坏层级单向依赖 | 违背蓝图与可替换性 | High |
| 语义重复 | 若 health 自定义 ErrorInfo/ResultCode 语义，会与 contracts 冲突 | 契约漂移与回归失败 | High |
| 依赖反转 | 若上层直接绑死健康实现类而非接口 | 难以 profile 裁剪与替换 | Medium |
| 恢复越权 | 若 health 直接执行重启/回滚决策 | 违反 ADR-007 主控裁定边界 | High |

---

## 4. 候选方案对比

### 4.1 候选方案概述

1. 方案 A：同步轮询健康检查（单线程顺序探测）。
2. 方案 B：分层探针引擎（Probe Registry + Scheduler + Evaluator + EventPublisher）。
3. 方案 C：外部健康代理优先（Collector/sidecar 拉取，模块仅上报原始心跳）。

### 4.2 对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| A 同步轮询 | 中 | 中 | 低 | 高延迟探针阻塞、无法细粒度分组与降级 | 淘汰：仅适合 PoC |
| B 分层探针引擎 | 高 | 高 | 中 | 组件较多，需要清晰接口治理与测试 | 保留并采纳 |
| C 外部代理优先 | 中 | 中 | 高 | 引入部署耦合，对 edge_minimal 不友好 | 暂不采纳，列为 v2 |

### 4.3 行业方案匹配结论

1. Azure Health Endpoint 模式强调 liveness/readiness 分离，适配 DASALL 健康三态需求。
2. Kubernetes probes 模型适合将“可存活”和“可接流量”解耦，避免单一健康位误判。
3. systemd watchdog 强调心跳超时升级路径，可映射为 infra/watchdog 与 health 事件联动。

---

## 5. 决策结论

### 5.1 最终选型

采纳方案 B：分层探针引擎。

### 5.2 放弃其他方案理由

1. 方案 A：简单但不可扩展，探针超时会拖垮全局评估周期。
2. 方案 C：当前阶段工程成本与部署依赖过高，不符合最小可交付目标。

### 5.3 与架构/ADR/contracts 一致性说明

1. 架构一致：health 位于 infra，仅提供基础治理与观测，不接管业务决策。
2. ADR 一致：
   - 不接管上下文装配（ADR-006）。
   - 不做恢复裁定，仅输出证据（ADR-007）。
   - 不拥有调度主权，仅服务主控链路（ADR-008）。
3. contracts 一致：消费 ResultCode/ErrorInfo 语义，健康实现细节留在 infra 私有域。

---

## 6. 详细设计

### 6.1 职责边界

infra/health 职责：
1. 注册并调度子组件健康探针。
2. 生成 HealthSnapshot（liveness/readiness/degraded）与失败组件列表。
3. 发布健康事件并输出指标、日志、审计锚点。
4. 提供查询接口给 runtime/ops。

infra/health 非职责：
1. 不执行重试、回滚、熔断等恢复动作。
2. 不直接改写 runtime 主状态机。
3. 不定义跨模块共享语义对象的实现细节。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| HealthMonitorFacade | 对外统一入口，管理生命周期与对外查询 |
| ProbeRegistry | 管理探针注册、去重、分组和元信息 |
| ProbeScheduler | 周期调度探针执行，支持分组周期与超时 |
| ProbeExecutor | 执行探针并收集 ProbeResult，隔离执行异常 |
| HealthEvaluator | 基于策略聚合结果，输出三态快照 |
| HealthStateStore | 维护最近 N 次评估窗口与状态转移 |
| HealthEventPublisher | 发布状态变化事件到事件总线/日志审计 |
| HealthMetricsBridge | 输出健康指标（探针时延、失败计数、退化计数） |
| RecoveryHintEmitter | 仅输出恢复建议事件，不执行恢复动作 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| HealthMonitorFacade | Runtime/Infra 查询请求、初始化配置 | HealthSnapshot/ResultCode | 查询失败必须返回明确错误码 |
| ProbeRegistry | register_probe(name, group, criticality) | ProbeDescriptor 集合 | 同名探针不可重复注册 |
| ProbeScheduler | 周期配置、探针列表 | 调度任务 | 支持 liveness/readiness 不同周期 |
| ProbeExecutor | ProbeDescriptor | ProbeResult | 超时/异常都要结构化返回 |
| HealthEvaluator | ProbeResult 窗口、策略阈值 | HealthSnapshot | 输出 liveness/readiness/degraded |
| HealthStateStore | 新快照与转移事件 | 当前状态、历史状态 | 保留 last_transition_ts |
| HealthEventPublisher | 状态变化、失败详情 | event bus/logging/audit | 关键变化必须发布事件 |
| HealthMetricsBridge | ProbeResult、状态转移 | metrics exporter | 指标名受白名单治理 |
| RecoveryHintEmitter | Degraded/Failed 快照 | 建议事件（RecoveryHint） | 仅建议，不下发执行命令 |

### 6.4 子组件依赖关系

1. HealthMonitorFacade -> ProbeRegistry、ProbeScheduler、HealthEvaluator、HealthStateStore。
2. ProbeScheduler -> ProbeExecutor。
3. ProbeExecutor -> IHealthProbe（由 logging/metrics/config/secret/ota 等实现）。
4. HealthEvaluator -> HealthStateStore -> HealthEventPublisher/HealthMetricsBridge。
5. HealthEvaluator -> RecoveryHintEmitter（建议输出）。

依赖约束：
1. 仅依赖 IHealthProbe 抽象，不依赖探针实现内部结构。
2. 不依赖 runtime/cognition 具体类。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | contracts 对齐关系 |
|---|---|---|---|
| ProbeDescriptor | probe_name, group, criticality, interval_ms, timeout_ms | criticality 取值固定且可扩展 | 不写入 contracts，infra 私有 |
| ProbeResult | probe_name, status, latency_ms, error_code, detail_ref, ts | status 必含 Unknown/Healthy/Degraded/Unhealthy | error_code 映射 ResultCode/ErrorInfo |
| HealthSnapshot | liveness, readiness, degraded, failed_components, version, ts | version 单调递增，状态可回放 | 仅消费 contracts 错误语义，不新增共享字段 |
| HealthTransition | from_state, to_state, reason, trigger_probe, ts | 只在状态变化时生成 | 事件包装遵循 EventEnvelope 语义 |
| RecoveryHint | reason_code, severity, suggested_action, evidence_ref | 仅建议，不包含执行句柄 | 对齐 ADR-007：建议与执行分离 |

### 6.6 核心接口语义定义

建议头文件分布：infra/include/

1. IHealthProbe
   - probe(): ProbeResult
   - 语义：无副作用健康探测；超时需返回结构化失败。

2. IHealthMonitor
   - register_probe(name, group, probe): 注册探针。
   - evaluate_now(): 立即评估并返回 HealthSnapshot。
   - get_snapshot(): 返回最近快照。
   - subscribe(listener): 订阅状态变化。

3. IHealthPolicy
   - evaluate(results): HealthSnapshot
   - 语义：将结果与阈值策略解耦，支持 profile 替换。

前置条件：
1. init 完成且配置已加载。
2. 注册探针满足唯一性约束。

后置条件：
1. 每轮评估要么返回快照，要么返回明确错误码。
2. 状态变化必须产生事件与指标。

错误语义（infra 私有错误码域，映射 contracts::ResultCode）：
1. INF_E_HEALTH_PROBE_TIMEOUT
2. INF_E_HEALTH_PROBE_EXCEPTION
3. INF_E_HEALTH_PROBE_NOT_FOUND
4. INF_E_HEALTH_POLICY_INVALID
5. INF_E_HEALTH_EVENT_PUBLISH_FAIL

### 6.7 主流程时序（正常）

1. InfraServiceFacade 初始化 health 子系统并加载配置。
2. 各子组件注册 IHealthProbe。
3. ProbeScheduler 按分组周期触发 ProbeExecutor。
4. ProbeExecutor 采集 ProbeResult 写入窗口。
5. HealthEvaluator 基于策略计算 HealthSnapshot。
6. HealthStateStore 更新状态版本。
7. HealthEventPublisher 仅在状态变化时发布事件。
8. runtime/apps 调用 get_snapshot 获取当前健康状态。

### 6.8 异常与恢复时序

异常分类：
1. 探针超时：单次超时，记为 Degraded 候选。
2. 探针持续失败：连续 N 次失败，判定 Unhealthy。
3. 事件发布失败：快照生成成功但事件总线失败。
4. 策略配置非法：阈值冲突或缺失。

恢复动作：
1. 探针超时：记录超时指标并触发降级事件。
2. 持续失败：标记 failed_components，输出 RecoveryHint。
3. 事件发布失败：重试发布并记录 audit/log 指标。
4. 策略非法：快速失败并返回 INF_E_HEALTH_POLICY_INVALID。

兜底策略：
1. 若调度线程故障，health 进入 safe_observe_mode：保留最近快照并拒绝新评估请求。
2. 若非关键探针失败，系统可 degraded 运行；关键探针失败则 readiness=false。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| infra.health.enabled | true | 默认/Profile/部署 | 是否启用健康子系统 |
| infra.health.liveness.interval_ms | 2000 | Profile/部署 | liveness 探测周期 |
| infra.health.readiness.interval_ms | 5000 | Profile/部署 | readiness 探测周期 |
| infra.health.probe.timeout_ms | 1000 | Profile/部署 | 单探针超时 |
| infra.health.degraded.threshold | 1 | Profile/部署 | 单窗口失败阈值 |
| infra.health.unhealthy.consecutive_failures | 3 | Profile/部署 | 连续失败进入 Unhealthy |
| infra.health.history.window_size | 20 | 默认/Profile | 状态窗口长度 |
| infra.health.event_on_transition_only | true | 默认/Profile | 仅状态变化时发事件 |
| infra.health.recovery_hint.enabled | true | Profile/部署 | 是否发布恢复建议 |
| infra.health.probe.groups.critical | logging,secret,config | Profile/部署 | 关键探针分组 |

### 6.10 可观测性设计

日志点：
1. 模块 init/start/stop。
2. 探针注册、注销、重复注册拒绝。
3. 探针超时、异常、恢复。
4. 状态转移事件（Healthy->Degraded->Unhealthy）。

指标：
1. infra_health_probe_total{probe,status}
2. infra_health_probe_latency_ms{probe}
3. infra_health_snapshot_state{state}
4. infra_health_transition_total{from,to}
5. infra_health_degraded_total
6. infra_health_event_publish_fail_total

追踪：
1. evaluate_now 创建 span，记录 policy_version、probe_count。
2. 对超时探针附加 trace attributes：probe_name、timeout_ms。

审计：
1. 关键状态变化（readiness=true->false、false->true）写审计记录。
2. recovery_hint 仅记录建议与证据，不记录执行动作。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 引入健康统一入口与生命周期 | 新增 HealthMonitorFacade + IHealthMonitor | 将健康能力收敛为单入口，避免散落调用 | infra/include/IHealthMonitor.h; infra/src/health/HealthMonitorFacade.cpp | unit: HealthMonitorFacadeTest | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R HealthMonitorFacadeTest | 依赖 contracts ResultCode/ErrorInfo |
| 探针注册与执行解耦 | 新增 IHealthProbe + ProbeRegistry + ProbeExecutor | 降低探针实现耦合并支持分组执行 | infra/include/IHealthProbe.h; infra/src/health/ProbeRegistry.cpp; infra/src/health/ProbeExecutor.cpp | unit: ProbeRegistryTest; unit: ProbeExecutorTest | ctest --test-dir build-ci -R "ProbeRegistryTest|ProbeExecutorTest" | 阻塞：线程/定时抽象策略待 platform 对齐 |
| 三态评估策略可替换 | 新增 IHealthPolicy + HealthEvaluator | 支持 profile 差异阈值并保持可演进 | infra/include/IHealthPolicy.h; infra/src/health/HealthEvaluator.cpp | unit: HealthPolicyEvaluateTest | ctest --test-dir build-ci -R HealthPolicyEvaluateTest | 依赖 profiles 健康参数键命名冻结 |
| 健康状态转移事件闭环 | 新增 HealthStateStore + HealthEventPublisher | 保证状态变化可回放可审计 | infra/src/health/HealthStateStore.cpp; infra/src/health/HealthEventPublisher.cpp | integration: HealthTransitionIntegrationTest | ctest --test-dir build-ci -R HealthTransitionIntegrationTest | 阻塞：事件总线最小接口待统一 |
| 恢复建议与执行解耦 | 新增 RecoveryHintEmitter（只发建议） | 严格对齐 ADR-007 边界 | infra/src/health/RecoveryHintEmitter.cpp | contract: RecoveryHintBoundaryContractTest | ctest --test-dir build-ci -R RecoveryHintBoundaryContractTest | 依赖 ADR 边界用例模板 |
| 建立 health 测试门禁 | 新增 tests/unit/infra/health + tests/integration/infra/health | 把设计约束映射到自动化 gate | tests/unit/infra/health/*; tests/integration/infra/health/*; tests/contract/infra/* | unit/contract/integration/failure injection | ctest --test-dir build-ci -L "infra|health" | 依赖测试标签标准化 |

不可立即映射项：
1. 外部 sidecar/collector 拉取模式：当前不纳入最小交付，作为 v2 演进项。
2. 跨进程远程健康聚合：受部署拓扑限制，先保留事件接口扩展点。

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

建议目录：
1. infra/include/
2. infra/src/health/
3. tests/unit/infra/health/
4. tests/integration/infra/health/
5. tests/contract/infra/

建议文件：
1. infra/include/IHealthProbe.h
2. infra/include/IHealthMonitor.h
3. infra/include/IHealthPolicy.h
4. infra/src/health/HealthMonitorFacade.cpp
5. infra/src/health/ProbeRegistry.cpp
6. infra/src/health/ProbeScheduler.cpp
7. infra/src/health/ProbeExecutor.cpp
8. infra/src/health/HealthEvaluator.cpp
9. infra/src/health/HealthStateStore.cpp
10. infra/src/health/HealthEventPublisher.cpp
11. infra/src/health/RecoveryHintEmitter.cpp
12. tests/unit/infra/health/*Test.cpp
13. tests/integration/infra/health/*Test.cpp

### 8.2 分阶段实施与完成判定

| 阶段 | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| HLT-M1 接口冻结 | Not Started | 新增并冻结 IHealthProbe/IHealthMonitor/IHealthPolicy | Blueprint 3.12/4.3 + 工程规范 | infra/include/*.h | unit: HealthInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口可编译且无实现耦合 |
| HLT-M2 探针执行闭环 | Not Started | 新增 ProbeRegistry/Scheduler/Executor | 架构 8.7 + 行业探针模式 | infra/src/health/Probe*.cpp | unit: ProbeRegistryTest/ProbeExecutorTest | ctest --test-dir build-ci -R "ProbeRegistryTest|ProbeExecutorTest" | 支持注册、调度、超时返回 |
| HLT-M3 状态评估闭环 | Not Started | 新增 HealthEvaluator/StateStore/EventPublisher | 基础设施详细设计 6.3 + ADR-007 | infra/src/health/Health*.cpp | unit + integration | ctest --test-dir build-ci -R "HealthPolicyEvaluateTest|HealthTransitionIntegrationTest" | 三态状态可稳定转移 |
| HLT-M4 恢复建议闭环 | Not Started | 新增 RecoveryHintEmitter 并接入审计锚点 | ADR-007 + 工程规范 3.6 | infra/src/health/RecoveryHintEmitter.cpp | contract: RecoveryHintBoundaryContractTest | ctest --test-dir build-ci -R RecoveryHintBoundaryContractTest | 建议与执行严格分离 |
| HLT-M5 测试 Gate 接入 | Not Started | 补齐 health 相关测试标签与 CI 门禁 | 工程规范 3.7 + contracts gate 实践 | tests/* + scripts/ci/* | unit/contract/integration/failure | ctest --test-dir build-ci -L "health" | Gate 全绿且可重复 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| HLT-T001 | Not Started | 新增 IHealthProbe 接口并定义 ProbeResult | 架构 8.7 + Blueprint 4.3 | infra/include/IHealthProbe.h | HealthInterfaceCompileTest | cmake --build build-ci --target dasall_infra | 接口可独立编译 |
| HLT-T002 | Not Started | 新增 IHealthMonitor 与 HealthMonitorFacade 骨架 | 基础设施详细设计 6.2/6.3 | infra/include/IHealthMonitor.h; infra/src/health/HealthMonitorFacade.cpp | HealthMonitorFacadeTest | ctest --test-dir build-ci -R HealthMonitorFacadeTest | 支持注册与查询快照 |
| HLT-T003 | Not Started | 新增 ProbeRegistry/ProbeExecutor 最小实现 | 行业探针模式 + 工程规范 | infra/src/health/ProbeRegistry.cpp; infra/src/health/ProbeExecutor.cpp | ProbeRegistryTest; ProbeExecutorTest | ctest --test-dir build-ci -R "ProbeRegistryTest|ProbeExecutorTest" | 超时与异常可结构化返回 |
| HLT-T004 | Not Started | 新增 IHealthPolicy 与 HealthEvaluator 规则 | 架构 8.7 + Profile 约束 | infra/include/IHealthPolicy.h; infra/src/health/HealthEvaluator.cpp | HealthPolicyEvaluateTest | ctest --test-dir build-ci -R HealthPolicyEvaluateTest | liveness/readiness/degraded 判定正确 |
| HLT-T005 | Not Started | 新增 HealthStateStore 与状态转移事件 | 架构 5.10 + 工程规范 3.6 | infra/src/health/HealthStateStore.cpp; infra/src/health/HealthEventPublisher.cpp | HealthTransitionIntegrationTest | ctest --test-dir build-ci -R HealthTransitionIntegrationTest | 状态变化事件可观测 |
| HLT-T006 | Not Started | 新增 RecoveryHintEmitter 并对齐 ADR-007 | ADR-007 | infra/src/health/RecoveryHintEmitter.cpp | RecoveryHintBoundaryContractTest | ctest --test-dir build-ci -R RecoveryHintBoundaryContractTest | 不触发执行动作，仅发布建议 |
| HLT-T007 | Not Started | 新增健康故障注入测试 | 工程规范 3.7 | tests/integration/infra/health/HealthFailureInjectionTest.cpp | HealthFailureInjectionTest | ctest --test-dir build-ci -R HealthFailureInjectionTest | 覆盖超时/异常/发布失败三类故障 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层 | 覆盖对象 | 关键用例 | 通过标准 |
|---|---|---|---|
| Unit | ProbeRegistry/Executor/Evaluator/StateStore | 重复注册拒绝、超时处理、阈值判定、状态转移 | 断言全通过，错误码稳定 |
| Contract | health 与 contracts 边界 | ErrorInfo/ResultCode 映射、RecoveryHint 不含执行字段 | 无越权字段、兼容性检查通过 |
| Integration | health 与 logging/metrics/runtime 装配 | 周期评估、状态变化事件、快照查询 | 关键链路可重复执行 |
| Failure Injection | 超时、异常、事件发布失败、策略配置非法 | degraded 触发、兜底快照、告警计数 | 每类故障均有可观测证据 |
| Compatibility | Profile 差异参数行为 | desktop_full/edge_balanced/edge_minimal 阈值行为 | 不出现 breaking 行为 |

### 9.2 质量 Gate 建议

| Gate ID | 检查项 | 失败判定 |
|---|---|---|
| HLT-G1 | health 单元测试全绿 | 任一 unit 失败即阻断 |
| HLT-G2 | health 集成测试全绿 | 任一 integration 失败即阻断 |
| HLT-G3 | 健康故障注入关键用例全绿 | 任一故障无兜底动作即阻断 |
| HLT-G4 | contracts 边界检查通过 | 出现越权字段或语义漂移即阻断 |
| HLT-G5 | Profile 兼容检查通过 | 任一 profile 行为不一致即阻断 |

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | runtime/apps/tools 等健康消费者 | 新增接口 + 默认实现兼容旧占位，调用方逐步迁移到 IHealthMonitor | 先 desktop_full 灰度，再 edge_balanced，最后 edge_minimal | 预留远程聚合 probe、自定义策略插件 |
| Medium（仅当接口签名变更） | 全部健康调用方与测试 | v1/v2 接口并存 + 适配器过渡 | 双写快照事件，稳定后切换 | 预留跨进程健康事件桥接 |

演进原则：
1. 默认向后兼容，新增字段优先 optional。
2. breaking 变更先走 ADR/评审与迁移窗口。
3. Profile 先灰度后全量。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| B-HLT-01 线程与定时抽象未统一 | HLT-T003/HLT-M2 | platform 定时与线程抽象接口冻结 | 先用单线程模拟调度打通流程与测试 | 暂时禁用并发调度，仅保留同步 evaluate_now |
| B-HLT-02 事件总线接口未冻结 | HLT-T005/HLT-M3 | EventEnvelope 发布接口冻结 | 先输出日志+指标并缓存状态转移 | 延后事件总线发布到下一迭代 |
| B-HLT-03 Profile 健康参数命名未冻结 | HLT-T004/HLT-M3 | profiles 下 infra.health 键命名确定 | 先支持默认值 + 部署层覆盖 | 暂停运行时覆盖，仅保留静态配置 |
| B-HLT-04 contracts 边界用例模板缺失 | HLT-T006/HLT-M4 | contract 测试模板可复用 | 先以最小 schema 断言覆盖关键字段 | 延后严格契约用例到 HLT-M5 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 缓解动作 |
|---|---|---|---|
| 边界越权 | High | health 直接触发恢复执行 | 在接口与测试中强制区分 RecoveryHint 与 RecoveryAction |
| 探针雪崩 | High | 大量探针超时导致调度阻塞 | 分组周期 + 超时隔离 + 并发窗口限制 |
| 状态抖动 | Medium | 阈值过小导致 Healthy/Degraded 频繁切换 | 引入 hysteresis 与连续失败计数 |
| 观测缺失 | High | 探针失败未记录指标/日志 | 强制失败路径打点并纳入 failure injection gate |

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. HealthEventPublisher 最终事件通道采用统一 event bus 还是先内联 logging/metrics。
2. criticality 分级策略是否按模块固定，还是允许 profile 动态重写。
3. HealthSnapshot 历史窗口是否需要持久化到磁盘以支持重启后诊断。
4. Watchdog 与 health 的边界是否在本轮即冻结（建议先保留松耦合）。

### 12.2 后续任务建议

1. 在 docs/todos 下新增 infra/health 专项 TODO，按 HLT-T001~HLT-T007 指派责任人与时间窗。
2. 优先推进 HLT-M1 与 HLT-M2，先替换 infra placeholder 并打通最小评估闭环。
3. 同步补齐 tests/contract 的健康边界守卫用例，阻断越权字段回归。
4. 在 edge_balanced 与 edge_minimal 做探针参数压测，校准默认超时与窗口阈值。
