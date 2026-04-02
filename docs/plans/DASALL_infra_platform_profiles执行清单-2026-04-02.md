# DASALL infra/platform/profiles 执行清单（2026-04-02）

最近更新时间：2026-04-02  
输出位置：docs/plans/  
适用范围：platform、profiles、infrastructure 子系统及其组件专项 TODO

## 1. 编排规则

1. 本清单只重排现有原子任务和 Block 任务，不新增、不删除、不改写源 TODO 的三件套。
2. 状态统一为三类：`done`、`not_started`、`blocked`。
3. 源文档中的 Done、Completed、Done（含日期）统一折算为 `done`。
4. 若出现“上游已完成、下游未回链”的情况，仍保持源 TODO 的 `blocked` 状态，不擅自解阻。
5. 每个任务的代码目标、测试目标、验收命令，仍以对应源 TODO 文档为准；本清单只负责顺序、阶段、泳道、关键路径和阻塞台账。
6. 已完成任务必须保留，不得从执行清单中移除；统一标记为 `done`。

## 2. 输入来源

1. docs/todos/platform/DASALL_platform_linux组件专项TODO.md
2. docs/todos/profiles/DASALL_profiles子系统专项TODO.md
3. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
4. docs/todos/infrastructure/DASALL_infrastructure评审后TODO落地实施步骤指引-2026-03-26.md
5. docs/todos/infrastructure/DASALL_infrastructure_audit组件专项TODO.md
6. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
7. docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md
8. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
9. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
10. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
11. docs/todos/infrastructure/DASALL_infrastructure_ota组件专项TODO.md
12. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_policy组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_watchdog组件专项TODO.md

## 3. 当前基线结论

1. Phase 0 全局解阻已完成：integration 顶层接线、命名统一、并发策略 SSOT、统一 gate 脚本均已落地。
2. platform_linux 全量任务已完成，可作为后续 health/watchdog/plugin 的已落地前置能力。
3. profiles 全量任务已完成，可作为 config、metrics、ota、watchdog 的已落地前置能力。
4. infrastructure 子系统基础对象、基础接口、CMake 入口、unit/contract 注册入口已完成。
5. 当前剩余工作的主体已经从“接口/对象冻结”转为“组件主链骨架、桥接实现、integration/failure/profile 收口”。
6. infra 子系统仍保留一个横切 contract 补丁任务：INF-TODO-020。

## 4. 推荐执行顺序

### 4.1 Phase 0 与 Phase 1 已完成基线

| 顺序 | Phase | Lane | 任务组 | 任务 ID | 当前状态 | 说明 |
|---|---|---|---|---|---|---|
| 1 | Phase 0 | Global | 全局解阻与 gate | INF-PLAT-INT-001, INF-NAME-001, INF-CONCUR-001, INF-GATE-UNIFY-001 | done | 顶层 integration、命名、并发策略、统一 gate 已完成 |
| 2 | Phase 1 | Platform | platform_linux 全量基线 | PLAT-LNX-TODO-001~025 | done | platform/linux 全量完成，保持为后续组件前置基线 |
| 3 | Phase 1 | Profiles | profiles 全量基线 | PRF-TODO-001~021 | done | profiles 全量完成，保持为配置与策略基线 |
| 4 | Phase 1 | Infra Core | infrastructure 基础骨架 | INF-TODO-001~019, INF-TODO-021 | done | infra 对象、接口、CMake、unit/contract、gate 基线均已落地 |

### 4.2 Phase 2 主执行面

| 顺序 | Phase | Lane | 任务组 | 任务 ID | 当前状态 | 排序理由 |
|---|---|---|---|---|---|---|
| 5 | Phase 2A | Config | config 主链骨架与测试接线 | CFG-TODO-007~011, CFG-TODO-014~017 | not_started | config 阻塞最少，且是 logging、metrics、ota、policy 后续配置接入的基础 |
| 6 | Phase 2A | Logging | logging 主链骨架与测试接线 | LOG-TODO-006~011, LOG-TODO-014~016 | not_started | logging 是 audit、metrics、tracing、diagnostics 的公共观测出口 |
| 7 | Phase 2A | Audit | audit 主链骨架与测试接线 | AUD-TODO-008~011, AUD-TODO-016~019 | not_started | audit 是 policy、plugin、ota、diagnostics 的高风险动作证据出口 |
| 8 | Phase 2A | Secret | secret 核心骨架与测试接线 | SEC-TODO-006~010, SEC-TODO-012~017 | not_started | secret 直接影响 config secret 引用、ota 校验、plugin 信任链 |
| 9 | Phase 2A | Policy | policy 主链骨架与测试接线 | POL-TODO-010~018, POL-TODO-022 | not_started | diagnostics 与 plugin 的准入链路依赖 policy 主链 |
| 10 | Phase 2B | Health | health 可执行主链 | HLT-TODO-007, HLT-TODO-008, HLT-TODO-010, HLT-TODO-011, HLT-TODO-013, HLT-TODO-015~018 | not_started | health 是 watchdog 与 diagnostics 的输入事实源 |
| 11 | Phase 2B | Metrics | metrics 主链与测试接线 | MET-TODO-009~018, MET-TODO-020 | not_started | metrics 是 tracing、diagnostics、policy、audit 后续桥接的前置 |
| 12 | Phase 2B | Tracing | tracing 主链与测试接线 | TRC-TODO-008~014, TRC-TODO-016~018 | not_started | tracing 完成后再进入 ARC-01 contract 补丁和桥接收口 |
| 13 | Phase 2C | Diagnostics | diagnostics 可执行子集 | DIA-TODO-009, DIA-TODO-012, DIA-TODO-014~017, DIA-TODO-019, DIA-TODO-023~026 | not_started | 先推进 policy guard、executor、snapshot 主链，跳过当前 blocked 子任务 |
| 14 | Phase 2C | OTA | ota 可执行子集 | OTA-TODO-006~010, OTA-TODO-012~018, OTA-TODO-020~021 | not_started | 先做 precheck、verifier、install、rollback 骨架，boot confirm 仍 blocked |
| 15 | Phase 2C | Plugin | plugin 可执行子集 | PLG-TODO-005~013 | not_started | 先做 validation pipeline、audit adapter、CMake/test 接线与 failure/profile 测试 |
| 16 | Phase 2C | Watchdog | watchdog 可执行子集 | WDG-TODO-009~011, WDG-TODO-013, WDG-TODO-015~017, WDG-TODO-019~023 | not_started | 先做 facade、registry、ingestor、policy engine 与 bridge，跳过 blocked 子任务 |

