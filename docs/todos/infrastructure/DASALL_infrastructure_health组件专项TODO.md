# DASALL infrastructure 子系统 health 组件专项 TODO

最近更新时间：2026-03-31  
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
| infra/include/ | 空目录 | health 接口未落盘 |
| infra/src/health/ | 空目录 | health 实现未落盘 |
| infra/CMakeLists.txt | 仅 src/placeholder.cpp | health 未接入构建 |
| tests/CMakeLists.txt | 仅 mocks/unit/contract | integration 顶层未接入 |
| tests/unit/CMakeLists.txt | 未接入 infra 子目录 | health unit 发现性缺失 |
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
| 配置与 Profile 裁剪 | health 设计 6.9 | 配置 | HLT-TODO-014、HLT-BLK-003 | 键命名未冻结，先补设计再实现 |
| RecoveryHint 边界守卫 | health 设计 6.8；ADR-007 | 适配器/边界 | HLT-TODO-015、HLT-BLK-004 | 明确建议与执行分离并 contract 化 |
| CMake 与测试门禁接线 | health 设计 7/8/9；代码现状 | 测试/门禁 | HLT-TODO-016、HLT-TODO-017、HLT-BLK-005 | 构建和 unit/contract 可先做，integration 先阻塞 |
| 文档与证据回写 | health 设计 9.2/11 | 文档/交付证据 | HLT-TODO-018 | 对 gate、阻塞、回退证据做收口 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | HLT-TODO-001~003 |
| 数据结构定义类任务 | 是 | HLT-TODO-004~006 |
| 生命周期与初始化类任务 | 是 | HLT-TODO-007 |
| 适配器/桥接类任务 | 是 | HLT-TODO-015 |
| 异常与错误处理类任务 | 是 | HLT-TODO-012~013 |
| 配置与 Profile 裁剪类任务 | 是 | HLT-TODO-014（含 HLT-BLK-003） |
| 测试与门禁类任务 | 是 | HLT-TODO-016~017（含 HLT-BLK-005） |
| 文档/交付证据回写类任务 | 是 | HLT-TODO-018 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HLT-TODO-001 | Done | 定义 IHealthProbe 接口头文件 | health 设计 6.6；编码规范 3.7 | 6.6 IHealthProbe | L3 | infra/include/health/IHealthProbe.h | probe(): ProbeResult | unit：接口可编译；contract：错误语义入口可映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | 无 | 无 | 无 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/health/IHealthProbe.h、tests/unit/infra/health/HealthProbeInterfaceTest.cpp，并通过 `cmake --build build-ci --target dasall_infra dasall_unit_tests` 与 `ctest --test-dir build-ci --output-on-failure -R HealthProbeInterfaceTest` 验证接口签名可编译且未吸收 monitor 职责 | 仅当接口签名与 6.6 一致且不依赖业务实现时完成 |
| HLT-TODO-002 | Not Started | 定义 IHealthMonitor 接口头文件 | health 设计 6.6/6.7 | 6.6 IHealthMonitor | L3 | infra/include/health/IHealthMonitor.h | register_probe(name,group,probe), evaluate_now(), get_snapshot(), subscribe(listener) | unit：接口可编译；contract：快照边界不越权 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-001 | listener 类型细节未冻结 | 先冻结最小 listener 抽象占位 | 接口头文件、编译记录 | 仅当四个方法语义与 6.6 一致且可编译时完成 |
| HLT-TODO-003 | Not Started | 定义 IHealthPolicy 接口头文件 | health 设计 6.6/6.9 | 6.6 IHealthPolicy；6.9 策略配置 | L3 | infra/include/health/IHealthPolicy.h | evaluate(results): HealthSnapshot | unit：接口可编译；unit：阈值输入输出可约束 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-002 | policy version 规范未成文 | 先用字符串版本占位并保留扩展点 | 接口头文件、编译记录 | 仅当策略接口可承载三态评估输入输出时完成 |
| HLT-TODO-004 | Not Started | 定义 ProbeTypes 数据结构 | health 设计 6.5 | 6.5 ProbeDescriptor/ProbeResult | L3 | infra/include/health/ProbeTypes.h | ProbeDescriptor, ProbeResult | unit：字段完整性与状态枚举覆盖 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-001 | 无 | 无 | 数据结构头文件、单测 | 仅当字段与状态集合与 6.5 一致且默认语义可测试时完成 |
| HLT-TODO-005 | Not Started | 定义 HealthStateTypes 数据结构 | health 设计 6.5/6.7 | 6.5 HealthSnapshot/HealthTransition | L3 | infra/include/health/HealthStateTypes.h | HealthSnapshot, HealthTransition | unit：version 单调与状态转移字段校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-004 | 历史持久化策略未冻结 | 先仅实现进程内窗口语义 | 数据结构头文件、单测 | 仅当快照与转移对象字段覆盖设计约束并通过测试时完成 |
| HLT-TODO-006 | Not Started | 定义 RecoveryHint 数据结构 | health 设计 6.5/6.8；ADR-007 | 6.5 RecoveryHint；6.8 恢复动作 | L2 | infra/include/health/RecoveryHint.h | RecoveryHint{reason_code,severity,suggested_action,evidence_ref} | contract：不含执行句柄字段；unit：字段完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-005 | contract 模板缺失 | 先补最小边界断言模板 | 对象头文件、contract 测试 | 仅当 contract 测试能阻止执行字段进入 RecoveryHint 时完成 |
| HLT-TODO-007 | Not Started | 实现 HealthMonitorFacade 生命周期骨架 | health 设计 6.2/6.7 | 6.2 HealthMonitorFacade；6.7 正常流程 | L3 | infra/src/health/HealthMonitorFacade.cpp | register_probe, evaluate_now, get_snapshot | unit：未初始化/已初始化路径；failure：safe_observe_mode | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-002、HLT-TODO-005 | 无 | 无 | Facade 骨架、单测 | 仅当主入口路径可判定成功/失败且 safe_observe_mode 可触发时完成 |
| HLT-TODO-008 | Not Started | 实现 ProbeRegistry 注册治理骨架 | health 设计 6.2/6.3/6.7 | 6.2 ProbeRegistry；6.3 输入输出 | L3 | infra/src/health/ProbeRegistry.cpp | register_probe, unregister_probe, list_by_group | unit：重复注册拒绝、分组查询 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-004、HLT-TODO-007 | 无 | 无 | Registry 骨架、单测 | 仅当重复注册返回可判定失败且分组查询一致时完成 |
| HLT-TODO-009 | Blocked | 实现 ProbeScheduler 调度骨架 | health 设计 6.2/6.7；11.1 | 6.2 ProbeScheduler；6.7 步骤 3 | L2 | infra/src/health/ProbeScheduler.cpp | start(periods), stop(), tick_once() | unit：周期触发与超时路由；failure：调度线程故障退化 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-008 | HLT-BLK-001 | platform 线程/定时抽象接口冻结 | 调度骨架或阻塞记录 | 仅当平台抽象冻结后状态由 Blocked 转 Not Started |
| HLT-TODO-010 | Not Started | 实现 ProbeExecutor 执行骨架 | health 设计 6.2/6.7/6.8 | 6.2 ProbeExecutor；6.8 探针超时/异常 | L3 | infra/src/health/ProbeExecutor.cpp | execute_once(descriptor), execute_batch(group) | unit：超时/异常结构化返回；failure：探针持续失败计数 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-001、HLT-TODO-004、HLT-TODO-008 | 无 | 无 | Executor 骨架、单测 | 仅当超时与异常都映射明确错误码且测试通过时完成 |
| HLT-TODO-011 | Not Started | 实现 HealthEvaluator 三态评估骨架 | health 设计 6.2/6.7/6.8/6.9 | 6.2 HealthEvaluator；6.8 异常分类；6.9 阈值配置 | L3 | infra/src/health/HealthEvaluator.cpp | evaluate(results), evaluate_transition(previous,current) | unit：Healthy/Degraded/Unhealthy 判定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-003、HLT-TODO-005、HLT-TODO-010 | profile 键命名未冻结（部分） | 阈值先按默认键实现，运行时覆盖后置 | Evaluator 骨架、单测 | 仅当三态判定与连续失败阈值行为可重复验证时完成 |
| HLT-TODO-012 | Blocked | 实现 HealthEventPublisher 状态事件发布骨架 | health 设计 6.2/6.8/6.10；11.1 | 6.2 HealthEventPublisher；6.10 状态转移事件 | L2 | infra/src/health/HealthEventPublisher.cpp | publish_transition(from,to,reason), publish_probe_failure(result) | unit：仅状态变化时发布；failure：发布失败计数 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-005、HLT-TODO-011 | HLT-BLK-002 | event bus 最小发布接口冻结 | 发布骨架或阻塞记录 | 仅当 event bus 最小接口冻结后可解除阻塞 |
| HLT-TODO-013 | Not Started | 定义 HealthErrors 错误码域与映射 | health 设计 6.6；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/health/HealthErrors.h | INF_E_HEALTH_PROBE_TIMEOUT、INF_E_HEALTH_PROBE_EXCEPTION、INF_E_HEALTH_PROBE_NOT_FOUND、INF_E_HEALTH_POLICY_INVALID、INF_E_HEALTH_EVENT_PUBLISH_FAIL | contract：映射 contracts::ResultCode；unit：枚举稳定 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-001、HLT-TODO-003 | 映射矩阵未成文 | 在 contract 测试固化映射矩阵 | 错误码头文件、映射测试 | 仅当 5 个错误码可追溯且映射测试通过时完成 |
| HLT-TODO-014 | Blocked | 定义 HealthConfigPolicy 配置模型与覆盖策略 | health 设计 6.9/11.1；蓝图 5.1 | 6.9 配置项表 | L2 | infra/src/health/HealthConfigPolicy.cpp | merge(default/profile/deploy), validate_thresholds() | unit：默认值与覆盖优先级；failure：非法阈值拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | HLT-TODO-003、HLT-TODO-005 | HLT-BLK-003 | profiles 下 infra.health 键命名冻结 | 配置策略代码或阻塞记录 | 仅当配置键命名冻结后可由 Blocked 转 Not Started |
| HLT-TODO-015 | Blocked | 实现 RecoveryHintEmitter 边界守卫骨架 | health 设计 6.2/6.8；ADR-007；11.1 | 6.2 RecoveryHintEmitter | L2 | infra/src/health/RecoveryHintEmitter.cpp | emit_hint(snapshot,reason), sanitize_hint_payload() | contract：建议与执行分离；unit：evidence_ref 完整性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-006、HLT-TODO-011 | HLT-BLK-004 | 复用的 RecoveryHint 边界 contract 模板冻结 | 发射器代码或阻塞记录 | 仅当 contract 模板冻结后方可推进 |
| HLT-TODO-016 | Not Started | 注册 health 源码到 infra CMake | health 设计 8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt、infra/src/health/ | health 源文件纳入 dasall_infra | build：dasall_infra 可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | HLT-TODO-001~015 | 源文件分批落盘导致阶段性空实现 | 保留最小 non-empty health 源文件 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 health 源码入图时完成 |
| HLT-TODO-017 | Not Started | 注册 health 的 unit/contract/integration 测试入口 | health 设计 7/8/9；工程规范 3.7；tests 现状 | 7 映射；8.1 路径；9.1 测试矩阵 | L0 | tests/unit/CMakeLists.txt、tests/unit/infra/health/、tests/contract/CMakeLists.txt、tests/integration/infra/health/ | unit/contract/failure 注入先行，integration 发现性门禁 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-016 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 HLT-TODO-016 完成后落盘具体 integration 用例 | 测试注册改动或阻塞记录 | 仅当 health 新增测试可被 ctest -N 发现；integration 用例可被发现并执行时完成 |
| HLT-TODO-018 | Not Started | 回写 health 质量门与交付证据 | health 设计 9.2/11；工程规范 6.2 | 9.2 Gate；11 阻塞与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md | process test：门禁结论、阻塞变化、回退证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | HLT-TODO-017 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个门禁有通过/失败结论及命令证据时完成 |

