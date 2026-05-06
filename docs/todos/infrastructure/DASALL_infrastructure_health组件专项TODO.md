# DASALL infrastructure 子系统 health 组件专项 TODO

最近更新时间：2026-04-08  
阶段：Detailed Design -> Special TODO  
适用范围：infra/health

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_health模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/
12. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
17. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
18. 当前代码与测试现状：infra/CMakeLists.txt、infra/include/、infra/src/health/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写 ADR-005/006/007/008 已冻结结论。
2. 不越过 infrastructure/health 组件边界扩张到无关模块。
3. 不把讨论事项写成 Build-ready 实现任务。
4. 每项任务必须具备代码目标、测试目标、验收命令三件套。
5. 证据不足处必须转为 Blocked + 补设计前置任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供统一健康探针编排能力，输出 liveness/readiness/degraded 三态。
2. 提供可查询、可回放的 HealthSnapshot 与状态转移事件。
3. 在不越权的前提下提供 RecoveryHint（建议）能力，不触发执行动作。
4. 与 logging/metrics/watchdog/runtime 主控链路协同，但不接管恢复裁定与调度。

### 2.2 范围边界

纳入范围：

1. health 对外接口（IHealthProbe/IHealthMonitor/IHealthPolicy）与核心对象。
2. probe 注册/调度/执行、策略评估、状态存储、事件发布、恢复建议主链路。
3. health 私有错误码域、配置模型、CMake 接线、unit/contract/failure-injection 测试入口。
4. health 组件质量门、阻塞项、风险回退策略与交付证据回写。

不纳入范围：

1. runtime 主状态机推进、恢复执行准入与调度裁定。
2. contracts 共享语义对象扩写或重定义。
3. watchdog 完整实现（本轮仅保留接口协同点与阻塞说明）。
4. event bus 全量实现与跨进程聚合（仅做最小发布接口适配点）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1：约束与边界抽取输出）

| ID | 来源 | 类型 | 约束内容 | 对 health TODO 的影响 |
|---|---|---|---|---|
| HLT-TC001 | health 设计 1.1/6.1；架构 8.7 | Must | health 必须提供三态健康能力并纳入 infra 统一治理 | 任务必须覆盖接口、对象、主流程、异常流程 |
| HLT-TC002 | 架构 3.7；蓝图 4.2 | Must | 依赖方向单向，health 不反向依赖上层实现 | 代码目标仅限 infra/tests/docs/cmake 路径 |
| HLT-TC003 | 蓝图 4.3；工程规范 3.3 | Must | 跨模块调用通过冻结接口/contracts，不直连实现 | 任务禁止 include runtime/cognition/tools 实现类 |
| HLT-TC004 | ADR-005 | Must | contracts 与边界冻结前不可反向改写架构结论 | 设计缺口必须列 Blocked，不伪造实现任务 |
| HLT-TC005 | ADR-006 | Must-Not | health 不承担上下文装配与 Prompt 渲染语义 | 仅输出观测事实、快照与事件 |
| HLT-TC006 | ADR-007 | Must-Not | health 不拥有失败语义裁定与恢复执行权 | RecoveryHint 任务必须限制为“建议” |
| HLT-TC007 | ADR-008 | Must | health 不拥有全局调度权，只服务主控链路 | 禁止在 health 任务中引入 orchestrator 逻辑 |
| HLT-TC008 | health 设计 6.5；contracts 冻结约束 | Must-Not | probe 线程模型与实现细节不进入 contracts | 对象任务仅冻结 infra 私有对象 |
| HLT-TC009 | 工程规范 3.6 | Must | 错误不可吞没，失败必须可观测 | 每个异常路径任务都要绑定错误码与观测出口 |
| HLT-TC010 | 工程规范 3.7 | Should | 新增公共接口同步测试 | 每个接口任务必须绑定 unit/contract |
| HLT-TC011 | health 设计 6.9；蓝图 5.1 | Must | 配置支持默认/Profile/部署覆盖，不绕过治理链路 | 配置任务必须包含覆盖优先级与回退策略 |
| HLT-TC012 | 落地步骤指引 阶段 C | Must | infra 底座先行且每阶段可测试 | 执行顺序需先接口对象，再链路，再门禁 |
| HLT-TC013 | health 设计 11.1 | Must | 存在线程抽象、事件总线、profile 键命名、contract 模板阻塞项 | 需显式输出 Blocked 与解阻动作 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，health/ 子目录已落盘接口与对象 | health public headers 已冻结，后续差距集中在服务实现 |
| infra/src/health/ | 空目录 | health 实现未落盘 |
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | health 公共接口已落盘，但 health 服务实现仍未接入构建 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration 拓扑已接入顶层，后续只需补 health 具体集成/故障用例 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录 | health unit 发现性已建立，后续只需补具体用例 |
| tests/contract/CMakeLists.txt | centralized registration 已存在 | 可承载 health 边界 contract 测试 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成并执行 L3/L2 混合专项 TODO。  
当前最小可执行粒度：函数/接口/数据结构级（L3 为主，局部 L2；受阻项降为 L0 Blocked）。

证据：

1. 存在明确核心接口清单：IHealthProbe、IHealthMonitor、IHealthPolicy（health 设计 6.6）。
2. 存在核心对象字段定义：ProbeDescriptor、ProbeResult、HealthSnapshot、HealthTransition、RecoveryHint（health 设计 6.5）。
3. 存在主流程与异常流程：6.7 正常 8 步 + 6.8 异常分类/恢复动作/兜底策略。
4. 存在错误码域与语义：INF_E_HEALTH_PROBE_TIMEOUT、INF_E_HEALTH_PROBE_EXCEPTION、INF_E_HEALTH_PROBE_NOT_FOUND、INF_E_HEALTH_POLICY_INVALID、INF_E_HEALTH_EVENT_PUBLISH_FAIL（health 设计 6.6）。
5. 存在落盘路径与测试出口：infra/include、infra/src/health、tests/unit/infra/health、tests/integration/infra/health（health 设计 8.1、7）。
6. 仍有阻塞证据：platform 线程/定时抽象未统一、事件总线接口未冻结、profile 键命名未冻结、contract 模板缺失（health 设计 11.1）。