### 4.3 Phase 3 横切收口面

| 顺序 | Phase | Lane | 任务组 | 任务 ID | 当前状态 | 排序理由 |
|---|---|---|---|---|---|---|
| 17 | Phase 3 | Contract/Gate | tracing/metrics ARC 补丁与 gate 绑定 | MET-TODO-021~022, TRC-TODO-020~021 | not_started | metrics/tracing 主链稳定后补 ARC contract 约束和 gate 绑定 |
| 18 | Phase 3 | Infra Core | infra 横切 ARC contract 收口 | INF-TODO-020 | not_started | 在 tracing/metrics contract 侧补 planning stage、预算与延迟观测断言 |
| 19 | Phase 3 | Global | 全组件 integration/failure/profile 与证据回写 | 各组件 *-TODO-017/018/019/020/021/022/023 等收口任务 | not_started | 统一执行 integration、failure、profile 和文档证据回写 |

## 5. Critical Path

1. INF-TODO-021 已完成，后续所有实现类任务均应受仓库级 Blocked-first gate 约束。
2. CFG-TODO-007~011 -> CFG-TODO-014~017：先打通 config 主链和 unit/contract/integration 入口。
3. LOG-TODO-006~011 -> AUD-TODO-008~011：先打通 logging，再补 audit 主写与 fallback 主链。
4. SEC-TODO-006~010、012~017 与 POL-TODO-010~018 并行推进：形成 secret/policy 两条基础治理链。
5. MET-TODO-009~018 -> TRC-TODO-008~018 -> MET-TODO-021~022、TRC-TODO-020~021 -> INF-TODO-020：完成可观测主链与 ARC contract/gate 收口。
6. DIA-TODO-009、012、014~017、019、023~026：在 policy、audit、metrics 基础能力具备后推进 diagnostics 主链。
7. OTA-TODO-006~010、012~018、020~021 与 PLG-TODO-005~013：在 secret/policy/audit 基础能力具备后推进。
8. WDG-TODO-009~011、013、015~017、019~023：在 health、policy、audit、metrics 具备后推进。

## 6. Parallel Lanes

| Lane | 组件 | 说明 |
|---|---|---|
| Lane A | config, logging, audit | 优先形成配置与观测通道，最早为其它组件解阻 |
| Lane B | secret, policy | 安全与治理基线，直接影响 config、plugin、ota、diagnostics |
| Lane C | health, watchdog, diagnostics | 以健康事实、超时治理、诊断导出为主的运行保障链 |
| Lane D | metrics, tracing | 可观测增强链，负责指标、span、contract/gate 横切收口 |
| Lane E | ota, plugin | 升级与扩展链，依赖前述治理、审计、配置、策略能力 |

## 7. Block 任务台账

以下任务在源 TODO 中明确为 `blocked`，本清单保持原状态不变。