### 6.2 当前 Blocked 任务索引

| 任务 ID | 对应阻塞项 |
|---|---|
| HLT-TODO-009 | HLT-BLK-001 |
| HLT-TODO-012 | HLT-BLK-002 |
| HLT-TODO-014 | HLT-BLK-003 |
| HLT-TODO-015 | HLT-BLK-004 |

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
| HLT-GATE-09 | integration 准入门 | 进入 integration 任务前 | tests 顶层完成 integration 接线并定义标签规范 | 未通过前 integration 任务保持 Blocked |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| HLT-BLK-001 | platform 线程与定时抽象未统一 | HLT-TODO-009 | platform 接口冻结并给出最小调度契约 | 在 platform 文档冻结 IThread/ITimer 最小接口并回链到 health | 先禁用周期调度，仅保留 evaluate_now 同步路径 |
| HLT-BLK-002 | 事件总线发布接口未冻结 | HLT-TODO-012 | event publish 最小接口与 EventEnvelope 约束冻结 | 先定义最小 publish_transition API 与失败返回语义 | 先仅记录日志/指标并缓存状态转移，不对外发总线事件 |
| HLT-BLK-003 | profiles 下 infra.health 键命名未冻结 | HLT-TODO-014、HLT-TODO-011（部分） | 冻结 infra.health.* 键名与覆盖优先级 | 在 profile 文档补齐键名并评审 | 暂停运行时覆盖，仅保留默认+部署层 |
| HLT-BLK-004 | RecoveryHint 边界 contract 模板缺失 | HLT-TODO-006、HLT-TODO-015 | 提供可复用边界测试模板并冻结断言项 | 在 tests/contract 增补建议/执行分离模板 | 发射器仅本地构造对象，不宣称 contract ready |
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