### 4.2 粒度可行性评估表（Step 2：详细设计可执行性扫描输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| IHealthProbe | health 设计 6.6 | L3 | probe() 语义、超时/异常结构化返回 | 返回对象别名与错误映射矩阵未成文 | 直接拆接口冻结任务 |
| IHealthMonitor | health 设计 6.6/6.7 | L3 | register_probe/evaluate_now/get_snapshot/subscribe 语义 | listener 回调对象定义未冻结 | 直接拆接口 + Facade 骨架任务 |
| IHealthPolicy | health 设计 6.6/6.9 | L3 | evaluate(results) 语义与阈值约束 | policy version 编码规范未成文 | 直接拆接口 + 评估器任务 |
| ProbeDescriptor/ProbeResult | health 设计 6.5 | L3 | 字段与状态约束完整 | criticality 枚举扩展策略细节 | 直接拆数据结构任务 |
| HealthSnapshot/HealthTransition | health 设计 6.5/6.7/6.8 | L3 | 字段、状态转移、版本递增与回放约束 | 历史持久化策略未冻结 | 直接拆数据结构 + 状态存储任务 |
| RecoveryHint | health 设计 6.5/6.8；ADR-007 | L2 | 建议字段与“仅建议”边界明确 | suggested_action 枚举清单未成文 | 先冻结对象，再做边界 contract |
| ProbeRegistry/ProbeExecutor | health 设计 6.2/6.3/6.7 | L3 | 子组件职责、输入输出、异常路径明确 | 并发执行参数细节 | 直接拆函数级骨架任务 |
| ProbeScheduler | health 设计 6.2/6.7/8.1 | L2 | 调度职责、分组周期、超时语义明确 | 线程与定时抽象未统一 | 先输出 Blocked 前置，再进实现任务 |
| HealthEvaluator | health 设计 6.2/6.7/6.8 | L3 | 结果聚合、阈值判定、三态输出明确 | hysteresis 细节未成文 | 直接拆评估策略任务 |
| HealthEventPublisher | health 设计 6.2/6.8/10 | L2 | 状态变化才发事件、发布失败语义明确 | event bus 最小接口未冻结 | 先补接口设计后实现 |
| RecoveryHintEmitter | health 设计 6.2/6.8；ADR-007 | L2 | 输出建议且不执行动作边界明确 | contract 模板缺失 | 先补模板再进实现 |
| tests/integration/infra/health | health 设计 8.1/9.1；tests 现状 | L0 | 路径与用例建议存在，且 tests 顶层 integration 拓扑已接入 | health integration 用例尚未落盘 | 直接拆集成任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| IHealthProbe/IHealthMonitor/IHealthPolicy 接口冻结 | health 设计 6.6 | 接口 | HLT-TODO-001、HLT-TODO-002、HLT-TODO-003 | 先冻结边界，阻断上层直连实现 |
| ProbeDescriptor/ProbeResult/HealthSnapshot/Transition/RecoveryHint | health 设计 6.5 | 数据结构 | HLT-TODO-004、HLT-TODO-005、HLT-TODO-006 | 先稳定字段，再推进流程实现 |
| Facade 生命周期与初始化约束 | health 设计 6.2/6.7 | 生命周期/初始化 | HLT-TODO-007 | 主入口单独成任务，避免与执行器耦合 |
| Probe 注册与执行链路 | health 设计 6.2/6.3/6.7 | 流程 | HLT-TODO-008、HLT-TODO-009 | Registry 与 Executor 拆分单目标 |
| 三态评估与状态存储 | health 设计 6.2/6.7/6.8 | 流程 | HLT-TODO-010、HLT-TODO-011 | 评估与状态转移拆分，便于独立验收 |
| 事件发布与错误处理 | health 设计 6.8/6.10 | 异常/错误处理 | HLT-TODO-012、HLT-TODO-013 | 发布逻辑与错误码域拆分，避免任务过大 |
| 配置与 Profile 裁剪 | health 设计 6.9 | 配置 | HLT-TODO-014 | profile 键域已由 PRF-TODO-022 冻结，当前可直接进入 HealthConfigPolicy 实现 |
| RecoveryHint 边界守卫 | health 设计 6.8；ADR-007 | 适配器/边界 | HLT-TODO-015、HLT-BLK-004 | 明确建议与执行分离并 contract 化 |
| CMake 与测试门禁接线 | health 设计 7/8/9；代码现状 | 测试/门禁 | HLT-TODO-016、HLT-TODO-017 | 构建和 unit/contract 可先做，integration 用例待组件后续落盘 |
| 文档与证据回写 | health 设计 9.2/11 | 文档/交付证据 | HLT-TODO-018 | 对 gate、阻塞、回退证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | HLT-TODO-001~003 |
| 数据结构定义类任务 | 是 | HLT-TODO-004~006 |
| 生命周期与初始化类任务 | 是 | HLT-TODO-007 |
| 适配器/桥接类任务 | 是 | HLT-TODO-015 |
| 异常与错误处理类任务 | 是 | HLT-TODO-012~013 |
| 配置与 Profile 裁剪类任务 | 是 | HLT-TODO-014（2026-04-08 已由 PRF-TODO-022 解阻） |
| 测试与门禁类任务 | 是 | HLT-TODO-016~017 |
| 文档/交付证据回写类任务 | 是 | HLT-TODO-018 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HLT-TODO-001 | Done | 定义 IHealthProbe 接口头文件 | health 设计 6.6；编码规范 3.7 | 6.6 IHealthProbe | L3 | infra/include/health/IHealthProbe.h | probe(): ProbeResult | unit：接口可编译；contract：错误语义入口可映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/health/IHealthProbe.h、tests/unit/infra/health/HealthProbeInterfaceTest.cpp，并通过 `cmake --build build-ci --target dasall_infra dasall_unit_tests` 与 `ctest --test-dir build-ci --output-on-failure -R HealthProbeInterfaceTest` 验证接口签名可编译且未吸收 monitor 职责 | 仅当接口签名与 6.6 一致且不依赖业务实现时完成 |
| HLT-TODO-002 | Done | 定义 IHealthMonitor 接口头文件 | health 设计 6.6/6.7 | 6.6 IHealthMonitor | L3 | infra/include/health/IHealthMonitor.h | register_probe(name,group,probe), evaluate_now(), get_snapshot(), subscribe(listener) | unit：接口可编译；contract：快照边界不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-001 | listener 类型细节未冻结 | 先冻结最小 listener 抽象占位 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/health/IHealthMonitor.h，并移除 legacy 根层 wrapper，统一 tests/unit/infra/HealthMonitorInterfaceTest.cpp、tests/contract/smoke/HealthMonitorInterfaceBoundaryContractTest.cpp 的入口语义；通过 `cmake --build build-ci --target dasall_infra dasall_health_monitor_interface_unit_test dasall_contract_health_monitor_interface_boundary_test` 与 `ctest --test-dir build-ci --output-on-failure -R "HealthMonitorInterfaceTest|HealthMonitorInterfaceBoundaryContractTest"` 验证四方法边界与快照输出边界 | 仅当四个方法语义与 6.6 一致且可编译时完成 |
| HLT-TODO-003 | Done | 定义 IHealthPolicy 接口头文件 | health 设计 6.6/6.9 | 6.6 IHealthPolicy；6.9 策略配置 | L3 | infra/include/health/IHealthPolicy.h | evaluate(results): HealthSnapshot | unit：接口可编译；unit：阈值输入输出可约束 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-002 | policy version 规范未成文 | 先用字符串版本占位并保留扩展点 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/health/IHealthPolicy.h、tests/unit/infra/health/HealthPolicyInterfaceTest.cpp，并通过 `cmake --build build-ci --target dasall_infra dasall_health_policy_interface_unit_test` 与 `ctest --test-dir build-ci --output-on-failure -R HealthPolicyInterfaceTest` 验证 ProbeResultView 输入约束、HealthSnapshot 输出边界与 `policy_version()` 字符串占位扩展点 | 仅当策略接口可承载三态评估输入输出时完成 |
| HLT-TODO-004 | Done | 定义 ProbeTypes 数据结构 | health 设计 6.5 | 6.5 ProbeDescriptor/ProbeResult | L3 | infra/include/health/ProbeTypes.h | ProbeDescriptor, ProbeResult | unit：字段完整性与状态枚举覆盖 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-001 | 无 | 无 | 数据结构头文件、单测；2026-03-31 已落盘 infra/include/health/ProbeTypes.h、tests/unit/infra/health/ProbeTypesTest.cpp，并将 IHealthProbe 对 ProbeResult 的前置声明收敛为实际对象依赖；通过 `cmake --build build-ci --target dasall_infra dasall_health_probe_interface_unit_test dasall_health_probe_types_unit_test` 与 `ctest --test-dir build-ci --output-on-failure -R "HealthProbeInterfaceTest|ProbeTypesUnitTest"` 验证字段完整性、状态枚举和失败细节守卫 | 仅当字段与状态集合与 6.5 一致且默认语义可测试时完成 |
| HLT-TODO-005 | Done | 定义 HealthStateTypes 数据结构 | health 设计 6.5/6.7 | 6.5 HealthSnapshot/HealthTransition | L3 | infra/include/health/HealthStateTypes.h | HealthSnapshot, HealthTransition | unit：version 单调与状态转移字段校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-004 | 历史持久化策略未冻结 | 先仅实现进程内窗口语义 | 数据结构头文件、单测；2026-03-31 已落盘 infra/include/health/HealthStateTypes.h，并将 HealthSnapshot 入口统一收敛到 infra/include/health/HealthStateTypes.h、移除根层 wrapper；同步扩展 tests/unit/infra/HealthSnapshotTest.cpp 与 tests/contract/smoke/HealthSnapshotBoundaryContractTest.cpp，通过 `cmake --build build-ci --target dasall_infra dasall_health_snapshot_unit_test dasall_contract_health_snapshot_boundary_test` 与 `ctest --test-dir build-ci --output-on-failure -R "HealthSnapshotUnitTest|HealthSnapshotBoundaryContractTest"` 验证 version 元数据、状态枚举和 HealthTransition 字段守卫 | 仅当快照与转移对象字段覆盖设计约束并通过测试时完成 |
| HLT-TODO-006 | Done | 定义 RecoveryHint 数据结构 | health 设计 6.5/6.8；ADR-007 | 6.5 RecoveryHint；6.8 恢复动作 | L2 | infra/include/health/RecoveryHint.h | RecoveryHint{reason_code,severity,suggested_action,evidence_ref} | contract：不含执行句柄字段；unit：字段完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-005 | 已解阻（2026-03-31） | 已通过 RecoveryHintBoundaryContractTest 冻结最小边界模板 | 对象头文件、contract 测试；2026-03-31 已落盘 infra/include/health/RecoveryHint.h、tests/unit/infra/health/RecoveryHintTest.cpp、tests/contract/smoke/RecoveryHintBoundaryContractTest.cpp，并在 tests/contract/CMakeLists.txt 注册模板；通过 `cmake --build build-ci --target dasall_infra dasall_health_recovery_hint_unit_test dasall_contract_recovery_hint_boundary_test` 与 `ctest --test-dir build-ci --output-on-failure -R "RecoveryHintUnitTest|RecoveryHintBoundaryContractTest"` 验证 advisory 字段完整性与无执行句柄边界，且 `ctest --test-dir build-ci -N` 可发现 RecoveryHintUnitTest / RecoveryHintBoundaryContractTest | 仅当 contract 测试能阻止执行字段进入 RecoveryHint 时完成 |
| HLT-TODO-007 | Done (2026-04-06) | 实现 HealthMonitorFacade 生命周期骨架 | health 设计 6.2/6.7 | 6.2 HealthMonitorFacade；6.7 正常流程 | L3 | infra/src/health/HealthMonitorFacade.cpp | register_probe, evaluate_now, get_snapshot | unit：未初始化/已初始化路径；failure：safe_observe_mode | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-002、HLT-TODO-005 | 无 | 无 | Facade 私有头/源、单测；2026-04-06 已落盘 infra/src/health/HealthMonitorFacade.h、infra/src/health/HealthMonitorFacade.cpp、tests/unit/infra/health/HealthMonitorFacadeTest.cpp，并更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt 以注册 `HealthMonitorFacadeTest`；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_health_monitor_facade_unit_test`、`ctest --test-dir build-ci --output-on-failure -R HealthMonitorFacadeTest`、`ctest --test-dir build-ci -N -R HealthMonitorFacadeTest` 与 `cmake --build build-ci --target dasall_unit_tests` 验证未初始化/已初始化路径与 safe_observe_mode 失败语义，unit 标签 129/129 通过 | 仅当主入口路径可判定成功/失败且 safe_observe_mode 可触发时完成 |
| HLT-TODO-008 | Done (2026-04-06) | 实现 ProbeRegistry 注册治理骨架 | health 设计 6.2/6.3/6.7 | 6.2 ProbeRegistry；6.3 输入输出 | L3 | infra/src/health/ProbeRegistry.cpp | register_probe, unregister_probe, list_by_group | unit：重复注册拒绝、分组查询 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-004、HLT-TODO-007 | 无 | 无 | Registry 私有头/源、单测；2026-04-06 已落盘 infra/src/health/ProbeRegistry.h、infra/src/health/ProbeRegistry.cpp、tests/unit/infra/health/ProbeRegistryTest.cpp，并调整 infra/src/health/HealthMonitorFacade.h、infra/src/health/HealthMonitorFacade.cpp 与 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，使 façade 注册路径委托 `ProbeRegistry`；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_probe_registry_unit_test dasall_health_monitor_facade_unit_test`、`ctest --test-dir build-ci --output-on-failure -R "(ProbeRegistryTest|HealthMonitorFacadeTest)"`、`ctest --test-dir build-ci -N -R ProbeRegistryTest` 与 `cmake --build build-ci --target dasall_unit_tests` 验证重复注册拒绝、分组查询与 façade 回归，unit 标签 130/130 通过 | 仅当重复注册返回可判定失败且分组查询一致时完成 |
| HLT-TODO-009 | Not Started | 实现 ProbeScheduler 调度骨架 | health 设计 6.2/6.7；11.1；INT-TODO-026 | 6.2 ProbeScheduler；6.7 步骤 3 | L2 | infra/src/health/ProbeScheduler.cpp | start(periods), stop(), tick_once() | unit：周期触发与超时路由；failure：调度线程故障退化 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-008 | 无 | 已由 `platform/include/ITimer.h` 与 `docs/ssot/HealthCadenceAndEventBoundary.md` 冻结 timer seam 与 default cadence 基线 | 调度骨架或阻塞记录 | 仅当沿 `ITimer` seam 落盘并通过周期/超时测试时完成 |
| HLT-TODO-010 | Done (2026-04-06) | 实现 ProbeExecutor 执行骨架 | health 设计 6.2/6.7/6.8 | 6.2 ProbeExecutor；6.8 探针超时/异常 | L3 | infra/src/health/ProbeExecutor.cpp | execute_once(descriptor), execute_batch(group) | unit：超时/异常结构化返回；failure：探针持续失败计数 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-001、HLT-TODO-004、HLT-TODO-008 | 无 | 无 | Executor 私有头/源、单测；2026-04-06 已落盘 infra/src/health/ProbeExecutor.h、infra/src/health/ProbeExecutor.cpp、tests/unit/infra/health/ProbeExecutorTest.cpp，并更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt 以注册 `ProbeExecutorTest`；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_probe_executor_unit_test dasall_probe_registry_unit_test`、`ctest --test-dir build-ci --output-on-failure -R "(ProbeExecutorTest|ProbeRegistryTest)"`、`ctest --test-dir build-ci -N -R ProbeExecutorTest` 与 `cmake --build build-ci --target dasall_unit_tests` 验证超时/异常结构化返回、批量执行与连续失败计数，unit 标签 131/131 通过 | 仅当超时与异常都映射明确错误码且测试通过时完成 |
| HLT-TODO-011 | Done (2026-04-06) | 实现 HealthEvaluator 三态评估骨架 | health 设计 6.2/6.7/6.8/6.9 | 6.2 HealthEvaluator；6.8 异常分类；6.9 阈值配置 | L3 | infra/src/health/HealthEvaluator.cpp | evaluate(results), evaluate_transition(previous,current) | unit：Healthy/Degraded/Unhealthy 判定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-003、HLT-TODO-005、HLT-TODO-010 | profile 键命名未冻结（部分） | 阈值先按默认键实现，运行时覆盖后置 | Evaluator 私有头/源、单测；2026-04-06 已落盘 infra/src/health/HealthEvaluator.h、infra/src/health/HealthEvaluator.cpp、tests/unit/infra/health/HealthEvaluatorTest.cpp，并更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt 以注册 `HealthEvaluatorTest`；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_health_evaluator_unit_test`、`ctest --test-dir build-ci --output-on-failure -R HealthEvaluatorTest`、`ctest --test-dir build-ci -N -R HealthEvaluatorTest` 与 `cmake --build build-ci --target dasall_unit_tests` 验证 Healthy/Degraded/Unhealthy 判定与状态转移，unit 标签 132/132 通过 | 仅当三态判定与连续失败阈值行为可重复验证时完成 |
| HLT-TODO-012 | Blocked | 实现 HealthEventPublisher 状态事件发布骨架 | health 设计 6.2/6.8/6.10；11.1；INT-TODO-026 | 6.2 HealthEventPublisher；6.10 状态转移事件 | L2 | infra/src/health/HealthEventPublisher.cpp | publish_transition(from,to,reason), publish_probe_failure(result) | unit：仅状态变化时发布；failure：发布失败计数 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-005、HLT-TODO-011 | HLT-BLK-002 | 026 已冻结 event publish fallback；剩余 external bus 最小接口待 HLT-BLK-002 解阻 | 发布骨架或阻塞记录 | 仅当 event bus 最小接口冻结后可解除阻塞；冻结前必须遵守日志/指标/local cache fallback |
| HLT-TODO-013 | Done (2026-04-06) | 定义 HealthErrors 错误码域与映射 | health 设计 6.6；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/health/HealthErrors.h | INF_E_HEALTH_PROBE_TIMEOUT、INF_E_HEALTH_PROBE_EXCEPTION、INF_E_HEALTH_PROBE_NOT_FOUND、INF_E_HEALTH_POLICY_INVALID、INF_E_HEALTH_EVENT_PUBLISH_FAIL | contract：映射 contracts::ResultCode；unit：枚举稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-001、HLT-TODO-003 | 无 | 无 | 错误码头文件、unit/contract 测试；2026-04-06 已落盘 infra/include/health/HealthErrors.h、tests/unit/infra/health/HealthErrorsTest.cpp、tests/contract/smoke/HealthErrorMappingContractTest.cpp，并更新 infra/CMakeLists.txt、infra/src/health/ProbeRegistry.cpp、infra/src/health/ProbeExecutor.cpp、infra/src/health/HealthEvaluator.cpp、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt 与 tests/unit/infra/health/HealthEvaluatorTest.cpp，使 health 私有错误语义收敛到统一映射矩阵；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_health_errors_unit_test dasall_contract_health_error_mapping_test dasall_health_evaluator_unit_test dasall_probe_executor_unit_test`、`ctest --test-dir build-ci --output-on-failure -R "(HealthErrorsTest|HealthErrorMappingContractTest|HealthEvaluatorTest|ProbeExecutorTest)"`、`ctest --test-dir build-ci -N -R "(HealthErrorsTest|HealthErrorMappingContractTest)"` 与 `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests` 验证映射矩阵冻结、发现性与回归，unit 标签 133/133 通过，contract 标签 140/140 通过 | 仅当 5 个错误码可追溯且映射测试通过时完成 |
| HLT-TODO-014 | Not Started | 定义 HealthConfigPolicy 配置模型与覆盖策略 | health 设计 6.9/11.1；蓝图 5.1 | 6.9 配置项表 | L2 | infra/src/health/HealthConfigPolicy.cpp | merge(default/profile/deploy), validate_thresholds() | unit：默认值与覆盖优先级；failure：非法阈值拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-003、HLT-TODO-005 | 无 | 已于 2026-04-08 通过 PRF-TODO-022 冻结 infra.health.* 键域与覆盖优先级 | 配置策略代码或解阻记录；2026-04-08 已由 PRF-TODO-022 补齐五档 profile 资产与 schema contract | 仅当 HealthConfigPolicy 落盘并通过默认值/覆盖优先级/非法阈值测试时完成 |
| HLT-TODO-015 | Done (2026-04-06) | 实现 RecoveryHintEmitter 边界守卫骨架 | health 设计 6.2/6.8；ADR-007；11.1 | 6.2 RecoveryHintEmitter | L2 | infra/src/health/RecoveryHintEmitter.cpp | emit_hint(snapshot,reason), sanitize_hint_payload() | contract：建议与执行分离；unit：evidence_ref 完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-006、HLT-TODO-011 | 无 | 无 | 发射器代码、unit/contract 验证；2026-04-06 已落盘 infra/src/health/RecoveryHintEmitter.h、infra/src/health/RecoveryHintEmitter.cpp、tests/unit/infra/health/RecoveryHintEmitterTest.cpp，并更新 tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt 以注册 `RecoveryHintEmitterTest`；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_recovery_hint_emitter_unit_test dasall_contract_recovery_hint_boundary_test`、`ctest --test-dir build-ci --output-on-failure -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`、`ctest --test-dir build-ci -N -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`、`cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`、`ctest --test-dir build-ci --output-on-failure -L unit` 与 `ctest --test-dir build-ci --output-on-failure -L contract` 验证建议/执行分离、evidence_ref 完整性与回归，unit 标签 134/134 通过，contract 标签 140/140 通过 | 仅当 contract 模板冻结后方可推进 |
| HLT-TODO-016 | Done (2026-04-06) | 注册 health 源码到 infra CMake | health 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt、infra/src/health/ | health 源文件纳入 dasall_infra | build：dasall_infra 可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-001~015 | 无 | 无 | CMake 改动、构建记录；2026-04-06 已在 infra/CMakeLists.txt 新增 `DASALL_INFRA_HEALTH_SOURCES`、`DASALL_INFRA_HEALTH_PRIVATE_HEADERS` 与 `dasall_infra` 的 PRIVATE `src` include 路径，并同步调整 tests/unit/infra/CMakeLists.txt 使 health unit 目标不再直接编译 health 源文件；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_infra dasall_health_monitor_facade_unit_test dasall_probe_registry_unit_test dasall_probe_executor_unit_test dasall_health_evaluator_unit_test dasall_recovery_hint_emitter_unit_test`、`ctest --test-dir build-ci --output-on-failure -R "(HealthMonitorFacadeTest|ProbeRegistryTest|ProbeExecutorTest|HealthEvaluatorTest|RecoveryHintEmitterTest)"`、`cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`、`ctest --test-dir build-ci --output-on-failure -L unit` 与 `ctest --test-dir build-ci --output-on-failure -L contract` 验证 health 源码入图、health unit 去重与全量回归，unit 标签 134/134 通过，contract 标签 140/140 通过 | 仅当 placeholder 不再是唯一源码入口且 health 源码入图时完成 |
| HLT-TODO-017 | Done (2026-04-06) | 注册 health 的 unit/contract/integration 测试入口 | health 设计 7/8/9；工程规范 3.7；tests 现状 | 7 映射；8.1 路径；9.1 测试矩阵 | L0 | tests/unit/CMakeLists.txt、tests/unit/infra/health/、tests/contract/CMakeLists.txt、tests/integration/infra/health/ | unit/contract/failure 注入先行，integration 发现性门禁 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-016 | 无 | 无 | 测试注册改动、integration wiring smoke 与发现性证据；2026-04-06 已更新 tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt、tests/integration/CMakeLists.txt、tests/integration/infra/CMakeLists.txt，并新增 tests/integration/infra/health/CMakeLists.txt、tests/integration/infra/health/HealthWiringIntegrationTest.cpp，使 health unit/contract 测试统一挂载 `health` 标签并补齐最小 integration 入口；通过 `cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_health_wiring_integration_test`、`ctest --test-dir build-ci -N -L health`、`ctest --test-dir build-ci --output-on-failure -R HealthWiringIntegrationTest`、`ctest --test-dir build-ci --output-on-failure -L health`、`ctest --test-dir build-ci --output-on-failure -L unit` 与 `ctest --test-dir build-ci --output-on-failure -L contract` 验证 discoverability、health integration smoke 与全量回归，health 标签 17/17 通过，integration 1/1 通过，unit 标签 134/134 通过，contract 标签 140/140 通过 | 仅当 health 新增测试可被 ctest -N 发现；integration 用例可被发现并执行时完成 |
| HLT-TODO-018 | Done (2026-04-06) | 回写 health 质量门与交付证据 | health 设计 9.2/11；工程规范 6.2 | 9.2 Gate；11 阻塞与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md | process test：门禁结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-017 | 无 | 无 | 更新后的 TODO 文档证据段；2026-04-06 已同步修正 9.1 基线说明、10 风险与回退策略、11 可行性结论下一步口径，并新增本轮 gate/blocked/rollback 回写记录；通过 `ctest --test-dir build-ci -N`、`ctest --test-dir build-ci --output-on-failure -L unit` 与 `ctest --test-dir build-ci --output-on-failure -L contract` 复核 discoverability、unit/contract gate，总测试 290 可发现，unit 标签 134/134 通过，contract 标签 140/140 通过 | 仅当每个门禁有通过/失败结论及命令证据时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| HLT-TODO-012 | HLT-BLK-002 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | HLT-TODO-001~006 | 可并行（接口组与对象组并行） | 先稳定边界与字段语义 |
| B 主入口与注册执行 | HLT-TODO-007、HLT-TODO-008、HLT-TODO-010 | 串行 | Facade -> Registry -> Executor |
| C 评估与错误语义 | HLT-TODO-011、HLT-TODO-013 | 可并行 | 判定逻辑与错误映射分开推进 |
| D 受阻链路解阻推进 | HLT-TODO-009、012、014、015 | 串行（按阻塞项解锁） | 平台抽象/事件总线/Profile 键/contract 模板依赖明确 |
| E 构建与测试门禁 | HLT-TODO-016、HLT-TODO-017 | 可并行（integration 仍阻塞） | 先完成构建接线与 unit/contract 发现性 |
| F 证据收口 | HLT-TODO-018 | 串行 | 回写 gate 与阻塞变化证据 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| HLT-GATE-01 | 接口冻结门 | 进入实现任务前 | IHealthProbe/IHealthMonitor/IHealthPolicy 与对象头文件已落盘并可编译 | 回退到接口定义任务 |
| HLT-GATE-02 | 主链路闭环门 | 执行 HLT-TODO-011 前 | register->execute->evaluate 主链 unit 测试通过 | 回退 HLT-TODO-007/008/010 |
| HLT-GATE-03 | 错误语义门 | 执行 HLT-TODO-013 后 | 5 个健康错误码映射 contract 测试通过 | 补齐映射矩阵后重测 |
| HLT-GATE-04 | 事件边界门 | 推进 HLT-TODO-012 前 | event bus 最小接口冻结且仅状态变化发事件 | 未冻结则维持 Blocked |
| HLT-GATE-05 | RecoveryHint 边界门 | 推进 HLT-TODO-015 前 | contract 可阻断执行字段进入 RecoveryHint | 未通过则禁止推进发射器实现 |
| HLT-GATE-06 | 构建接线门 | 推进测试注册前 | dasall_infra 构建通过且 health 文件入图 | 修复 CMake 接线 |
| HLT-GATE-07 | 测试发现性门 | 提交前 | ctest -N 可见新增 health unit/contract | 修复 tests 注册 |
| HLT-GATE-08 | breaking 评审门 | 任意接口签名/错误映射变更前 | 评审结论明确风险、迁移窗口、回退方案 | 未评审不得推进 |
| HLT-GATE-09 | integration 准入门 | 进入 integration 任务前 | tests 顶层已完成 integration 接线并定义标签规范，且 health 组件用例已落盘 | 未通过前补齐 health integration 用例与注册 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| HLT-BLK-001 | 已解阻（2026-05-06）：`platform::ITimer` 最小接口已存在，且 `INT-TODO-026` 已冻结 `ProbeScheduler` 必须沿 `ITimer` seam 运转 | 无 | 无 | 证据回链到 `platform/include/ITimer.h` 与 `docs/ssot/HealthCadenceAndEventBoundary.md` | 若 `ITimer` seam 回退或被私有轮询替代，则重新转为 Blocked |
| HLT-BLK-002 | 事件总线发布接口未冻结 | HLT-TODO-012 | event publish 最小接口与 EventEnvelope 约束冻结 | 先定义最小 publish_transition API 与失败返回语义；026 已冻结 fallback rule | 先仅记录日志/指标并缓存状态转移，不对外发总线事件 |
| HLT-BLK-003 | 已解阻（2026-04-08）：PRF-TODO-022 已冻结 infra.health.* 键名与覆盖优先级，并在五档 profile 资产与 schema contract 中补齐 health 键域 | HLT-TODO-014、HLT-TODO-011（部分） | 无 | 证据回链到 profiles/*/runtime_policy.yaml、tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp 与 docs/architecture/DASALL_profiles模块详细设计.md | 若 profile 资产或 schema contract 回退，则重新转为 Blocked |
| HLT-BLK-004 | 已解阻（2026-03-31）：RecoveryHint 边界 contract 模板已冻结到 tests/contract/smoke/RecoveryHintBoundaryContractTest.cpp | HLT-TODO-015 | 无；后续可直接复用 RecoveryHintBoundaryContractTest | 证据回链到 infra/include/health/RecoveryHint.h、tests/unit/infra/health/RecoveryHintTest.cpp、tests/contract/CMakeLists.txt | 若建议/执行分离模板被回退，则重新转为 Blocked |
| HLT-BLK-005 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；health integration 是否可执行改由组件自身落盘负责 | HLT-TODO-017 及后续 integration 任务 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |

说明：

1. 截至 2026-04-06，health 已通过 HLT-TODO-017 落盘最小 integration wiring 用例；全组件通用基线仍以 unit/contract 为主，health integration 的 discoverability 与 smoke 结果在 HLT-TODO-017/018 证据中单独追踪。
2. 每项 Build-ready 任务至少包含 1 条构建命令与 1 条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而非仅任务标题：是。
2. 是否明确当前最细可达到粒度：是（L3/L2 混合，受阻项 L0）。
3. 是否所有任务具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项具备证据与解阻条件：是。
5. 是否所有任务具备可二值判定完成标准：是。
6. 是否避免跨子系统范围扩张：是。
7. 若要求函数/数据结构级，是否真正落到对象：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 边界越权风险 | High | health 直接触发恢复执行动作 | RecoveryHint 出现执行句柄字段或执行调用 | 立即回退到建议对象输出，仅保留 contract 守卫 |
| 探针雪崩风险 | High | 大量探针超时导致调度阻塞 | 超时计数持续上升、评估周期明显拉长 | 回退为同步 evaluate_now + 缩减关键探针组 |
| 状态抖动风险 | Medium | 阈值过小导致状态频繁跳变 | transition_total 高频抖动 | 回退为提高阈值与引入连续失败计数 |
| 事件发布失败被吞没 | High | 发布失败无计数与日志 | event_publish_fail_total 不增长但事件缺失 | 回退为日志+指标双写并阻断事件路径 |
| 配置漂移风险 | Medium | profile 键命名变更未评审 | 不同 profile 行为不一致 | 回退到默认键集合并禁用运行时覆盖 |
| integration 覆盖不足风险 | Medium | 当前只有最小 wiring smoke，blocked 链路尚未纳入 integration | `ctest -L health` 仅包含 `HealthWiringIntegrationTest` 作为 integration 用例 | 在 blocked 解阻前保留 unit/contract + minimal integration 双轨 gate；解阻后补 failure/profile/integration 用例 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3 为主），但部分任务需先解阻后再执行。
2. 原因：
   - 已有明确接口清单、对象字段、主/异常流程与错误码域。
   - 已有落盘路径、测试出口与 Design -> Build 映射。
   - 当前代码虽为空实现，但目录与 CMake 骨架已存在，可承载增量落盘。
   - 已识别并量化 5 项阻塞，解阻动作可最小执行。
   - ADR 边界对 RecoveryHint 与调度权归属约束清晰，可直接转为门禁断言。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构（L3），受阻链路为 L2/L0。
4. 未达到全量函数级的缺口：平台线程抽象、事件总线最小接口、profile 键命名，以及对应 blocked 链路的扩展 integration/failure/profile 用例。
5. 下一步建议：
   - 当前 health 可执行主链 `HLT-TODO-007/008/010/011/013/015/016/017/018` 已完成，可作为 watchdog 与 diagnostics 的事实输入基线。
   - 当前仅剩 `HLT-TODO-012` 一条 blocked 链路等待 `HLT-BLK-002` 解阻；`HLT-TODO-009` 已由 `INT-TODO-026` 解阻为 Not Started，`HLT-TODO-014` 已由 `PRF-TODO-022` 解阻，可直接进入配置实现。
   - 在解阻前维持当前回退口径：同步 `evaluate_now` 主链、默认阈值策略、无事件总线发布、minimal integration smoke。

## 12. 本轮执行记录（2026-04-06 / HLT-TODO-007）

### 12.1 选中任务

1. 本轮任务：HLT-TODO-007。
2. 可执行性依据：`HLT-TODO-002` 与 `HLT-TODO-005` 已完成，且 `HLT-TODO-007` 当前无 Blocked 依赖；当前代码缺口集中在 `HealthMonitorFacade` 生命周期骨架与最小 unit gate，而非 registry/executor/evaluator 主链实现。

### 12.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.2/6.7/6.8 已明确 `HealthMonitorFacade` 只负责统一入口、生命周期与查询；`safe_observe_mode` 需要“保留最近快照并拒绝新评估请求”，不能越权承担恢复执行。
2. infra/include/health/IHealthMonitor.h 已冻结 `register_probe`、`evaluate_now`、`get_snapshot`、`subscribe` 四个方法及 contracts `ResultCode/ErrorInfo` 错误边界，说明 007 只能在现有接口上补最小实现，不能改写 public API。
3. tests/unit/infra/ConfigCenterFacadeTest.cpp 与 tests/unit/infra/secret/SecretManagerFacadeTest.cpp 已给出当前仓库的私有实现模式：私有 façade 放在 infra/src，单测通过 `tests/unit/infra/CMakeLists.txt` 引入 `infra/src` 私有头进行验证。
4. 外部参考：Kubernetes 官方文档 `Liveness, Readiness, and Startup Probes` 指出 readiness 适用于初始化与临时故障期间的流量摘除，liveness 用于“应用无法继续前进”时的恢复判定；本轮据此保持 `safe_observe_mode` 仅阻断新评估、保留最近快照，而不在 health 内直接执行恢复动作。

D 结论：

1. `HealthMonitorFacade` 作为 infra/health 私有实现落盘在 infra/src/health，仅承接生命周期骨架，不提前吸收 `ProbeRegistry`、`ProbeExecutor`、`HealthEvaluator` 的职责。
2. 在 `HLT-TODO-008/010/011` 尚未完成前，`evaluate_now` 只生成占位 `HealthSnapshot`，用于固定主入口成功/失败语义与版本化快照出口，不提前执行真实探针。
3. `safe_observe_mode` 本轮只作为 façade 生命周期失败分支：拒绝新的 `evaluate_now`，同时保留最近一次成功快照供 `get_snapshot` 返回。
4. D Gate：PASS。

### 12.3 Build 交付与证据

交付物：

1. infra/src/health/HealthMonitorFacade.h、infra/src/health/HealthMonitorFacade.cpp：新增 `HealthMonitorFacade` 私有实现，落盘 `register_probe`、`evaluate_now`、`get_snapshot`、`subscribe` 及 `safe_observe_mode` 测试钩子。
2. tests/unit/infra/health/HealthMonitorFacadeTest.cpp：新增 unit 测试，覆盖未初始化失败、注册后成功求值、最近快照回读与 `safe_observe_mode` 失败语义。
3. tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：注册 `dasall_health_monitor_facade_unit_test` 与 `HealthMonitorFacadeTest`，保证 unit 标签可发现。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_health_monitor_facade_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R HealthMonitorFacadeTest`：通过，1/1 tests passed。
4. `ctest --test-dir build-ci -N -R HealthMonitorFacadeTest`：通过，发现 1 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests`：通过，unit 标签 129/129 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接补齐 `HealthMonitorFacade` 主入口生命周期骨架，而不是把 registry/executor/evaluator 提前混入 007。
2. 边界保持：实现停留在 `IHealthMonitor` 冻结接口和 contracts 错误边界内，未扩写 public headers，也未越权进入恢复执行链路。
3. 测试闭环：新增用例包含正例、负例与 `safe_observe_mode` 失败分支，并补了发现性验证。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-007` 的 health façade 实现、unit CMake/test 接线与 TODO/worklog 证据，不混入 `HLT-TODO-008`、`HLT-TODO-010`、`HLT-TODO-011`、`HLT-TODO-013`。