| 任务 ID | 组件 | Blocker | 解阻条件 | 解阻后回到 |
|---|---|---|---|---|
| AUD-TODO-012 | audit | AUD-BLK-001：ExportQuery filter 语义未冻结 | 冻结时间窗、actor、action 三键最小过滤模型与越权边界 | Phase 3 audit 导出链 |
| AUD-TODO-013 | audit | AUD-BLK-002：RetentionOutcome 未冻结 | 补齐 retention 输出对象和清理/归档动作对象 | Phase 3 audit retention |
| AUD-TODO-014 | audit | AUD-BLK-003：AuditHealthStatus 未冻结 | 冻结 ready/degraded/unavailable 与最近失败原因字段 | Phase 3 audit health probe |
| AUD-TODO-015 | audit | AUD-BLK-004：metrics/health 桥接接口未冻结 | metrics 与 health 给出最小桥接接口、标签白名单、失败语义 | Phase 3 audit bridge |
| CFG-TODO-013 | config | CFG-BLK-001：事件总线最小抽象未冻结 | 冻结发布/订阅最小接口与命名空间过滤语义 | Phase 3 config publish |
| DIA-TODO-008 | diagnostics | DIA-BLK-002：CommandCatalog/ValidationResult 未定义 | 在详细设计中补齐目录对象与校验返回对象 | Phase 2C diagnostics registry |
| DIA-TODO-011 | diagnostics | DIA-BLK-002：CommandCatalog/ValidationResult 未定义 | 在详细设计中补齐目录对象与校验返回对象 | Phase 2C diagnostics registry |
| DIA-TODO-013 | diagnostics | DIA-BLK-002、DIA-BLK-003：注册表对象和 allowed_commands schema 未冻结 | 冻结目录对象、ValidationResult 与 allowed_commands 参数 schema | Phase 2C diagnostics registry |
| DIA-TODO-018 | diagnostics | DIA-BLK-004：脱敏规则矩阵未冻结 | 冻结字段分级、deny-list 与 redaction.profile 语义 | Phase 3 diagnostics redaction |
| DIA-TODO-020 | diagnostics | DIA-BLK-005：导出格式/checksum/target 白名单未冻结 | 冻结 format/checksum/allowed_targets 与 remote.enabled 默认门禁 | Phase 3 diagnostics export |
| DIA-TODO-021 | diagnostics | DIA-BLK-006：metrics 桥接接口未冻结 | metrics 侧最小桥接接口与标签白名单冻结 | Phase 3 diagnostics bridge |
| DIA-TODO-022 | diagnostics | DIA-BLK-006：audit 桥接接口未冻结 | audit 侧 write_audit 最小适配接口与失败语义冻结 | Phase 3 diagnostics bridge |
| HLT-TODO-009 | health | HLT-BLK-001：platform 线程/定时抽象未统一 | health 侧完成对 platform 抽象的回链确认 | Phase 2B health scheduler |
| HLT-TODO-012 | health | HLT-BLK-002：事件总线发布接口未冻结 | 冻结 publish_transition 最小接口与失败返回 | Phase 3 health event |
| HLT-TODO-014 | health | HLT-BLK-003：infra.health 键命名未冻结 | 在 profiles/runtime policy 中冻结 watchdog/health 键名与覆盖优先级 | Phase 3 health config |
| LOG-TODO-012 | logging | LOG-BLK-001：logging config 模型未冻结 | 冻结 apply(config) 结构、冲突裁定规则、运行时 patch 形状 | Phase 3 logging config |
| LOG-TODO-013 | logging | LOG-BLK-002：metrics 接口未冻结 | metrics 侧冻结指标出口、标签白名单与失败语义 | Phase 3 logging bridge |
| MET-TODO-019 | metrics | MET-BLK-002、MET-BLK-004：audit/logging 接口未冻结 | audit 最小写入接口、logging 错误日志接口冻结 | Phase 3 metrics bridge |
| OTA-TODO-011 | ota | OTA-BLK-03：boot confirm 成功判据未冻结 | 冻结 boot confirm 成功判据与回退前观察窗口 | Phase 3 ota boot confirm |
| PLG-TODO-014 | plugin | PLG-BLK-01：manifest schema 未定 | 冻结 PluginManifest 字段集与扩展命名空间策略 | Phase 3 plugin artifact model |
| PLG-TODO-015 | plugin | PLG-BLK-03：签名校验与信任链规范未定 | 冻结 trust_store、链验算法、信任等级与失败码映射 | Phase 3 plugin signature |
| PLG-TODO-016 | plugin | PLG-BLK-02：ABI 兼容矩阵与 Host ABI 识别未定 | 冻结 ABI 版本格式、兼容规则与 platform tag 语义 | Phase 3 plugin compatibility |
| PLG-TODO-017 | plugin | INF-BLK-09 上游未解：manifest/signature/abi 三项未同步冻结 | 先完成 PLG-BLK-01/02/03 对应冻结，再补报告对象 | Phase 3 plugin reports |
| POL-TODO-019 | policy | POL-BLK-003：audit 最小写入接口未冻结 | audit 侧冻结 write_audit 最小接口与字段集合 | Phase 3 policy bridge |
| POL-TODO-020 | policy | POL-BLK-004：metrics/health 桥接接口未冻结 | metrics/health 给出最小桥接接口、标签白名单、探针状态对象 | Phase 3 policy bridge |
| POL-TODO-021 | policy | POL-BLK-004：metrics/health 桥接接口未冻结 | metrics/health 给出最小桥接接口、标签白名单、探针状态对象 | Phase 3 policy probe |
| TRC-TODO-015 | tracing | TRC-BLK-001、TRC-BLK-002：metrics/audit 桥接接口未冻结 | metrics 与 audit 侧最小桥接接口冻结 | Phase 3 tracing bridge |
| WDG-TODO-012 | watchdog | WDG-BLK-01：platform monotonic clock 与 scheduler 抽象未冻结 | watchdog 侧对 platform 抽象完成回链确认 | Phase 3 watchdog scheduler |
| WDG-TODO-014 | watchdog | WDG-BLK-02：事件总线最小发布接口未统一 | 冻结 timeout event publish 最小接口与失败返回 | Phase 3 watchdog event |
| WDG-TODO-018 | watchdog | WDG-BLK-04：watchdog 键名与覆盖优先级未冻结 | 在 profiles/runtime policy 中冻结 watchdog 键名与覆盖优先级 | Phase 3 watchdog config |

## 8. 源状态未标 blocked、但执行前需要同步处理的设计缺口