1. integration 验收命令本轮不纳入必过基线，原因是 HLT-TODO-017 尚未落盘具体 integration 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
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
| 测试拓扑不完整风险 | Medium | integration 未接线即推进集成验收 | ctest -N 无 integration 用例 | 延迟 integration 任务，保留 unit/contract 门禁 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3 为主），但部分任务需先解阻后再执行。
2. 原因：
   - 已有明确接口清单、对象字段、主/异常流程与错误码域。
   - 已有落盘路径、测试出口与 Design -> Build 映射。
   - 当前代码虽为空实现，但目录与 CMake 骨架已存在，可承载增量落盘。
   - 已识别并量化 5 项阻塞，解阻动作可最小执行。
   - ADR 边界对 RecoveryHint 与调度权归属约束清晰，可直接转为门禁断言。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构（L3），受阻链路为 L2/L0。
4. 未达到全量函数级的缺口：平台线程抽象、事件总线最小接口、profile 键命名、RecoveryHint contract 模板、integration 顶层接线。
5. 下一步建议：
   - 先执行 HLT-TODO-001~008、010~011、013、016 完成接口对象与主链骨架。
   - 并行推进 HLT-BLK-001~005 的解阻动作。
   - 解阻后再推进 HLT-TODO-009、012、014、015、017，最后执行 HLT-TODO-018 收口证据。