## 13. 本轮执行记录（2026-04-06 / HLT-TODO-008）

### 13.1 选中任务

1. 本轮任务：HLT-TODO-008。
2. 可执行性依据：`HLT-TODO-004` 与 `HLT-TODO-007` 已完成，当前仓库已有 `ProbeDescriptor`/`HealthProbeRegistration`/`HealthMonitorFacade` 最小骨架，缺口集中在“同名唯一、按组查询”的注册治理逻辑，而非调度/执行主链。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.2/6.3 已明确 `ProbeRegistry` 负责“管理探针注册、去重、分组和元信息”，并要求同名探针不可重复注册。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.5/6.9 已给出 `ProbeDescriptor` 字段与默认周期/超时配置：`group` 只允许 liveness/readiness，默认周期分别为 2000/5000 ms，单探针默认超时为 1000 ms。
3. infra/src/secret/SecretLeaseRegistry.h/.cpp 与 platform/include/linux/CapabilityRegistry.h、platform/src/linux/CapabilityRegistry.cpp 已给出当前仓库的 registry 模式：私有实现通过稳定结果对象暴露成功/失败语义，查询接口保持只读快照，不把治理逻辑散落到 façade 外层。
4. 外部参考：Kubernetes 官方文档 `Liveness, Readiness, and Startup Probes` 明确区分 liveness/readiness 的生命周期职责；本轮沿用该划分，把 registry 查询维持在分组层，不混入调度、执行或恢复语义。