| 任务 ID | 组件 | 相关 blocker | 说明 |
|---|---|---|---|
| SEC-TODO-007 | secret | SEC-BLK-001 | file backend 加密与根目录策略未冻结，建议在 FileSecretBackend 落地前先补策略 |
| SEC-TODO-010 | secret | SEC-BLK-002 | dual-slot 轮换验证规则未冻结，建议与轮换骨架一并补齐 |
| SEC-TODO-012 | secret | SEC-BLK-004 | 审计注册点细节未统一，建议与 Audit bridge 一并收口 |
| OTA-TODO-012、OTA-TODO-018 | ota | OTA-BLK-01 | rollback token 生命周期与持久化位置未冻结，建议先补设计再做 rollback controller |
| OTA-TODO-006、OTA-TODO-021 | ota | OTA-BLK-05 | ota profile 键命名与覆盖优先级未最终收敛，建议先冻结键域 |
| OTA-TODO-020 | ota | OTA-BLK-03 | boot confirm 成功判据未冻结，建议与 OTA-TODO-011 配套推进 |
| PLG-TODO-011 | plugin | PLG-BLK-04 | PluginRuntimeBridge 与平台动态库接口约定不完整，可先使用 mock bridge |

## 9. 全量任务清单

说明：以下清单保留全部任务；已完成项统一标记为 `done`。

### 9.1 全局新增任务（来自实施步骤指引）

- done INF-PLAT-INT-001 顶层接入 integration 子目录与标签规范
- done INF-NAME-001 统一 tracer/tracing 命名与引用
- done INF-CONCUR-001 冻结并发背压与锁顺序规范
- done INF-GATE-UNIFY-001 建立 infra 统一 gate 脚本

### 9.2 platform_linux

- done PLAT-LNX-TODO-001 定义 PlatformInitConfig 数据结构头文件
- done PLAT-LNX-TODO-002 定义 PlatformCapabilitySet 数据结构头文件
- done PLAT-LNX-TODO-003 定义 PlatformError 与 PlatformResult 头文件
- done PLAT-LNX-TODO-004 定义 IThread 接口头文件
- done PLAT-LNX-TODO-005 定义 ITimer 接口头文件
- done PLAT-LNX-TODO-006 定义 IQueue 接口头文件
- done PLAT-LNX-TODO-007 定义 IFileSystem 接口头文件
- done PLAT-LNX-TODO-008 定义 INetwork 接口头文件
- done PLAT-LNX-TODO-009 定义 IIPC 接口头文件
- done PLAT-LNX-TODO-010 实现 LinuxPlatformFactory create(config) 骨架
- done PLAT-LNX-TODO-011 实现 CapabilityRegistry 状态登记骨架
- done PLAT-LNX-TODO-012 实现 PosixThreadProvider 骨架
- done PLAT-LNX-TODO-013 实现 PosixTimerProvider 骨架
- done PLAT-LNX-TODO-014 实现 BlockingQueueProvider 骨架
- done PLAT-LNX-TODO-015 实现 LinuxFileSystemProvider 骨架
- done PLAT-LNX-TODO-016 实现 LinuxNetworkProvider 骨架
- done PLAT-LNX-TODO-017 实现 UnixIpcProvider 骨架
- done PLAT-LNX-TODO-018 实现 HalAvailabilityBridge 与 HalStub 桩
- done PLAT-LNX-TODO-019 注册 platform unit 测试目录与目标
- done PLAT-LNX-TODO-020 注册 platform integration 测试目录与目标
- done PLAT-LNX-TODO-021 验证平台初始化集成路径
- done PLAT-LNX-TODO-022 回写 platform/linux 门禁与交付证据
- done PLAT-LNX-TODO-023 补齐 HAL 最小接口设计前置
- done PLAT-LNX-TODO-024 补齐网络 fallback 策略前置设计
- done PLAT-LNX-TODO-025 补齐 profile 注入键与入口设计前置

### 9.3 profiles

- done PRF-TODO-001 定义 IProfileCatalog 接口头文件
- done PRF-TODO-002 定义 BuildProfileManifest 对象头文件
- done PRF-TODO-003 定义 RuntimePolicySnapshot 对象头文件
- done PRF-TODO-004 定义 ValidationReport 对象与私有错误码头文件
- done PRF-TODO-005 定义 ILastKnownGoodStore 接口头文件
- done PRF-TODO-006 实现 ProfileCatalog 最小资产发现
- done PRF-TODO-007 实现 BuildProfileResolver 最小解析流程
- done PRF-TODO-008 实现 RuntimePolicyProvider 最小加载流程
- done PRF-TODO-009 实现 ProfileOverlayComposer 合并流程
- done PRF-TODO-010 定义 IProfileCompatibilityValidator 接口并落最小实现
- done PRF-TODO-011 校验 BuildManifest 与 RuntimeSnapshot 模块矩阵一致性
- done PRF-TODO-012 实现 LastKnownGoodStore 最小读写与激活回退路径
- done PRF-TODO-013 补齐 5 档 runtime_policy.yaml 最小 schema 与策略域
- done PRF-TODO-014 新增 profiles 模块 CMake 接线
- done PRF-TODO-015 注册 profiles unit 测试目录与目标
- done PRF-TODO-016 注册 profiles integration 测试目录与目标
- done PRF-TODO-017 实现 ProfileTelemetryAdapter 最小观测链路
- done PRF-TODO-018 回写 profiles 专项 Gate 与交付证据
- done PRF-TODO-019 补齐模块标识与适配器命名冻结表
- done PRF-TODO-020 补齐 overlay 输入契约与来源合法性设计
- done PRF-TODO-021 补齐 LKG 存储介质与失效语义设计

### 9.4 infrastructure 子系统