D 结论：

1. `ProbeRegistry` 作为 infra/health 私有实现落盘在 infra/src/health，负责 `HealthProbeRegistration -> ProbeDescriptor` 的最小治理转换、同名唯一性校验、分组查询和 probe 查找。
2. 在 profile 键命名与 critical group 配置尚未冻结前，registry 仅为 `ProbeDescriptor` 补齐最小默认值：liveness/readiness 周期取 6.9 默认值，timeout 取 1000 ms，`criticality` 暂收敛为 `NonCritical`，避免伪造 profile 特定策略。
3. `HealthMonitorFacade` 本轮只接入 registry 的注册治理能力，仍不提前承担 executor/evaluator 语义。
4. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. infra/src/health/ProbeRegistry.h、infra/src/health/ProbeRegistry.cpp：新增 `ProbeRegistry` 私有实现，落盘 `register_probe`、`unregister_probe`、`list_by_group`、`find_descriptor`、`find_probe` 与结果对象。
2. infra/src/health/HealthMonitorFacade.h、infra/src/health/HealthMonitorFacade.cpp：将 façade 的注册存储从内部 map 重构为委托 `ProbeRegistry`，保持 007 生命周期边界不变。
3. tests/unit/infra/health/ProbeRegistryTest.cpp、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：新增 `ProbeRegistryTest` 并把 `ProbeRegistry.cpp` 纳入 `HealthMonitorFacadeTest` 的回归编译链路。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_probe_registry_unit_test dasall_health_monitor_facade_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "(ProbeRegistryTest|HealthMonitorFacadeTest)"`：通过，2/2 tests passed。
4. `ctest --test-dir build-ci -N -R ProbeRegistryTest`：通过，发现 1 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests`：通过，unit 标签 130/130 tests passed。