- done INF-TODO-001 定义 InfraContext 数据结构
- done INF-TODO-002 新增 IInfrastructureService 接口与 Facade 生命周期骨架
- done INF-TODO-003 定义 LogEvent 数据结构
- done INF-TODO-004 定义 AuditEvent 数据结构
- done INF-TODO-005 新增 ILogger 接口
- done INF-TODO-006 新增 IAuditLogger 接口
- done INF-TODO-007 定义 HealthSnapshot 数据结构
- done INF-TODO-008 新增 IHealthMonitor 接口
- done INF-TODO-009 定义 infra 私有错误码域
- done INF-TODO-010 接线 infra CMake 落盘入口
- done INF-TODO-011 注册 infra 单元测试入口
- done INF-TODO-012 注册 infra contracts 边界测试入口
- done INF-TODO-013 定义 IConfigCenter 接口骨架
- done INF-TODO-014 定义 ISecretManager 接口骨架
- done INF-TODO-015 定义 IOTAManager 接口骨架与 UpgradeOutcome 对接点
- done INF-TODO-016 新增 AuditService 独立组件骨架
- done INF-TODO-017 冻结 SecurityPolicyManager 接口与策略对象
- done INF-TODO-018 冻结 DiagnosticsSnapshot 与 IDiagnosticsService 接口
- done INF-TODO-019 冻结 PluginDescriptor 与 IPluginManager 接口
- not_started INF-TODO-020 ARC-01
- done INF-TODO-021 ARC-02

### 9.5 audit

- done AUD-TODO-001 定义 AuditEvent 数据结构
- done AUD-TODO-002 定义 AuditContext 数据结构
- done AUD-TODO-003 定义 AuditWriteOutcome 数据结构
- done AUD-TODO-004 定义 ExportQuery 数据结构
- done AUD-TODO-005 定义 ExportResult 数据结构
- done AUD-TODO-006 定义 IAuditLogger 接口头文件
- done AUD-TODO-007 定义 AuditErrors 错误码域
- not_started AUD-TODO-008 实现 AuditValidator 字段校验骨架
- not_started AUD-TODO-009 实现 AuditPipeline 主写骨架
- not_started AUD-TODO-010 实现 AuditFallbackPipeline 降级骨架
- not_started AUD-TODO-011 实现 AuditServiceFacade 入口骨架
- blocked AUD-TODO-012 实现 AuditExporter 导出与脱敏骨架
- blocked AUD-TODO-013 定义 IAuditRetention 接口与 RetentionOutcome 对象
- blocked AUD-TODO-014 定义 IAuditHealthProbe 接口与 AuditHealthStatus 对象
- blocked AUD-TODO-015 实现 AuditMetricsBridge 指标桥接骨架
- not_started AUD-TODO-016 注册 audit 源码到 infra CMake
- not_started AUD-TODO-017 注册 audit 的 unit 与 contract 测试入口
- not_started AUD-TODO-018 注册 audit integration 测试入口
- not_started AUD-TODO-019 回写 audit 质量门与交付证据

### 9.6 config

- done CFG-TODO-001 定义 IConfigCenter 接口头文件
- done CFG-TODO-002 定义 IConfigLoader 接口头文件
- done CFG-TODO-003 定义 IConfigValidator 接口头文件
- done CFG-TODO-004 定义 IConfigSnapshotStore 接口头文件
- done CFG-TODO-005 定义 IConfigPublisher 接口头文件
- done CFG-TODO-006 定义 ConfigTypes 核心对象
- not_started CFG-TODO-007 实现 ConfigCenterFacade 生命周期骨架
- not_started CFG-TODO-008 实现 ConfigLoader 四层读取骨架
- not_started CFG-TODO-009 实现 ConfigMerger 覆盖与来源追踪骨架
- not_started CFG-TODO-010 实现 ConfigValidator 规则校验骨架
- not_started CFG-TODO-011 实现 ConfigSnapshotStore 快照与 LKG 骨架
- done CFG-TODO-012 定义 ConfigErrors 错误码域与映射
- blocked CFG-TODO-013 实现 ConfigPublisher 运行时覆盖发布骨架
- not_started CFG-TODO-014 注册 config 代码到 infra CMake
- not_started CFG-TODO-015 注册 config unit 与 contract 测试入口
- not_started CFG-TODO-016 补齐 config integration 注册拓扑
- not_started CFG-TODO-017 回写 config 质量门与交付证据

### 9.7 diagnostics

- done DIA-TODO-001 定义 DiagnosticsCommand 数据结构
- done DIA-TODO-002 定义 CommandDecision 数据结构
- done DIA-TODO-003 定义 EvidenceBundle 数据结构
- done DIA-TODO-004 定义 DiagnosticsSnapshot 数据结构
- done DIA-TODO-005 定义 SnapshotExportResult 数据结构
- done DIA-TODO-006 定义 DiagnosticsErrors 错误码域
- done DIA-TODO-007 补齐 IDiagnosticsService 请求与返回对象设计
- blocked DIA-TODO-008 补齐 CommandRegistry 目录与校验返回对象设计
- not_started DIA-TODO-009 定义 IDiagnosticsPolicyGuard 接口头文件
- done DIA-TODO-010 定义 IDiagnosticsService 接口头文件
- blocked DIA-TODO-011 定义 IDiagnosticsCommandRegistry 接口头文件
- not_started DIA-TODO-012 实现 DiagnosticsServiceFacade 生命周期与 safe_mode 骨架
- blocked DIA-TODO-013 实现 CommandRegistry 白名单治理骨架
- not_started DIA-TODO-014 实现 CommandPolicyGuard 准入骨架
- not_started DIA-TODO-015 实现 CommandExecutor 执行骨架
- not_started DIA-TODO-016 实现 EvidenceCollector 证据聚合骨架
- not_started DIA-TODO-017 实现 SnapshotAssembler 快照组装骨架
- blocked DIA-TODO-018 实现 RedactionEngine 脱敏骨架
- not_started DIA-TODO-019 实现 SnapshotStore 持久化骨架
- blocked DIA-TODO-020 实现 ExportManager 导出骨架
- blocked DIA-TODO-021 实现 DiagnosticsMetricsBridge 指标桥接骨架
- blocked DIA-TODO-022 实现 DiagnosticsAuditBridge 审计桥接骨架
- not_started DIA-TODO-023 注册 diagnostics 源码到 infra CMake
- not_started DIA-TODO-024 注册 diagnostics 的 unit 与 contract 测试入口
- not_started DIA-TODO-025 注册 diagnostics integration 测试入口
- not_started DIA-TODO-026 回写 diagnostics 质量门与交付证据

### 9.8 health

- done HLT-TODO-001 定义 IHealthProbe 接口头文件
- done HLT-TODO-002 定义 IHealthMonitor 接口头文件
- done HLT-TODO-003 定义 IHealthPolicy 接口头文件
- done HLT-TODO-004 定义 ProbeTypes 数据结构
- done HLT-TODO-005 定义 HealthStateTypes 数据结构
- done HLT-TODO-006 定义 RecoveryHint 数据结构
- not_started HLT-TODO-007 实现 HealthMonitorFacade 生命周期骨架
- not_started HLT-TODO-008 实现 ProbeRegistry 注册治理骨架
- blocked HLT-TODO-009 实现 ProbeScheduler 调度骨架
- not_started HLT-TODO-010 实现 ProbeExecutor 执行骨架
- not_started HLT-TODO-011 实现 HealthEvaluator 三态评估骨架
- blocked HLT-TODO-012 实现 HealthEventPublisher 状态事件发布骨架
- not_started HLT-TODO-013 定义 HealthErrors 错误码域与映射
- blocked HLT-TODO-014 定义 HealthConfigPolicy 配置模型与覆盖策略
- not_started HLT-TODO-015 实现 RecoveryHintEmitter 边界守卫骨架
- not_started HLT-TODO-016 注册 health 源码到 infra CMake
- not_started HLT-TODO-017 注册 health 的 unit/contract/integration 测试入口
- not_started HLT-TODO-018 回写 health 质量门与交付证据

### 9.9 logging

- done LOG-TODO-001 定义 ILogger 接口头文件
- done LOG-TODO-002 定义 IAuditLinkAdapter 接口头文件
- done LOG-TODO-003 定义 LogContext 数据结构
- done LOG-TODO-004 定义 LogEvent 数据结构
- done LOG-TODO-005 定义 AuditEvent 数据结构
- not_started LOG-TODO-006 实现 LoggingFacade 生命周期骨架
- not_started LOG-TODO-007 实现 SinkDispatcher 路由骨架
- not_started LOG-TODO-008 实现 AsyncQueueController 队列策略骨架
- not_started LOG-TODO-009 实现 AuditLinkAdapter 审计关联适配骨架
- not_started LOG-TODO-010 定义 LoggingErrors 错误码域
- not_started LOG-TODO-011 实现 LoggingRecovery 故障降级骨架
- blocked LOG-TODO-012 实现 LoggingConfigAdapter 四层配置适配
- blocked LOG-TODO-013 实现 LoggingMetricsBridge 指标桥接骨架
- not_started LOG-TODO-014 注册 logging 构建落点到 infra CMake
- not_started LOG-TODO-015 注册 logging 单元与契约测试入口
- not_started LOG-TODO-016 回写 logging 质量门与交付证据

### 9.10 metrics

- done MET-TODO-001 定义 IMetricsProvider 接口头文件
- done MET-TODO-002 定义 IMeter 接口头文件
- done MET-TODO-003 定义 IMetricExporter 接口头文件
- done MET-TODO-004 定义 IMetricConfigPolicy 接口头文件
- done MET-TODO-005 定义 IMetricsHealthProbe 接口头文件
- done MET-TODO-006 定义 MetricTypes 核心对象头文件
- done MET-TODO-007 定义 MetricsSnapshots 对象头文件
- done MET-TODO-008 定义 MetricsErrors 错误码域
- not_started MET-TODO-009 实现 MetricsFacade 初始化与写入骨架
- not_started MET-TODO-010 实现 InstrumentRegistry 唯一性管理骨架
- not_started MET-TODO-011 实现 AggregationEngine 聚合骨架
- not_started MET-TODO-012 实现 CardinalityGuard 标签治理骨架
- not_started MET-TODO-013 实现 MetricReaderScheduler 调度骨架
- not_started MET-TODO-014 实现 MetricsExporterAdapter 首版导出骨架
- not_started MET-TODO-015 实现 MetricsRecovery 降级与恢复骨架
- not_started MET-TODO-016 定义 MetricsConfigPolicy 配置模型与默认策略
- not_started MET-TODO-017 注册 metrics 代码到 infra CMake
- not_started MET-TODO-018 注册 metrics 的 unit 与 contract 测试入口
- blocked MET-TODO-019 接线 MetricsAuditBridge 与 MetricsLoggingBridge 骨架
- not_started MET-TODO-020 回写 metrics 质量门与交付证据
- not_started MET-TODO-021 ARC-01
- not_started MET-TODO-022 ARC-02