Build 合规复核：

1. 根因闭环：本轮把重复注册拒绝与分组查询从 façade 占位逻辑抽离到独立 registry，而不是在 executor/evaluator 中旁路补治理。
2. 边界保持：`ProbeRegistry` 仅依赖现有 health public headers 与 contracts 错误类型，未扩写 public API，也未越权承担调度或恢复职责。
3. 测试闭环：新增用例同时覆盖重复注册负例、按组查询正例、注销后一致性和 façade 回归。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-008` 的 registry 实现、与 façade 的直接对接、unit CMake/test 接线及 TODO/worklog 证据，不混入 `HLT-TODO-010`、`HLT-TODO-011`、`HLT-TODO-013`。

## 14. 本轮执行记录（2026-04-06 / HLT-TODO-010）

### 14.1 选中任务

1. 本轮任务：HLT-TODO-010。
2. 可执行性依据：`HLT-TODO-001`、`HLT-TODO-004`、`HLT-TODO-008` 已完成，当前仓库已经具备 `IHealthProbe`、`ProbeDescriptor/ProbeResult` 与 `ProbeRegistry`；缺口集中在同步执行、错误结构化返回和连续失败计数。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.2/6.7/6.8 已明确 `ProbeExecutor` 只负责执行 probe、隔离超时/异常并把结果写回窗口，不承担调度线程与 evaluator 职责。
2. `HLT-TODO-008` 已冻结 `ProbeRegistry` 的 descriptor/probe 查询能力，因此 010 可以复用 registry 做批量分组执行，而不再内嵌注册状态。
3. 截至 2026-04-06 当时，`HLT-TODO-009` 仍因线程/定时抽象阻塞而未开始，说明 010 只能采用同步执行骨架，以“执行时长后验判定 timeout”的方式固定错误语义，不能提前引入线程取消机制。
4. 外部参考：Kubernetes 官方文档 `Liveness, Readiness, and Startup Probes` 将 readiness/liveness 失败区分为临时故障与不可继续前进两类；本轮据此把单次 timeout/exception 先映射为 `Degraded`，连续失败达到阈值后再提升为 `Unhealthy`。

D 结论：

1. `ProbeExecutor` 作为 infra/health 私有实现落盘在 infra/src/health，通过 `ProbeRegistry` 查找 probe 并同步执行 `execute_once` / `execute_batch`。
2. timeout 采用执行后 `latency_ms > timeout_ms` 的后验判定，异常通过 catch 结构化为失败 `ProbeResult`；两类失败分别映射到 contracts `ProviderTimeout` 与 `ToolExecutionFailed`，为后续 013 的私有错误码域冻结保留稳定落点。
3. 执行结果统一绑定 `descriptor.probe_name`，并维护逐 probe 的连续失败计数；健康结果清零计数，失败达到默认 3 次后提升状态为 `Unhealthy`。
4. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/src/health/ProbeExecutor.h、infra/src/health/ProbeExecutor.cpp：新增 `ProbeExecutor` 私有实现，落盘同步执行、批量执行、timeout/exception 结构化返回与连续失败计数。
2. tests/unit/infra/health/ProbeExecutorTest.cpp：新增 unit 测试，覆盖 timeout、exception、批量执行与连续失败升级语义。
3. tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：注册 `dasall_probe_executor_unit_test` 与 `ProbeExecutorTest`。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_probe_executor_unit_test dasall_probe_registry_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "(ProbeExecutorTest|ProbeRegistryTest)"`：通过，2/2 tests passed。
4. `ctest --test-dir build-ci -N -R ProbeExecutorTest`：通过，发现 1 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests`：通过，unit 标签 131/131 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接补齐 probe 执行、timeout/exception 结构化返回与失败计数，而不是把执行逻辑散落到 façade 或 evaluator。
2. 边界保持：实现停留在同步执行骨架和既有 contracts 错误类型内，未越权进入 scheduler/blocker 009 的线程抽象范围。
3. 测试闭环：新增用例覆盖 timeout、exception、batch 和 repeated failure escalation，并保留对 `ProbeRegistry` 的回归验证。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-010` 的 executor 实现、unit CMake/test 接线与 TODO/worklog 证据，不混入 `HLT-TODO-011`、`HLT-TODO-013`。