### 9.11 ota

- done OTA-TODO-001 定义 OTATypes 核心对象头文件组
- done OTA-TODO-002 定义 IOTAManager 接口头文件
- done OTA-TODO-003 定义 IOTAPackageVerifier 接口头文件
- done OTA-TODO-004 定义 IInstallExecutor 接口头文件
- done OTA-TODO-005 定义 IBootControlAdapter 接口头文件
- not_started OTA-TODO-006 实现 OTAPrecheckService 骨架
- not_started OTA-TODO-007 实现 PackageVerifier 骨架
- not_started OTA-TODO-008 实现 ArtifactCompatibilityEvaluator 骨架
- not_started OTA-TODO-009 实现 InstallExecutor 骨架
- not_started OTA-TODO-010 实现 SlotSwitchCoordinator 骨架
- blocked OTA-TODO-011 实现 BootConfirmationMonitor 骨架
- not_started OTA-TODO-012 实现 RollbackController 骨架
- not_started OTA-TODO-013 实现 OTAAuditBridge 骨架
- not_started OTA-TODO-014 实现 OTAHealthProbe 骨架
- not_started OTA-TODO-015 接线 ota 到 infra CMake 构建入口
- not_started OTA-TODO-016 注册 ota 的 unit 与 contract 测试入口
- not_started OTA-TODO-017 注册 ota integration/failure 测试入口
- not_started OTA-TODO-018 补齐 rollback token 生命周期与持久化设计
- done OTA-TODO-019 补齐签名算法与 trust anchor 接口设计
- not_started OTA-TODO-020 补齐 boot confirm 成功判据设计
- not_started OTA-TODO-021 补齐 ota profile 键命名与覆盖优先级收敛

### 9.12 plugin

- done PLG-TODO-001 定义 PluginDescriptor 数据结构
- done PLG-TODO-002 定义 PluginCatalog 数据结构
- done PLG-TODO-003 新增 IPluginManager 接口与骨架实现
- done PLG-TODO-004 新增 IPluginPolicyGate 接口
- not_started PLG-TODO-005 建立 PluginValidationPipeline 骨架与三检流程
- not_started PLG-TODO-006 新增 PluginAuditAdapter 适配器
- done PLG-TODO-007 定义 plugin 私有错误码域
- not_started PLG-TODO-008 接线 infra/src/plugin 与 infra/include/plugin CMake 目标
- not_started PLG-TODO-009 注册 tests/unit/infra/plugin 单元测试入口
- not_started PLG-TODO-010 注册 tests/contract/infra/plugin 合约边界测试入口
- not_started PLG-TODO-011 新增 PluginLifecycleManager 状态机与转移测试
- not_started PLG-TODO-012 编写 plugin 失败注入与可观测性测试
- not_started PLG-TODO-013 编写 Profile 插件治理行为矩阵测试
- blocked PLG-TODO-014 定义 PluginManifest 对象与 schema 冻结
- blocked PLG-TODO-015 定义 IPluginSignatureVerifier 与签名链路规范
- blocked PLG-TODO-016 定义 IPluginCompatibilityEngine 与 ABI 兼容矩阵
- blocked PLG-TODO-017 定义 SignatureReport 与 CompatibilityReport 对象

### 9.13 policy

- done POL-TODO-001 定义 PolicyBundle 与 PolicyRuleDescriptor 数据结构
- done POL-TODO-002 定义 PolicyPatch 与 ValidationReport 数据结构
- done POL-TODO-003 定义 PolicySnapshot 与 PolicyOpResult 数据结构
- done POL-TODO-004 定义 PolicyQueryContext 与 PolicyDecisionRef 数据结构
- done POL-TODO-005 定义 PolicyErrors 错误码域
- done POL-TODO-006 定义 ISecurityPolicyManager 接口头文件
- done POL-TODO-007 定义 IPolicyLoader 接口头文件
- done POL-TODO-008 定义 IPolicySchemaValidator 接口头文件
- done POL-TODO-009 定义 IPolicySnapshotStore 接口头文件
- not_started POL-TODO-010 实现 PolicyLoader 配置读取骨架
- not_started POL-TODO-011 实现 PolicySchemaValidator 最小校验骨架
- not_started POL-TODO-012 实现 PolicyConflictResolver 冲突裁定骨架
- not_started POL-TODO-013 实现 PolicySnapshotStore generation/LKG 骨架
- not_started POL-TODO-014 实现 PolicyDecisionProjector 查询投影骨架
- not_started POL-TODO-015 实现 SecurityPolicyManager 主链骨架
- not_started POL-TODO-016 注册 policy 源码到 infra CMake
- not_started POL-TODO-017 注册 policy 的 unit 与 contract 测试入口
- not_started POL-TODO-018 注册 policy integration 测试入口
- blocked POL-TODO-019 实现 PolicyAuditBridge 审计桥接骨架
- blocked POL-TODO-020 实现 PolicyMetricsBridge 指标桥接骨架
- blocked POL-TODO-021 实现 PolicyHealthProbe 健康探针骨架
- not_started POL-TODO-022 回写 policy 质量门与交付证据

### 9.14 secret