## 15. 本轮执行记录（2026-04-06 / HLT-TODO-011）

### 15.1 选中任务

1. 本轮任务：HLT-TODO-011。
2. 可执行性依据：`HLT-TODO-003`、`HLT-TODO-005`、`HLT-TODO-010` 已完成，当前仓库已有 `IHealthPolicy`、`HealthSnapshot/HealthTransition` 与 `ProbeExecutor` 输出；当时 `HLT-BLK-003` 只影响 profile 键命名和运行时覆盖，因此本轮可按 6.9 默认阈值先落最小 evaluator（该 blocker 已于 2026-04-08 由 `PRF-TODO-022` 解阻）。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.2/6.7/6.8 已明确 `HealthEvaluator` 负责基于 probe 结果聚合出三态快照，并输出状态转移对象，不承担事件发布或恢复执行。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.9 已冻结默认阈值：`degraded.threshold=1`、`unhealthy.consecutive_failures=3`；当前 profile 键命名未冻结，因此 evaluator 只能先固化默认值，不引入运行时覆盖。
3. `HLT-TODO-010` 已让 repeated failure 在 `ProbeResult.status` 上体现为 `Unhealthy`，因此 evaluator 可以在不新增执行依赖的前提下，根据结果状态完成 Healthy/Degraded/Unhealthy 收敛。
4. 外部参考：Kubernetes 官方文档 `Liveness, Readiness, and Startup Probes` 将 readiness/liveness 失败区分为可降级与需恢复两类；本轮 evaluator 沿用这一原则，把单次失败聚合为 `Degraded`，把执行器已判定的 `Unhealthy` 结果聚合为 failed snapshot。

D 结论：

1. `HealthEvaluator` 作为 infra/health 私有实现落盘在 infra/src/health，实现 `IHealthPolicy::evaluate` 和 `evaluate_transition(previous,current)`，固定三态输出与状态转移边界。
2. 在 profile 键命名与 critical group 规则尚未冻结前，evaluator 采用默认策略：任一 `Unhealthy` 结果触发 failed snapshot；否则只要失败计数达到默认 degraded threshold 即输出 degraded snapshot；全健康则输出 ready snapshot。
3. evaluator 只消费 `ProbeResultView`，不反向依赖 registry/executor 实现类，也不提前接入事件总线或状态存储。
4. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/src/health/HealthEvaluator.h、infra/src/health/HealthEvaluator.cpp：新增 `HealthEvaluator` 私有实现，落盘默认三态聚合、稳定 `policy_version()` 与 `evaluate_transition`。
2. tests/unit/infra/health/HealthEvaluatorTest.cpp：新增 unit 测试，覆盖 invalid input 失败、Healthy/Degraded/Unhealthy 判定与状态转移输出。
3. tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：注册 `dasall_health_evaluator_unit_test` 与 `HealthEvaluatorTest`。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_health_evaluator_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R HealthEvaluatorTest`：通过，1/1 tests passed。
4. `ctest --test-dir build-ci -N -R HealthEvaluatorTest`：通过，发现 1 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests`：通过，unit 标签 132/132 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接补齐健康三态聚合与状态转移骨架，而不是把判定逻辑散落在 executor 或 façade 中。
2. 边界保持：实现停留在 `IHealthPolicy` 冻结接口和默认阈值策略内，未越权进入 profile 覆盖、event publisher 或 recovery hint 领域。
3. 测试闭环：新增用例覆盖 invalid input、三态分类和 transition 输出，并补了发现性验证。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-011` 的 evaluator 实现、unit CMake/test 接线与 TODO/worklog 证据，不混入 `HLT-TODO-013`。

## 16. 本轮执行记录（2026-04-06 / HLT-TODO-013）

### 16.1 选中任务

1. 本轮任务：HLT-TODO-013。
2. 可执行性依据：`HLT-TODO-001` 与 `HLT-TODO-003` 已完成，`HLT-TODO-010/011` 已把 probe 执行失败与策略失败路径稳定到 `ProbeExecutor`/`HealthEvaluator`，当前缺口集中在“health 私有错误码域是否成文并映射到 contracts 错误类别”。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.6 已明确 5 个 health 私有错误语义：`INF_E_HEALTH_PROBE_TIMEOUT`、`INF_E_HEALTH_PROBE_EXCEPTION`、`INF_E_HEALTH_PROBE_NOT_FOUND`、`INF_E_HEALTH_POLICY_INVALID`、`INF_E_HEALTH_EVENT_PUBLISH_FAIL`，说明本轮需要冻结的是本地错误域与映射关系，而不是扩写 contracts 共享枚举。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.8 已把 probe timeout、probe exception、policy invalid 与 event publish failure 列为结构化失败出口，因此 013 不能只写常量表，还要把当前 executor/evaluator/registry 失败路径接到统一映射矩阵。
3. infra/include/error/ResultCode.h 与现有 metrics/config/policy 错误映射 contract 测试模式表明：私有错误域应通过 unit/contract 双测试固化“本地码名稳定 + contracts 结果码稳定”，避免后续实现随意漂移。
4. `HLT-TODO-012` 仍因事件总线接口未冻结而阻塞，因此 `INF_E_HEALTH_EVENT_PUBLISH_FAIL` 本轮只冻结到公开头文件和 contract 映射，不提前伪造 publisher 实现。

D 结论：

1. `HealthErrors.h` 作为 health 公共头落盘到 infra/include/health，冻结 health 私有错误枚举、稳定名称和 `contracts::ResultCode` 映射矩阵。
2. `ProbeRegistry`、`ProbeExecutor`、`HealthEvaluator` 现有失败路径统一改为消费 `map_health_error_code(...)`，确保 6.6/6.8 中已成文的错误语义在真实代码路径可观测，而不是停留在文档常量表。
3. 通过 unit 测试冻结枚举值/名称，通过 contract 测试冻结映射矩阵与 source anchor；`EventPublishFail` 仅冻结名称与映射，不越权补事件发布实现。
4. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. infra/include/health/HealthErrors.h、infra/CMakeLists.txt：新增 `HealthErrorCode`、`HealthErrorMapping`、`health_error_code_name`、`map_health_error_code`，并把公共头纳入 `dasall_infra` public header 集合。
2. infra/src/health/ProbeRegistry.cpp、infra/src/health/ProbeExecutor.cpp、infra/src/health/HealthEvaluator.cpp：将 missing probe、probe timeout、probe exception、policy invalid 等失败路径统一改为走 `HealthErrors` 映射矩阵。
3. tests/unit/infra/health/HealthErrorsTest.cpp、tests/contract/smoke/HealthErrorMappingContractTest.cpp、tests/unit/infra/health/HealthEvaluatorTest.cpp、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：新增 unit/contract 冻结测试，并同步更新 evaluator 回归断言与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_health_errors_unit_test dasall_contract_health_error_mapping_test dasall_health_evaluator_unit_test dasall_probe_executor_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "(HealthErrorsTest|HealthErrorMappingContractTest|HealthEvaluatorTest|ProbeExecutorTest)"`：通过，4/4 tests passed。
4. `ctest --test-dir build-ci -N -R "(HealthErrorsTest|HealthErrorMappingContractTest)"`：通过，发现 2 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；后续全量执行结果为 unit 标签 133/133 tests passed、contract 标签 140/140 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接冻结 health 私有错误码域与 contracts 映射矩阵，并把真实失败路径接入统一映射，而不是继续在 executor/evaluator 内散落硬编码错误码。
2. 边界保持：实现停留在 infra/health 本地错误域与既有 contracts `ResultCode` 之间，未扩写 contracts 共享枚举，也未越权补 `HLT-TODO-012` 的事件发布实现。
3. 测试闭环：新增用例同时覆盖枚举值稳定、名称稳定、映射矩阵稳定、source anchor 可观察性，以及 evaluator/executor 的回归路径，并补了发现性验证。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-013` 的错误码域、映射接线、unit/contract 测试与 TODO/worklog 证据，不混入后续 `HLT-TODO-015` 或解阻任务。

## 17. 本轮执行记录（2026-04-06 / HLT-TODO-015）

### 17.1 选中任务

1. 本轮任务：HLT-TODO-015。
2. 可执行性依据：`HLT-TODO-006` 已冻结 `RecoveryHint` 边界对象与 contract 模板，`HLT-TODO-011` 已提供可消费的三态 `HealthSnapshot` 输出，因此 015 当前无 Blocked 依赖，可以直接补齐“只发建议、不带执行句柄”的边界守卫骨架。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 6.3/6.4 已明确 `RecoveryHintEmitter` 的输入是 Degraded/Failed 快照，输出是建议事件 `RecoveryHint`，且依赖方向是 `HealthEvaluator -> RecoveryHintEmitter`，不允许反向进入 runtime/cognition 实现。
2. docs/architecture/DASALL_infra_health模块详细设计.md 6.5 已冻结 `RecoveryHint{reason_code,severity,suggested_action,evidence_ref}`，明确“仅建议，不包含执行句柄”；docs/architecture/DASALL_infra_health模块详细设计.md 6.8 进一步要求持续失败路径“输出 RecoveryHint”，但不执行恢复动作。
3. tests/contract/smoke/RecoveryHintBoundaryContractTest.cpp 已将 `RecoveryHint` 的 advisory-only 边界固化为 contract，因此 015 应复用既有模板，并把实现重点放在建议级别、reason_code 和 `evidence_ref` 的结构化输出，而不是重复扩写对象字段。
4. 外部参考：Kubernetes `Liveness, Readiness, and Startup Probes` 文档指出探针负责持续报告状态，而 kubelet 根据 probe 结果执行重启或摘流等动作；该分层说明“状态信号输出”和“恢复动作执行”应保持分离，本轮据此把 `RecoveryHintEmitter` 限制为 advisory output，不承担执行动作。

D 结论：

1. `RecoveryHintEmitter` 作为 infra/health 私有实现落盘在 infra/src/health，提供 `emit_hint(snapshot, reason)` 与 `sanitize_hint_payload()`，只接受 `Degraded` / `Unhealthy` 快照，拒绝 `Healthy`/`Unknown` 输入。
2. 建议对象沿用已冻结的 `RecoveryHint`：degraded 快照映射为 `ProviderTimeout + Warning + observe_and_retry_later`，unhealthy 快照映射为 `RuntimeRetryExhausted + Critical + escalate_for_runtime_recovery_review`；建议动作仅为字符串建议，不引入任何执行句柄或 runtime 回调。
3. `evidence_ref` 统一锚定到 `audit://health/recovery_hint/`，并纳入状态、snapshot version、failed_components 与 sanitize 后的 reason，确保后续 audit/publisher 接线前已经具备稳定审计锚点。
4. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. infra/src/health/RecoveryHintEmitter.h、infra/src/health/RecoveryHintEmitter.cpp：新增 `RecoveryHintEmitter` 私有实现，落盘 `RecoveryHintEmissionResult`、`emit_hint`、`sanitize_hint_payload` 与基于 snapshot state 的建议级别/证据锚点生成逻辑。
2. tests/unit/infra/health/RecoveryHintEmitterTest.cpp：新增 unit 测试，覆盖 degraded hint、unhealthy hint 与 healthy snapshot 拒绝路径，同时固定 `evidence_ref` 前缀与 sanitize 行为。
3. tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt：注册 `dasall_recovery_hint_emitter_unit_test` 与 `RecoveryHintEmitterTest`，保证 unit 标签可发现。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_recovery_hint_emitter_unit_test dasall_contract_recovery_hint_boundary_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`：通过，2/2 tests passed。
4. `ctest --test-dir build-ci -N -R "(RecoveryHintEmitterTest|RecoveryHintBoundaryContractTest)"`：通过，发现 2 个目标测试。
5. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；后续全量执行结果为 unit 标签 134/134 tests passed、contract 标签 140/140 tests passed。
6. `ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`：通过。

Build 合规复核：

1. 根因闭环：本轮直接补齐 RecoveryHint 的结构化建议输出与 evidence_ref 守卫，而不是把建议逻辑散落到 evaluator 或未来 runtime 恢复链路中。
2. 边界保持：实现停留在 `HealthSnapshot -> RecoveryHint` 的 advisory-only 映射内，未扩写 `RecoveryHint` 公共对象，也未越权补执行动作或 runtime 回调。
3. 测试闭环：新增用例覆盖正例、负例和 sanitize 路径，并复用了已冻结的 `RecoveryHintBoundaryContractTest` 做边界守卫验证。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-015` 的 emitter 实现、unit 接线与 TODO/worklog 证据，不混入后续 `HLT-TODO-016`、`HLT-TODO-017`、`HLT-TODO-018`。