- done SEC-TODO-001 定义 ISecretManager 接口头文件
- done SEC-TODO-002 定义 ISecretHealthSource 接口头文件
- done SEC-TODO-003 定义 SecretTypes 对象模型
- done SEC-TODO-004 定义 SecureBuffer 语义与约束
- done SEC-TODO-005 定义 ISecretBackend 统一协议
- not_started SEC-TODO-006 实现 MockSecretBackend 骨架
- not_started SEC-TODO-007 实现 FileSecretBackend 最小骨架
- not_started SEC-TODO-008 实现 SecretManagerFacade 访问骨架
- not_started SEC-TODO-009 实现 SecretLeaseRegistry 生命周期管理
- not_started SEC-TODO-010 实现 SecretRotationCoordinator 轮换骨架
- done SEC-TODO-011 定义 SecretErrors 错误码域与映射
- not_started SEC-TODO-012 实现 SecretAuditBridge 审计桥骨架
- not_started SEC-TODO-013 实现 SecretHealthProbe 健康出口骨架
- not_started SEC-TODO-014 接线 infra/secret 到 CMake
- not_started SEC-TODO-015 注册 secret unit 与 contract 测试入口
- not_started SEC-TODO-016 注册 secret integration 与故障注入入口
- not_started SEC-TODO-017 回写 secret 质量门与交付证据

### 9.15 tracing

- done TRC-TODO-001 定义 ITracerProvider 接口头文件
- done TRC-TODO-002 定义 ITracer 接口头文件
- done TRC-TODO-003 定义 ISpan 接口头文件
- done TRC-TODO-004 定义 ITraceContextPropagator 接口头文件
- done TRC-TODO-005 定义 TraceTypes 数据结构
- done TRC-TODO-006 定义 TraceErrors 错误码域
- done TRC-TODO-007 注册 tracing 头文件到 infra 公开包含路径
- not_started TRC-TODO-008 实现 TracerProviderImpl 生命周期骨架
- not_started TRC-TODO-009 实现 TracerImpl 与 SpanImpl 生命周期闭环
- not_started TRC-TODO-010 实现 ContextPropagationAdapter 注入提取
- not_started TRC-TODO-011 实现 SamplingPolicyEngine 本地采样策略
- not_started TRC-TODO-012 实现 BatchSpanBuffer 队列与导出触发
- not_started TRC-TODO-013 实现 SpanProcessorPipeline 与 ExporterAdapter 首版
- not_started TRC-TODO-014 实现 TraceHealthProbe 降级与恢复判定骨架
- blocked TRC-TODO-015 实现 TraceMetricsBridge 与 TraceAuditBridge 桥接骨架
- not_started TRC-TODO-016 定义 tracing 配置模型与默认策略
- not_started TRC-TODO-017 注册 tracing 源码到 infra CMake
- not_started TRC-TODO-018 注册 tracing 的 unit 与 contract 测试入口
- not_started TRC-TODO-020 ARC-01
- not_started TRC-TODO-021 ARC-02

### 9.16 watchdog

- done WDG-TODO-001 定义 IWatchdogService 接口头文件
- done WDG-TODO-002 定义 IHeartbeatSource 接口头文件
- done WDG-TODO-003 定义 ITimeoutPolicy 接口头文件
- done WDG-TODO-004 定义 WatchedEntityDescriptor 数据结构
- done WDG-TODO-005 定义 HeartbeatSample 数据结构
- done WDG-TODO-006 定义 TimeoutDecision 数据结构
- done WDG-TODO-007 定义 WatchdogSnapshot 数据结构
- done WDG-TODO-008 定义 RecoveryHintRequest 边界对象
- not_started WDG-TODO-009 实现 WatchdogServiceFacade 生命周期骨架
- not_started WDG-TODO-010 实现 HeartbeatRegistry 注册治理骨架
- not_started WDG-TODO-011 实现 HeartbeatIngestor 采集骨架
- blocked WDG-TODO-012 实现 DeadlineWheel 扫描骨架
- not_started WDG-TODO-013 实现 TimeoutPolicyEngine 判级骨架
- blocked WDG-TODO-014 实现 TimeoutEventPublisher 发布骨架
- not_started WDG-TODO-015 实现 WatchdogAuditBridge 审计桥接骨架
- not_started WDG-TODO-016 实现 WatchdogMetricsBridge 指标桥接骨架
- not_started WDG-TODO-017 实现 RecoveryRequestEmitter 边界守卫骨架
- blocked WDG-TODO-018 定义 WatchdogConfigPolicy 配置模型与覆盖规则
- not_started WDG-TODO-019 定义 WatchdogErrors 私有错误码域与映射
- not_started WDG-TODO-020 接线 watchdog 到 infra CMake 构建入口
- not_started WDG-TODO-021 注册 watchdog 的 unit 与 contract 测试入口
- not_started WDG-TODO-022 注册 watchdog integration/failure/profile 测试入口
- not_started WDG-TODO-023 回写 watchdog 门禁结果与交付证据

## 10. 使用口径

1. 每日执行时，优先从第 4 节当前 Phase 的最前一行任务组开始，不跨 Phase 乱序推进。
2. 组件内部继续沿用各自专项 TODO 已定义的顺序，不在本清单中重新拆粒度。
3. 遇到 `blocked` 任务，直接转到第 7 节按 blocker 解阻，不得跳过 blocker 直接标 done。
4. 执行完成后，只更新源 TODO 文档状态与证据；本清单只在阶段完成或顺序调整时更新。