## 18. 本轮执行记录（2026-04-06 / HLT-TODO-016）

### 18.1 选中任务

1. 本轮任务：HLT-TODO-016。
2. 可执行性依据：`HLT-TODO-001~015` 已完成，health 私有源码已经全部落盘于 infra/src/health，但仍停留在“单测直编源文件、库目标未入图”的过渡状态；016 当前无 Blocked 依赖，且完成标准可以通过 `dasall_infra` 构建与 health 回归测试二值判定。

### 18.2 研究与 Design 结论

本地证据：

1. infra/CMakeLists.txt 当前只把 health 公共头纳入 `DASALL_INFRA_PUBLIC_HEADERS`，并未把 `HealthMonitorFacade`、`ProbeRegistry`、`ProbeExecutor`、`HealthEvaluator`、`RecoveryHintEmitter` 纳入 `dasall_infra` 源文件集合，说明 health 仍处于“源码未入图”的临时状态。
2. tests/unit/infra/CMakeLists.txt 里 health 单测当前通过 `${CMAKE_SOURCE_DIR}/infra/src/health/*.cpp` 直接编译实现文件；如果 016 只把 health 源码加入 `dasall_infra` 而不调整 unit 目标，就会形成重复符号与双路径编译风险。
3. docs/architecture/DASALL_infra_health模块详细设计.md 8.1 已将 `infra/src/health/` 列为正式落盘目录，并把 health 测试门禁映射到独立目录和聚合 gate，说明 016 的职责是把这些已存在源码从“测试私用”提升为“库内正式成员”。
4. 外部参考：CMake `target_sources` 与 `target_include_directories` 文档都强调源码和 include 路径应通过目标级 `PRIVATE/PUBLIC` 作用域附着到目标自身，而不是由下游目标重复携带实现文件；本轮据此把 health 源码统一挂到 `dasall_infra`，并把 `infra/src` 作为库的 PRIVATE include 路径，只保留 health 单测对私有头的 include 能力。

D 结论：

1. 在 infra/CMakeLists.txt 中新增 `DASALL_INFRA_HEALTH_SOURCES` 与 `DASALL_INFRA_HEALTH_PRIVATE_HEADERS`，把 health 私有实现和私有头统一并入 `dasall_infra` 的 `target_sources(PRIVATE ...)`。
2. 为了让 `#include "health/..."` 的私有头路径在库内可解析，需要为 `dasall_infra` 增加 PRIVATE `src` include 路径，而不是扩散到 PUBLIC 接口面。
3. tests/unit/infra/CMakeLists.txt 中 health 相关 unit 目标改为“只编测试文件 + link `dasall_infra`”，保留 `${CMAKE_SOURCE_DIR}/infra/src` 作为测试目标 PRIVATE include 路径，以读取私有头但不再重复编译实现。
4. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. infra/CMakeLists.txt：新增 `DASALL_INFRA_HEALTH_SOURCES`、`DASALL_INFRA_HEALTH_PRIVATE_HEADERS`，并把 `infra/src` 作为 `dasall_infra` 的 PRIVATE include 路径，使 health 私有源码正式入图。
2. tests/unit/infra/CMakeLists.txt：将 `dasall_health_monitor_facade_unit_test`、`dasall_probe_registry_unit_test`、`dasall_probe_executor_unit_test`、`dasall_health_evaluator_unit_test`、`dasall_recovery_hint_emitter_unit_test` 从“直编实现文件”改为“只编测试文件并链接 `dasall_infra`”。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_health_monitor_facade_unit_test dasall_probe_registry_unit_test dasall_probe_executor_unit_test dasall_health_evaluator_unit_test dasall_recovery_hint_emitter_unit_test`：通过。
3. `ctest --test-dir build-ci --output-on-failure -R "(HealthMonitorFacadeTest|ProbeRegistryTest|ProbeExecutorTest|HealthEvaluatorTest|RecoveryHintEmitterTest)"`：通过，5/5 tests passed。
4. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；后续全量执行结果为 unit 标签 134/134 tests passed、contract 标签 140/140 tests passed。
5. `ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`：通过。

Build 合规复核：

1. 根因闭环：本轮直接消除了“health 源码未入 `dasall_infra`”和“health 单测重复编译实现文件”两个根因，而不是继续叠加新的测试目标或临时 include 路径。
2. 边界保持：`infra/src` 只作为 `dasall_infra` 的 PRIVATE include 路径引入，没有把 health 私有头暴露到 PUBLIC 接口面。
3. 测试闭环：定向 health 构建回归和全量 unit/contract gate 均通过，证明源码入图没有破坏既有库或边界测试。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-016` 的 CMake 接线与 unit 去重，不混入后续 `HLT-TODO-017`、`HLT-TODO-018` 的 integration/文档收口。

## 19. 本轮执行记录（2026-04-06 / HLT-TODO-017）

### 19.1 选中任务

1. 本轮任务：HLT-TODO-017。
2. 可执行性依据：`HLT-TODO-016` 已完成，health 私有源码已经正式入 `dasall_infra`；当前缺口不再是构建接线，而是“现有 health unit/contract 测试缺少统一标签，integration 子目录与最小 wiring smoke 尚未落盘”。

### 19.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 7/8.1/9.1 已把 health 测试矩阵拆成 unit、contract、integration 三层，并明确 `infra/src/health/` 的落盘实现需要对应的测试目录与发现性 gate。
2. tests/integration/CMakeLists.txt 与 tests/integration/infra/CMakeLists.txt 已完成顶层 integration 拓扑接线，且 audit/policy/secret 组件都采用“组件子目录 + register function + integration;<component> 标签”的统一模式，说明 017 只需按现有拓扑补齐 health 子目录，不需要再改顶层架构。
3. tests/unit/infra/CMakeLists.txt 与 tests/contract/CMakeLists.txt 中 health 相关测试已经存在，但此前只有 `unit` 或 `contract;smoke` 标签，缺少统一的 `health` 标签，因此无法通过 `ctest -L health` 做组件级发现性与回归。
4. 外部参考：CTest `add_test` / `set_tests_properties(... LABELS ...)` 约定表明，组件级 discoverability 最稳定的做法是统一标签而不是依赖测试名正则；本轮据此把 health unit/contract/integration 全部收敛到 `health` 标签，并新增最小 integration smoke 覆盖 registry -> executor -> evaluator -> recovery hint 的可执行主链。

D 结论：

1. 017 不新增生产能力，只补 health 的测试注册、标签收敛与最小 integration wiring，用来证明当前 health 主链已可在测试层被统一发现和执行。
2. 现有 health unit 测试与 contract 边界测试统一追加 `health` 标签，保持原有 `unit`、`contract`、`smoke` 标签不变，避免影响仓库既有全量 gate。
3. 新增 tests/integration/infra/health/ 目录与 `HealthWiringIntegrationTest`，在不引入新生产代码的前提下验证已落盘的 `ProbeRegistry`、`ProbeExecutor`、`HealthEvaluator` 与 `RecoveryHintEmitter` 可组合出最小可执行主链。
4. D Gate：PASS。

### 19.3 Build 交付与证据

交付物：

1. tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：为现有 health unit/contract 测试补齐 `health` 标签，形成统一组件级 discoverability 入口。
2. tests/integration/CMakeLists.txt、tests/integration/infra/CMakeLists.txt：把 `dasall_health_wiring_integration_test` 纳入 integration 聚合目标，并接入 `tests/integration/infra/health/` 子目录。
3. tests/integration/infra/health/CMakeLists.txt、tests/integration/infra/health/HealthWiringIntegrationTest.cpp：新增 health integration 注册函数与 wiring smoke，用最小合成 probe 验证 all-healthy snapshot 和 repeated failure -> recovery hint 两条主链。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_health_wiring_integration_test`：通过。
3. `ctest --test-dir build-ci -N -L health`：通过，发现 17 个带 `health` 标签的 unit/contract/integration 测试。
4. `ctest --test-dir build-ci --output-on-failure -R HealthWiringIntegrationTest`：通过，1/1 tests passed。
5. `ctest --test-dir build-ci --output-on-failure -L health`：通过，health 标签 17/17 tests passed。
6. `ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`：通过；unit 标签 134/134 tests passed，contract 标签 140/140 tests passed。

Build 合规复核：

1. 根因闭环：本轮直接补齐 health 组件级 discoverability 与 integration wiring，而不是继续依赖零散测试名或让 integration 拓扑停留在顶层占位状态。
2. 边界保持：新增 integration 用例只复用既有 health 私有实现和 public contracts，不扩写新的生产能力，也未越权碰 `HLT-TODO-009/012/014` 的 blocked 领域。
3. 测试闭环：同时证明了三件事：health 标签可统一发现、integration 用例可独立执行、全量 unit/contract gate 未受影响。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-017` 的测试注册、标签收敛、integration wiring smoke 与 TODO/worklog 证据，不混入后续 `HLT-TODO-018` 的质量门收口。

## 20. 本轮执行记录（2026-04-06 / HLT-TODO-018）

### 20.1 选中任务

1. 本轮任务：HLT-TODO-018。
2. 可执行性依据：`HLT-TODO-017` 已完成，health 主链、错误语义、建议输出、源码入图、测试发现性与最小 integration wiring 均已落盘；当前唯一缺口是把 gate 结果、blocked 现状与回退边界统一收口到 health 专项 TODO。

### 20.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_health模块详细设计.md 9.2 要求 gate 可验证、可回写，11 章要求阻塞项、风险与回退策略具备持续更新的证据面，因此 018 的职责是证据收口，而不是继续新增实现代码。
2. `HLT-TODO-007/008/010/011/013/015/016/017` 已分别完成 façade、registry、executor、evaluator、error mapping、recovery hint、CMake 接线和测试 discoverability，说明 018 需要把这些结果映射回 `HLT-GATE-01/02/03/05/06/07/09`，同时保留 `HLT-GATE-04` 对应 event bus blocked 状态。
3. 本轮 process tests 已复核 `ctest --test-dir build-ci -N`、`ctest --test-dir build-ci --output-on-failure -L unit` 与 `ctest --test-dir build-ci --output-on-failure -L contract`，当前总 discoverability 为 290 个测试，unit gate 134/134 通过，contract gate 140/140 通过。
4. 截至 2026-04-06 当时，`HLT-BLK-001/002/003` 仍未解阻，分别对应 scheduler、event publisher、config policy 三条链路；因此 018 需要明确“当前可执行主链已闭环，但 blocked 链路仍保持回退口径”。

D 结论：

1. 018 只更新 docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md 与开发执行记录，不新增任何生产代码或测试实现。
2. gate 回写按二值结论收口：`HLT-GATE-01/02/03/05/06/07/09 = PASS`，`HLT-GATE-04` 仍受 `HLT-BLK-002` 约束而维持 blocked 前置口径，`HLT-GATE-08` 本轮未触发 breaking change 评审条件。
3. 风险与回退口径同步修正到当前状态：integration 已不再“缺失”，但仍属于 minimal smoke；blocked 链路继续回退到同步评估、默认阈值和无事件发布路径。
4. D Gate：PASS。

### 20.3 Build 交付与证据

交付物：

1. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md：更新 `HLT-TODO-018` 状态，修正 9.1 基线说明、10 风险与回退策略、11 当前下一步口径，并追加本轮执行记录。
2. docs/worklog/DASALL_开发执行记录.md：新增记录 #128，回写本轮 gate 复核、blocked 台账与后续推进建议。

验收结果：

1. `ctest --test-dir build-ci -N`：通过，总 discoverability 为 290 个测试。
2. `ctest --test-dir build-ci --output-on-failure -L unit`：通过，unit 标签 134/134 tests passed。
3. `ctest --test-dir build-ci --output-on-failure -L contract`：通过，contract 标签 140/140 tests passed。

Gate/Blocked 收口结论：

1. `HLT-GATE-01`：PASS。health 接口与对象冻结已由 `HLT-TODO-001~006` 落盘并保持可编译。
2. `HLT-GATE-02`：PASS。`HLT-TODO-007/008/010/011` 已证明 register -> execute -> evaluate 主链可运行且回归通过。
3. `HLT-GATE-03`：PASS。`HLT-TODO-013` 已冻结 5 个 health 错误码与 contracts 映射，contract 回归通过。
4. `HLT-GATE-04`：维持 blocked 前置。`HLT-TODO-012` 仍受 `HLT-BLK-002` 限制，事件总线最小接口未冻结。
5. `HLT-GATE-05`：PASS。`HLT-TODO-015` 与 `RecoveryHintBoundaryContractTest` 已守住 advisory-only 边界。
6. `HLT-GATE-06`：PASS。`HLT-TODO-016` 已把 health 源码纳入 `dasall_infra`，health unit 去重成功。
7. `HLT-GATE-07`：PASS。`HLT-TODO-017` 已让 health 测试通过 `ctest -N -L health` 与 `ctest -L health` 统一发现和执行。
8. `HLT-GATE-08`：本轮未触发。015~018 未引入新的接口签名或错误映射 breaking change。
9. `HLT-GATE-09`：PASS。health integration 子目录与 `HealthWiringIntegrationTest` 已落盘并可执行。
10. 截至 2026-04-06 当时未解阻台账为：`HLT-TODO-009 -> HLT-BLK-001`、`HLT-TODO-012 -> HLT-BLK-002`、`HLT-TODO-014 -> HLT-BLK-003`；其中 `HLT-TODO-014 -> HLT-BLK-003` 已于 2026-04-08 由 `PRF-TODO-022` 解阻。

Build 合规复核：

1. 根因闭环：本轮没有继续追加实现代码，而是把已完成主链的 gate、blocked 和 fallback 证据统一收口，避免 health TODO 留下过时口径。
2. 边界保持：文档明确维持当前回退策略，不因为主链完成就越权标记 event/config blocked 链路为 done。
3. 证据闭环：discoverability、unit、contract 三类 process tests 都已重跑并写回；017 的 health 标签与 integration smoke 结果也已被纳入本轮结论。
4. 提交隔离：本轮提交范围限定为 `HLT-TODO-018` 的文档质量门收口与 worklog 证据，不混入新的代码或测试实现。
