# DASALL infrastructure 子系统 config 组件专项 TODO

最近更新时间：2026-04-02  
阶段：Detailed Design -> Special TODO  
适用范围：infra/config

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md
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
16. 当前代码现状：infra/CMakeLists.txt、infra/include/、infra/src/{InfraServiceFacade.cpp,InfraErrorCode.cpp,audit/,plugin/,tracing/}、infra/src/config/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 infrastructure/config 组件边界。
3. 每个任务必须具备代码目标、测试目标、验收命令三件套。
4. 设计证据不足处仅输出 Blocked 与补设计任务，不伪造实现任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供配置读取、分层合并、类型化访问、运行期覆盖、变更分发、配置校验与回退能力。
2. 落地四层配置模型：默认 -> Profile -> 部署 -> 运行时覆盖。
3. 保证配置变更可观测、可回退、可灰度、可验证。
4. 保持 Layer 1 边界：仅提供基础设施能力，不承载业务决策。

### 2.2 范围边界

纳入范围：

1. config 组件接口、对象、错误码、主/异常流程。
2. config 组件 CMake 接线、测试注册与门禁。
3. config 对 logging/tracing/metrics/secret 的 config 侧依赖接点。
4. profile 裁剪约束与运行时覆盖白名单约束。

不纳入范围：

1. runtime 主状态机、恢复裁定、调度策略实现。
2. context 语义装配与 prompt 渲染职责。
3. contracts 共享语义对象扩写或重定义。
4. 外置配置中心强一致事务与集群能力（v2 预研项）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 config TODO 的影响 |
|---|---|---|---|---|
| CFG-TC001 | config 设计 1/6.1；架构 3.4.7/5.10 | Must | infra/config 属于 Layer 1，只提供基础设施能力 | 任务必须限制在配置治理能力，不写业务决策 |
| CFG-TC002 | 蓝图 4.1/4.2；架构 3.7 | Must | 依赖方向单向，infra 不反向依赖业务实现 | 代码目标仅限 infra/tests/docs/cmake |
| CFG-TC003 | 蓝图 3.12；架构 8.6 | Must | 必须支持四层配置模型 | 必须拆分 load/merge/validate/commit 任务 |
| CFG-TC004 | 蓝图 3.13/5.1 | Must | Profile 只能裁剪能力和替换实现，不得绕过 Runtime 主控、PolicyGate、Audit | 运行时覆盖任务必须带白名单/门禁 |
| CFG-TC005 | ADR-005 | Must | 不得反向改写已冻结边界与 contracts 先行原则 | 设计缺口必须标 Blocked，不可伪造实现 |
| CFG-TC006 | ADR-006 | Must-Not | config 不接管上下文语义装配与 Prompt 渲染 | 禁止把 context/prompt 逻辑写入 config |
| CFG-TC007 | ADR-007 | Must-Not | config 不裁定失败语义与恢复策略 | 仅输出错误码与观测，不做恢复决策 |
| CFG-TC008 | ADR-008 | Must-Not | config 不拥有主控调度权 | 仅服务 orchestrator/coordinator 配置消费 |
| CFG-TC009 | contracts 冻结计划与 TODO 总表 | Must | 兼容优先，新增字段优先 optional，breaking 必须评审 | 所有接口与对象任务必须设置 breaking gate |
| CFG-TC010 | 编码规范 3.6 | Must | 禁止吞错，失败必须可观测 | 每条错误路径任务必须有日志/指标/审计出口 |
| CFG-TC011 | 编码规范 3.7 | Should | 新增公共接口应同步 unit 或 contract 测试 | 每条接口任务必须绑定测试 |
| CFG-TC012 | 落地步骤指引 阶段 C | Must | 先底座后能力，每阶段都要可测试 | 顺序必须先接口对象，再链路，再门禁 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 config PUBLIC_HEADER，但暂无 config 源文件 | config 接口已接入构建，具体实现未落盘 |
| infra/include/ | 已有 config/ 子目录，并已落盘 ConfigTypes/IConfigCenter 头文件 | config 对外接口冻结已启动 |
| infra/src/config/ | 目录尚未落盘 | config 实现未落盘 |
| tests/CMakeLists.txt | 已含 mocks/unit/contract/integration，且 integration 聚合已接线 | config 可直接落盘 integration 用例，不再受顶层拓扑阻塞 |
| tests/unit/CMakeLists.txt | 已接入 infra 子目录，并已纳入 ConfigTypes/ConfigCenter unit 目标 | config unit 发现性已接线 |
| tests/contract/CMakeLists.txt | centralized registration 已接入 ConfigTypes/ConfigCenter contract 用例 | config contract 发现性已接线 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：本轮可生成 L2 为主、局部 L3 的专项 TODO；可直接进入接口/数据结构级与主流程骨架级执行，不可全量进入函数细节实现级。

支撑证据：

1. 已有明确核心接口清单：IConfigCenter、IConfigLoader、IConfigValidator、IConfigSnapshotStore、IConfigPublisher。
2. 已有核心对象与字段：ConfigQuery、ConfigPatch、ConfigSnapshot、ConfigDiff、ValidationIssue、ConfigApplyResult。
3. 已有主流程与异常流程：启动加载、运行时覆盖、回滚与降级路径。
4. 已有错误语义：INF_CFG_E_NOT_FOUND、INF_CFG_E_TYPE_MISMATCH、INF_CFG_E_INVALID_SCHEMA、INF_CFG_E_CONFLICT、INF_CFG_E_SOURCE_UNAVAILABLE、INF_CFG_E_SECRET_RESOLVE_FAIL、INF_CFG_E_APPLY_REJECTED、INF_CFG_E_ROLLBACK_FAILED。
5. 已有落盘建议：infra/include/config、infra/src/config、tests/unit/infra/config、tests/integration/infra/config、tests/contract/infra。
6. 缺口仍在：外置配置适配器协议、事件总线最小抽象、快照持久化后端与 config integration 用例落盘。

当前最小可执行粒度：接口/数据结构级（L2），局部函数级（L3）仅限已明确语义的方法骨架。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| IConfigCenter | config 设计 6.6/6.7 | L2 | 方法名、主流程、错误语义 | startup_context 类型细节未冻结 | 直接拆接口冻结 + Facade 骨架 |
| IConfigLoader | config 设计 6.6/6.7 | L3 | load_default/load_profile/load_deploy/load_runtime_overlay 明确 | 外置源协议细节未定 | 先落四层本地读取骨架，外置源后置 |
| IConfigValidator | config 设计 6.6/6.8 | L3 | validate/validate_patch 语义明确 | 规则 DSL 未冻结 | 先落最小规则集校验 |
| IConfigSnapshotStore | config 设计 6.6/6.8 | L2 | commit/get_current/get_by_version/get_last_known_good 明确 | 持久化后端未定 | 先落内存实现与版本语义 |
| IConfigPublisher | config 设计 6.6/6.7 | L2 | publish_config_changed 与订阅语义明确 | 事件总线抽象未冻结 | 先落进程内发布骨架 |
| ConfigSnapshot/ConfigDiff | config 设计 6.5 | L3 | 字段、版本语义、source_chain 明确 | checksum 算法策略未定 | 直接拆数据结构任务 |
| ConfigPatch/ConfigApplyResult | config 设计 6.5/6.8 | L2 | 字段与白名单约束明确 | patch value 类型范围未成文 | 先冻结字段与拒绝语义 |
| ConfigMerger | config 设计 6.2/6.7 | L3 | 后层覆盖前层 + 来源追踪明确 | 冲突细则优先级例外未成文 | 先按线性优先级实现 |
| ConfigAuditBridge | config 设计 6.2/6.10 | L1 | 审计字段清单明确 | 审计写入接口签名未冻结 | 标记 Blocked，先补桥接接口 |
| SecretRefResolver | config 设计 6.2/6.4/6.8 | L1 | secret:// 解析职责明确 | secret 侧接口模型未冻结 | 标记 Blocked，先补 secret 接口对齐 |
| tests/integration 注册点 | config 设计 8.1/9；tests 现状 | L0 | 设计建议存在，且 tests 顶层 integration 拓扑已接入 | config integration 用例尚未落盘 | 直接拆 integration 注册任务 |

## 5. Design -> TODO 映射表

### 5.1 Design -> TODO 映射（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| config 统一入口与生命周期 | config 设计 6.2/6.6/6.7 | 接口/流程 | CFG-TODO-001、CFG-TODO-007 | 先冻结 IConfigCenter，再落 Facade 主链骨架 |
| 四层加载能力 | config 设计 6.2/6.6/6.7/6.9 | 接口/流程 | CFG-TODO-002、CFG-TODO-008 | 将四层读取从合并逻辑中拆开 |
| 分层合并与来源追踪 | config 设计 6.2/6.3/6.7 | 数据处理/流程 | CFG-TODO-009 | 单独验证覆盖顺序与 source_chain |
| 校验与错误语义 | config 设计 6.2/6.6/6.8 | 接口/错误处理 | CFG-TODO-003、CFG-TODO-010、CFG-TODO-012 | 接口、规则校验、错误码三段分拆 |
| 快照与回滚 | config 设计 6.2/6.6/6.8 | 数据结构/流程 | CFG-TODO-004、CFG-TODO-011 | 先内存快照，再回滚路径 |
| 运行时覆盖与发布订阅 | config 设计 6.6/6.7/6.8 | 接口/流程 | CFG-TODO-005、CFG-TODO-013 | 覆盖 apply 与发布订阅拆分 |
| 配置对象模型 | config 设计 6.5 | 数据结构 | CFG-TODO-006 | 冻结 Query/Patch/Snapshot/Diff/Issue/ApplyResult |
| CMake 与测试注册 | config 设计 7/8.1/9.1；工程现状 | 门禁/测试 | CFG-TODO-014、CFG-TODO-015、CFG-TODO-016 | 构建接线、unit/contract 注册、integration 解阻 |
| 可观测与审计桥接 | config 设计 6.10 | 桥接/门禁 | CFG-BLK-002 | 外部接口未冻结，先阻塞 |
| secret 引用解析 | config 设计 6.2/6.4/6.8 | 适配器/门禁 | CFG-BLK-003 | secret 接口未冻结，先阻塞 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | CFG-TODO-001~005 |
| 数据结构定义类任务 | 是 | CFG-TODO-006 |
| 生命周期与初始化类任务 | 是 | CFG-TODO-007~011 |
| 适配器/桥接类任务 | 是 | CFG-TODO-013、CFG-BLK-002、CFG-BLK-003 |
| 异常与错误处理类任务 | 是 | CFG-TODO-010、CFG-TODO-012 |
| 配置与 Profile 裁剪类任务 | 是 | CFG-TODO-008~010、CFG-TODO-013 |
| 测试与门禁类任务 | 是 | CFG-TODO-014~016 |
| 文档/交付证据回写类任务 | 是 | CFG-TODO-017 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CFG-TODO-001 | Done | 定义 IConfigCenter 接口头文件 | config 设计 6.6；编码规范 3.7 | 6.6 IConfigCenter | L2 | infra/include/config/IConfigCenter.h | load_layers(startup_context), get_typed(query), apply_override(config_patch), rollback(rollback_token), subscribe(subscription_request) | unit：接口占位对象与查询/回滚/订阅守卫可编译；contract：错误语义入口可映射，且 runtime override 不越过 profile 保护键 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" | 无 | 无 | 无 | 接口头文件、unit/contract 测试；2026-03-30 已落盘 infra/include/config/IConfigCenter.h、tests/unit/infra/ConfigCenterInterfaceTest.cpp、tests/contract/smoke/ConfigCenterInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 5 个方法与 6.6 一致、返回边界不引入业务依赖且测试通过时完成 |
| CFG-TODO-002 | Done | 定义 IConfigLoader 接口头文件 | config 设计 6.6/6.7 | 6.6 IConfigLoader | L3 | infra/include/config/IConfigLoader.h | load_default, load_profile, load_deploy, load_runtime_overlay | unit：四层入口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | CFG-TODO-001 | 外置源协议未冻结 | 首版仅约束本地源入口，外置源后置 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/config/IConfigLoader.h、tests/unit/infra/ConfigLoaderInterfaceTest.cpp，并完成 infra/tests CMake 注册 | 仅当四层加载方法齐全且命名与锚点一致时完成 |
| CFG-TODO-003 | Done | 定义 IConfigValidator 接口头文件 | config 设计 6.6/6.8 | 6.6 IConfigValidator | L3 | infra/include/config/IConfigValidator.h | validate(snapshot), validate_patch(current_snapshot, patch) | unit：接口可编译；contract：校验失败可映射错误码 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | CFG-TODO-001 | 规则 DSL 未冻结 | 首版仅定义最小规则集入口 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/config/IConfigValidator.h、tests/unit/infra/ConfigValidatorInterfaceTest.cpp、tests/contract/smoke/ConfigValidatorInterfaceBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当校验接口覆盖完整且可编译时完成 |
| CFG-TODO-004 | Done | 定义 IConfigSnapshotStore 接口头文件 | config 设计 6.6/6.8 | 6.6 IConfigSnapshotStore | L2 | infra/include/config/IConfigSnapshotStore.h | commit, get_current, get_by_version, get_last_known_good | unit：版本读写入口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | CFG-TODO-001 | 持久化后端未定 | 首版以内存快照语义冻结接口 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/config/IConfigSnapshotStore.h、tests/unit/infra/ConfigSnapshotStoreInterfaceTest.cpp，并完成 infra/tests CMake 注册 | 仅当 4 个方法完整且不泄露后端细节时完成 |
| CFG-TODO-005 | Done | 定义 IConfigPublisher 接口头文件 | config 设计 6.6/6.7 | 6.6 IConfigPublisher | L2 | infra/include/config/IConfigPublisher.h | publish_config_changed(diff) | unit：发布接口可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | CFG-TODO-001 | 事件总线抽象未冻结 | 首版定义进程内发布接口语义 | 接口头文件、编译记录；2026-03-31 已落盘 infra/include/config/IConfigPublisher.h、tests/unit/infra/ConfigPublisherInterfaceTest.cpp，并完成 infra/tests CMake 注册 | 仅当发布语义与配置 diff 对齐时完成 |
| CFG-TODO-006 | Done | 定义 ConfigTypes 核心对象 | config 设计 6.5 | 6.5 核心对象表 | L3 | infra/include/config/ConfigTypes.h | TypedConfig、ConfigQuery、ConfigPatchEntry、ConfigPatch、ConfigLayerRef、ConfigSnapshot、ConfigDiff、ValidationIssue、ConfigApplyResult | unit：字段完整性、schema/profile 键名与 patch 守卫；contract：ConfigApplyResult 仅使用 contracts 错误语义，其他对象不污染 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest" | 无 | 无 | 无 | 对象头文件、unit/contract 测试；2026-03-30 已落盘 infra/include/config/ConfigTypes.h、tests/unit/infra/ConfigTypesTest.cpp、tests/contract/smoke/ConfigTypesBoundaryContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 TypedConfig/patch/schema/profile 键名冻结与 unit/contract 证据一致时完成 |
| CFG-TODO-007 | Done | 实现 ConfigCenterFacade 生命周期骨架 | config 设计 6.2/6.7；设计映射 7 | 6.2 ConfigCenterFacade；6.7 启动流程 | L2 | infra/src/config/ConfigCenterFacade.cpp | load_layers 主链、get_typed 查询入口、apply_override/rollback 入口 | unit：未初始化/初始化后路径；contract：错误码映射入口 | cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R ConfigCenterFacadeTest | CFG-TODO-001、CFG-TODO-006 | startup_context 类型细节未冻结 | 首版使用最小上下文结构占位 | Facade 骨架、测试；2026-04-02 已落盘 infra/include/config/ConfigCenterFacade.h、infra/src/config/ConfigCenterFacade.cpp、tests/unit/infra/ConfigCenterFacadeTest.cpp，并完成 infra/tests CMake 注册 | 仅当主链入口可走通且失败路径可判定时完成 |
| CFG-TODO-008 | Not Started | 实现 ConfigLoader 四层读取骨架 | config 设计 6.2/6.7/6.9 | 6.7 启动流程第 2 步；6.9 配置层级 | L3 | infra/src/config/ConfigLoader.cpp | load_default/load_profile/load_deploy/load_runtime_overlay | unit：四层读取顺序与 source_id/version | ctest --test-dir build-ci -R ConfigLoaderTest | CFG-TODO-002、CFG-TODO-006 | profiles 键空间规范待确认 | 先按既有键名前缀读取，保留映射层 | Loader 骨架、单测 | 仅当四层均可加载且顺序符合锚点时完成 |
| CFG-TODO-009 | Not Started | 实现 ConfigMerger 覆盖与来源追踪骨架 | config 设计 6.2/6.7 | 6.7 启动流程第 3 步 | L3 | infra/src/config/ConfigMerger.cpp | merge(layers) 产生 merged tree + source_chain | unit：后层覆盖前层；冲突可定位 | ctest --test-dir build-ci -R ConfigMergerTest | CFG-TODO-006、CFG-TODO-008 | 冲突例外策略未成文 | 首版仅支持线性优先级覆盖 | Merger 骨架、单测 | 仅当覆盖顺序与来源追踪都可测试验证时完成 |
| CFG-TODO-010 | Not Started | 实现 ConfigValidator 规则校验骨架 | config 设计 6.2/6.7/6.8 | 6.7 启动流程第 4 步；6.8 语义故障 | L3 | infra/src/config/ConfigValidator.cpp | validate, validate_patch | unit：类型/范围/互斥校验；contract：错误码映射 | ctest --test-dir build-ci -R "ConfigValidatorTest|ConfigErrorMappingContractTest" | CFG-TODO-003、CFG-TODO-006 | 规则 DSL 未冻结 | 首版落最小规则集（类型/范围/互斥） | Validator 骨架、测试 | 仅当非法配置被拒绝且错误可定位时完成 |
| CFG-TODO-011 | Not Started | 实现 ConfigSnapshotStore 快照与 LKG 骨架 | config 设计 6.2/6.7/6.8 | 6.7 提交 current 与 LKG；6.8 回退动作 | L2 | infra/src/config/ConfigSnapshotStore.cpp | commit/get_current/get_by_version/get_last_known_good | unit：版本单调递增、LKG 可回退 | ctest --test-dir build-ci -R ConfigSnapshotStoreTest | CFG-TODO-004、CFG-TODO-006 | 持久化后端未定 | 首版实现内存快照与启动导入 | SnapshotStore 骨架、单测 | 仅当失败时可回退到 LKG 且测试通过时完成 |
| CFG-TODO-012 | Done | 定义 ConfigErrors 错误码域与映射 | config 设计 6.6/6.8；编码规范 3.6 | 6.6 错误语义 | L3 | infra/include/config/ConfigErrors.h | INF_CFG_E_NOT_FOUND, INF_CFG_E_TYPE_MISMATCH, INF_CFG_E_INVALID_SCHEMA, INF_CFG_E_CONFLICT, INF_CFG_E_SOURCE_UNAVAILABLE, INF_CFG_E_SECRET_RESOLVE_FAIL, INF_CFG_E_APPLY_REJECTED, INF_CFG_E_ROLLBACK_FAILED | contract：映射 contracts::ResultCode；unit：错误码稳定 | ctest --test-dir build-ci -R ConfigErrorMappingContractTest | CFG-TODO-001、CFG-TODO-003 | 映射矩阵未成文 | 在 contract 测试固化映射矩阵 | 错误码头文件、映射测试；2026-03-31 已落盘 infra/include/config/ConfigErrors.h、tests/unit/infra/ConfigErrorsTest.cpp、tests/contract/smoke/ConfigErrorMappingContractTest.cpp，并完成 infra/tests CMake 注册 | 仅当 8 个错误码均有锚点且映射测试通过时完成 |
| CFG-TODO-013 | Blocked | 实现 ConfigPublisher 运行时覆盖发布骨架 | config 设计 6.2/6.7/6.8；设计映射 7 | 6.7 运行时覆盖与 ConfigChanged 事件 | L2 | infra/src/config/ConfigPublisher.cpp | publish_config_changed(diff), namespace filter subscribe | integration：ConfigRuntimePatchIntegrationTest | ctest --test-dir build-ci -R ConfigRuntimePatchIntegrationTest | CFG-TODO-005、CFG-TODO-006、CFG-TODO-010、CFG-TODO-011 | CFG-BLK-001 | 先冻结事件总线最小抽象与订阅语义 | 发布骨架或阻塞记录 | 仅当事件总线抽象冻结后，状态才可由 Blocked 改为 Not Started |
| CFG-TODO-014 | Not Started | 注册 config 代码到 infra CMake | config 设计 8.1；工程现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt、infra/include/config/、infra/src/config/ | 将 config 头文件与源文件纳入 dasall_infra | build：dasall_infra 可编译 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | CFG-TODO-001~CFG-TODO-012 | 初期源文件渐进落盘 | 保留最小 non-empty 源文件过渡 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一功能入口且构建通过时完成 |
| CFG-TODO-015 | Not Started | 注册 config unit 与 contract 测试入口 | config 设计 8.1/9.1；编码规范 3.7 | 9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/config/、tests/contract/CMakeLists.txt | unit：ConfigCenterFacade/Loader/Merger/Validator/SnapshotStore；contract：错误码与边界映射 | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | CFG-TODO-014 | 无 | 无 | 测试代码、注册入口、执行记录 | 仅当新增测试在 ctest -N 可见并执行通过时完成 |
| CFG-TODO-016 | Not Started | 补齐 config integration 注册拓扑 | config 设计 8.1/9.1；tests 现状 | tests/integration 落盘建议 | L0 | tests/CMakeLists.txt、tests/integration/infra/config/ | integration：ConfigRuntimePatchIntegrationTest、ConfigObservabilityIntegrationTest | ctest --test-dir build-ci -N && ctest --test-dir build-ci -R "ConfigRuntimePatchIntegrationTest|ConfigObservabilityIntegrationTest" | CFG-TODO-015 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 CFG-TODO-015 完成后落盘具体 integration 用例 | CMake 改动或阻塞记录 | 仅当 integration 用例可发现并可执行时完成 |
| CFG-TODO-017 | Not Started | 回写 config 质量门与交付证据 | config 设计 9.2/11；工程规范 6.2 | 9.2 Gate 建议；11 风险与回退 | L2 | docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md | process test：门禁结论、阻塞变化、回退执行证据回写 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | CFG-TODO-015 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个门禁项都有通过/失败结论与对应命令证据时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 接口与对象冻结 | CFG-TODO-001~006、CFG-TODO-012 | 可并行（接口组/对象组/错误码组） | 先冻结边界，减少实现返工 |
| B 主链路骨架 | CFG-TODO-007~011 | 串行 | facade -> loader -> merger -> validator -> snapshot |
| C 构建与测试接线 | CFG-TODO-014~015 | 可并行 | CMake 与测试注册可同步推进 |
| D 发布与集成 | CFG-TODO-013、CFG-TODO-016 | 串行且当前 Blocked | 先事件总线抽象，再 integration 注册 |
| E 证据收口 | CFG-TODO-017 | 串行 | 回写 gate、阻塞、回退证据 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 通过标准 | 失败处置 |
|---|---|---|---|
| CFG-GATE-01 | 接口冻结门 | IConfigCenter/IConfigLoader/IConfigValidator/IConfigSnapshotStore/IConfigPublisher 与 ConfigTypes 全部落盘并编译通过 | 回退到接口定义，不推进实现 |
| CFG-GATE-02 | 四层模型门 | defaults/profile/deploy/runtime 覆盖顺序测试通过 | 回退 ConfigLoader/ConfigMerger |
| CFG-GATE-03 | 错误可观测门 | 关键失败路径均返回明确错误码并可观测 | 补齐错误码和观测点 |
| CFG-GATE-04 | 回退可用门 | LKG 回退路径测试通过 | 回退 SnapshotStore 变更 |
| CFG-GATE-05 | 测试发现性门 | ctest -N 可见 config 新增 unit/contract 测试 | 修复 CMake 注册 |
| CFG-GATE-06 | breaking 评审门 | 任意接口签名或 contracts 映射变更均有评审结论 | 未评审不得推进 |
| CFG-GATE-07 | integration 准入门 | tests 顶层完成 integration 接线并标签规范落地 | 未通过前禁止推进 integration 验收 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| CFG-BLK-001 | 事件总线最小抽象未冻结，ConfigPublisher 订阅发布语义无法稳定实现 | CFG-TODO-013 | 冻结发布/订阅最小接口与命名空间过滤语义 | 在 config 设计补充事件抽象章节与接口表 | 暂时禁用运行时发布，仅保留静态加载 |
| CFG-BLK-002 | 审计桥接接口未冻结，ConfigAuditBridge 无法稳定落盘 | 后续 ConfigAuditBridge 任务 | 冻结 audit 写入接口与最小字段集合 | 在 infra logging/audit 设计补桥接签名 | 暂时仅记录本地日志与指标，不写审计管线 |
| CFG-BLK-003 | secret 接口模型未冻结，SecretRefResolver 无法稳定实现 | 后续 SecretRefResolver 任务 | 冻结 ISecretManager 最小 get/rotate 语义与句柄模型 | 在 secret 设计补接口与错误映射 | 暂时拒绝 secret://，返回 INF_CFG_E_SECRET_RESOLVE_FAIL |
| CFG-BLK-004 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；config integration 用例是否可执行改由组件自身落盘负责 | CFG-TODO-016 | 无；后续仅需按组件落盘 integration 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt | 若 tests 顶层 integration 接线或聚合依赖回退，则重新转为 Blocked |
| CFG-BLK-005 | 快照持久化后端未冻结（sqlite/file） | 后续持久化任务 | 冻结持久化后端策略与恢复语义 | 先完成内存快照版本并补策略评审 | 回退为进程内回滚，不承诺重启后版本恢复 |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 执行 unit 套件 | cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 套件 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract |
| 检查测试发现性 | ctest --test-dir build-ci -N |
| 点测 config 测试（设计建议） | ctest --test-dir build-ci -R "ConfigCenterFacadeTest|ConfigLoaderTest|ConfigMergerTest|ConfigValidatorTest|ConfigSnapshotStoreTest|ConfigErrorMappingContractTest" |

说明：

1. integration 验收命令当前不纳入首轮 Gate，原因是 CFG-TODO-016 尚未落盘具体 integration 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 每项任务至少需要一条构建命令与一条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而不是仅列标题：是。
2. 是否明确当前最细可达到粒度等级：是（L2 为主，局部 L3）。
3. 是否所有任务都具备代码目标 + 测试目标 + 验收命令：是。
4. 是否所有 Blocked 项都带证据与解阻条件：是。
5. 是否所有任务都具备可二值判定完成标准：是。
6. 是否避免跨子系统扩张：是。
7. 是否真正落到接口/数据结构级对象：是。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 配置合并顺序实现偏差 | High | 层级覆盖次序与 6.9 不一致 | ConfigMergerTest 失败 | 回退到线性优先级实现并重跑覆盖测试 |
| 运行时覆盖越权 | High | 非白名单键被允许 apply | apply_override 失败率异常或审计缺失 | 立即关闭 runtime_patch.enabled，回退到部署层 |
| 错误码语义漂移 | High | ConfigErrors 与 contracts 映射不一致 | Contract 测试失败 | 回退错误映射并冻结变更 |
| LKG 回退不可用 | High | 快照提交失败后无法 rollback | SnapshotStoreTest 失败 | 回退到上一稳定版本并仅允许只读查询 |
| 审计桥接长期阻塞 | Medium | CFG-BLK-002 长期未解 | 高风险键变更无审计证据 | 暂时提高日志与指标等级并限制高风险键变更 |
| integration 用例缺失导致质量盲区 | Medium | config 组件 integration 用例长期未落盘 | ctest -N 无 config integration 用例 | 将 integration 任务保持 Not Started，并优先补齐组件用例 |

## 11. 可行性结论

1. 结论：可直接生成并执行接口/数据结构级专项 TODO，局部可进入函数骨架级；不建议在本轮全面进入细节函数实现级。
2. 原因：
   - 已具备核心接口清单与对象字段定义。
   - 已具备主流程、异常流程和错误码语义。
   - 已具备落盘目录与测试出口建议。
   - 事件总线、secret 接口、config integration 用例落盘仍有关键缺口。
3. 当前最小可执行粒度：接口 / 数据结构（L2），局部函数骨架（L3）。
4. 未达到全量函数级的缺口：事件总线抽象、secret 对齐接口、持久化后端、config integration 用例落盘。
5. 下一步建议：
   - 继续执行 CFG-TODO-002~005、007~012、014~015 建立可编译可测试骨架。
   - 并行推进 CFG-BLK-001/004 解阻，完成发布与 integration 准入。
   - 阻塞解除后再推进 CFG-TODO-013 与后续桥接任务，最后执行 CFG-TODO-017 证据收口。

## 12. 本轮执行记录（2026-03-30 / CFG-TODO-006）

### 12.1 选中任务

1. 本轮任务：CFG-TODO-006。
2. 可执行性依据：无前置依赖，且 INF-BLK-01 对应的 TypedConfig/patch/schema/profile 键名冻结缺口可由该任务最小闭环修复。

### 12.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.5 原有对象表已冻结 ConfigQuery/ConfigPatch/ConfigSnapshot 等对象，但未把 TypedConfig、ConfigLayerRef、配置格式和 profile 键名落到代码级契约。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已明确四层来源、runtime override 的 TTL/base_version、以及 `schema_version`、`profile_meta.*`、`enabled_modules.*` 的受保护边界。
3. docs/architecture/DASALL_profiles模块详细设计.md 6.9 已冻结五档 `profile_id`、`profile_meta` 必填键、`enabled_modules` 命名表和 schema_version=1。

外部参考：

1. Azure External Configuration Store 模式要求配置接口暴露 typed/structured 数据、版本、作用域与 last-known-good fallback；本轮据此把 source format、schema_version 与 source_chain 一并冻结。
2. 12-Factor Config 强调 deploy-time config 与代码分离、运行期配置按正交变量治理；本轮据此把五档 profile_id 与 runtime override 的受保护路径固定为不可热改边界。

D 结论：

1. Design -> Build 映射：新增 ConfigTypes.h，冻结 TypedConfig、ConfigPatchEntry、ConfigLayerRef 与六个核心对象的最小 typed 契约。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/ConfigTypes.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigTypesTest 覆盖 schema/profile 键名与 patch 守卫；新增 ConfigTypesBoundaryContractTest 覆盖 contracts 错误语义边界。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"。
3. D Gate：PASS。

### 12.3 Build 交付与证据

交付物：

1. infra/include/config/ConfigTypes.h：新增 TypedConfig、ConfigPatchEntry、ConfigLayerRef 和 ConfigApplyResult 等对象，冻结 schema_version=1、五档 profile_id 与 runtime override 受保护路径。
2. tests/unit/infra/ConfigTypesTest.cpp：覆盖 typed config/query、patch 元数据与受保护路径、四层 source_chain 唯一性。
3. tests/contract/smoke/ConfigTypesBoundaryContractTest.cpp：覆盖 ConfigApplyResult 的 contracts 错误语义边界与 profile 保护路径不外溢。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/contract/CMakeLists.txt：完成 config 头文件与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`：通过；unit 39/39、contract 94/94 全部通过。
3. `ctest --test-dir build-ci -N -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"`：通过，发现 2 个测试，分别为 `ConfigTypesTest` 与 `ConfigTypesBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigTypesTest|ConfigTypesBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增对象与守卫语义可由命名直接表达，无需冗余注释。
2. 正负例覆盖：unit 覆盖合法 typed/query/patch/source_chain 正例与 schema/path/duplicate layer 负例；contract 覆盖 contracts 错误边界与 profile 保护路径。
3. 测试发现性：本轮显式要求 `ctest -N -R ...` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物、发现性结果与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 config 类型对象、测试、CMake 注册与 blocker 校准文档。

## 13. 本轮执行记录（2026-03-30 / CFG-TODO-001）

### 13.1 选中任务

1. 本轮任务：CFG-TODO-001。
2. 可执行性依据：CFG-TODO-006 已完成，TypedConfig/patch/schema/profile 键名已冻结，IConfigCenter 可直接落到接口头文件级且不越过实现边界。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已明确 IConfigCenter 的五个核心入口。
2. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已明确 runtime override 仅接受受管 patch，并受 profile 保护键约束。
3. infra/include/config/ConfigTypes.h 已冻结 ConfigQuery、ConfigPatch、ConfigDiff 与 ConfigApplyResult，可直接复用为 IConfigCenter typed 契约。

外部参考：

1. Azure External Configuration Store 模式要求配置接口提供 typed/structured 读取、版本控制和变更通知；本轮据此冻结 startup context、typed lookup、rollback token 与 namespace-filtered subscription 的最小接口形状。

D 结论：

1. Design -> Build 映射：新增 IConfigCenter.h，冻结 ConfigStartupContext、ConfigRollbackToken、ConfigSubscriptionRequest、ConfigSubscriptionHandle 与 IConfigCenter 接口。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/IConfigCenter.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigCenterInterfaceTest 与 ConfigCenterInterfaceBoundaryContractTest。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigCenter.h：新增 startup context、rollback token、subscription request/handle 与 IConfigCenter 五个接口入口。
2. tests/unit/infra/ConfigCenterInterfaceTest.cpp：覆盖 load_layers/get_typed/apply_override/rollback/subscribe 的最小正负例。
3. tests/contract/smoke/ConfigCenterInterfaceBoundaryContractTest.cpp：覆盖接口签名、typed lookup 边界与受保护 profile 键拒绝路径。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；unit 40/40、contract 95/95 全部通过。
3. `ctest --test-dir build-ci -N -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `ConfigCenterInterfaceTest` 与 `ConfigCenterInterfaceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigCenterInterfaceTest|ConfigCenterInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增接口和占位对象命名已直接表达语义，无需冗余注释。
2. 正负例覆盖：unit 将覆盖合法 startup/query/subscription 正例与非法 load/override/rollback 负例；contract 将覆盖签名边界与受保护 profile 键拒绝路径。
3. 测试发现性：本轮显式要求 `ctest -N -R ...` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物、发现性结果与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 IConfigCenter 头文件、测试、CMake 注册和 INF-TODO-013 证据文档。

## 14. 本轮执行记录（2026-03-31 / CFG-TODO-002）

### 14.1 选中任务

1. 本轮任务：CFG-TODO-002。
2. 可执行性依据：CFG-TODO-001 已完成，IConfigCenter/ConfigTypes 已冻结，且 CFG-TODO-002 仅需补 IConfigLoader 接口边界，不依赖外置配置协议落地。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.3 已明确 ConfigLoader 输出原始层配置文档，且至少包含 source_id 与 version。
2. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已冻结 IConfigLoader 四个入口：load_default、load_profile(profile_id)、load_deploy(source_ref)、load_runtime_overlay()。
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已明确 defaults/profile/deployment/runtime 四层来源、格式和治理边界。

外部参考：

1. Azure App Configuration keys and values 文档强调配置键采用层次化命名，且通过 label/version 组织不同来源与版本；本轮据此把 Loader 输出收敛为带 source_id、version_ref 的最小 layer document，而不提前冻结远端适配细节。

D 结论：

1. Design -> Build 映射：新增 IConfigLoader.h，冻结 ConfigLayerDocument、ConfigLoadResult 与 IConfigLoader 四层加载接口。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/IConfigLoader.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigLoaderInterfaceTest，覆盖四层入口正例，以及非法 profile/source_ref/runtime overlay 负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci -N -R ConfigLoaderInterfaceTest && ctest --test-dir build-ci --output-on-failure -R ConfigLoaderInterfaceTest。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigLoader.h：新增 ConfigLayerDocument、ConfigLoadResult 与 IConfigLoader 四层入口。
2. tests/unit/infra/ConfigLoaderInterfaceTest.cpp：覆盖 defaults/profile/deploy/runtime 四层入口正例，以及非法 profile/source_ref/runtime overlay 负例。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件、unit 测试注册与 unit 聚合目标接线。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests`：通过；unit 42/42 全部通过。
3. `ctest --test-dir build-ci -N -R ConfigLoaderInterfaceTest`：通过，发现 1 个测试：`ConfigLoaderInterfaceTest`。
4. `ctest --test-dir build-ci --output-on-failure -R ConfigLoaderInterfaceTest`：通过，1/1 tests passed。

Blocker 修复记录：

1. 首次执行 `cmake --build build-ci --target dasall_infra dasall_unit_tests` 时，`ConfigLoaderInterfaceTest` 已被 CTest 发现，但 `dasall_config_loader_interface_unit_test` 未被 `tests/unit/CMakeLists.txt` 的 `DASALL_UNIT_TEST_EXECUTABLE_TARGETS` 聚合列表纳入，导致测试可执行文件未构建。
2. 本轮内最小解阻动作：补充 `tests/unit/CMakeLists.txt` 中的 `dasall_config_loader_interface_unit_test` 聚合依赖后重跑 configure/build/test。
3. 解阻结论：Blocker cleared，同轮恢复 CFG-TODO-002 并完成验收。

Build 合规复核：

1. 代码注释：新增接口与结果对象命名已直接表达 source_id/version/document_format 语义，无需额外注释。
2. 正负例覆盖：unit 覆盖四层入口正例与非法 profile/source_ref/runtime overlay 负例。
3. 测试发现性：已通过 `ctest -N -R ConfigLoaderInterfaceTest` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物、blocker 修复与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 IConfigLoader 头文件、unit 测试、CMake 注册与 TODO 证据文档。

## 15. 本轮执行记录（2026-03-31 / CFG-TODO-003）

### 15.1 选中任务

1. 本轮任务：CFG-TODO-003。
2. 可执行性依据：CFG-TODO-001 和 CFG-TODO-006 已完成，ConfigSnapshot/ConfigPatch/ValidationIssue 已冻结，可直接补 IConfigValidator 接口而不提前冻结规则 DSL。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.3 已明确 ConfigValidator 输出校验报告，失败必须给出路径与原因。
2. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已冻结 validate(snapshot) 与 validate_patch(current_snapshot, patch) 两个核心入口。
3. docs/architecture/DASALL_infrastructure子系统详细设计.md 6.6/6.9 已明确 config 失败必须可观测、runtime patch 必须受版本与治理边界约束。

外部参考：

1. JSON Schema object/reference 文档强调 required properties、additionalProperties 与 property-specific validation 应返回可定位的字段级错误；本轮据此把 Validator 结果收敛为带 ValidationIssue 列表的 report，并保持失败路径可映射到 contracts 错误语义。

D 结论：

1. Design -> Build 映射：新增 IConfigValidator.h，冻结 ConfigValidationReport、ConfigValidationResult 与 IConfigValidator 两个校验入口。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/IConfigValidator.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigValidatorInterfaceTest 与 ConfigValidatorInterfaceBoundaryContractTest，覆盖合法 snapshot/patch 正例与 invalid snapshot/base_version mismatch/empty patch 负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigValidatorInterfaceTest|ConfigValidatorInterfaceBoundaryContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigValidatorInterfaceTest|ConfigValidatorInterfaceBoundaryContractTest"。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigValidator.h：新增 ConfigValidationReport、ConfigValidationResult 与 validate/validate_patch 接口。
2. tests/unit/infra/ConfigValidatorInterfaceTest.cpp：覆盖合法 snapshot/patch 正例，以及 invalid snapshot 与 base_version mismatch 负例。
3. tests/contract/smoke/ConfigValidatorInterfaceBoundaryContractTest.cpp：覆盖 validator failure 的 contracts 错误映射边界。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件、unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；unit 43/43、contract 96/96 全部通过。
3. `ctest --test-dir build-ci -N -R "ConfigValidatorInterfaceTest|ConfigValidatorInterfaceBoundaryContractTest"`：通过，发现 2 个测试，分别为 `ConfigValidatorInterfaceTest` 与 `ConfigValidatorInterfaceBoundaryContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigValidatorInterfaceTest|ConfigValidatorInterfaceBoundaryContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：新增接口与结果对象命名已直接表达 report/result 语义，无需额外注释。
2. 正负例覆盖：unit 覆盖 snapshot/patch 正例与 invalid snapshot/base_version mismatch 负例；contract 覆盖 contracts 错误映射边界。
3. 测试发现性：已通过 `ctest -N -R ...` 回填 unit/contract 发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 IConfigValidator 头文件、unit/contract 测试、CMake 注册与 TODO 证据文档。

## 16. 本轮执行记录（2026-03-31 / CFG-TODO-004）

### 16.1 选中任务

1. 本轮任务：CFG-TODO-004。
2. 可执行性依据：CFG-TODO-006 已冻结 ConfigSnapshot，CFG-TODO-004 仅需冻结 commit/current/history/LKG 的接口边界，不依赖持久化后端选型。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.3 已明确 ConfigSnapshotStore 输出 current、history 与 LKG，并要求支持原子切换。
2. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已冻结 commit(snapshot)、get_current()、get_by_version(version)、get_last_known_good() 四个入口。
3. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.7/6.8 已明确校验通过后快照会标记为 current 与 LKG，失败路径需可回退到 LKG。

外部参考：

1. Azure App Configuration point-in-time snapshot 文档强调配置历史需可按时间线回看、恢复旧值并支持回滚预览；本轮据此把 SnapshotStore 结果收敛为 current_version 与 last_known_good_version 的最小版本语义，而不提前冻结具体持久化后端。

D 结论：

1. Design -> Build 映射：新增 IConfigSnapshotStore.h，冻结 ConfigSnapshotCommitResult 与 IConfigSnapshotStore 四个快照存取入口。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/IConfigSnapshotStore.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigSnapshotStoreInterfaceTest，覆盖 commit/get_current/get_by_version/get_last_known_good 的最小正负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci -N -R ConfigSnapshotStoreInterfaceTest && ctest --test-dir build-ci --output-on-failure -R ConfigSnapshotStoreInterfaceTest。
3. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigSnapshotStore.h：新增 ConfigSnapshotCommitResult 与四个快照存取接口。
2. tests/unit/infra/ConfigSnapshotStoreInterfaceTest.cpp：覆盖版本单调递增、history 查询与 LKG 读取正例，以及 invalid snapshot/non-monotonic commit 负例。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件与 unit 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests`：通过；unit 44/44 全部通过。
3. `ctest --test-dir build-ci -N -R ConfigSnapshotStoreInterfaceTest`：通过，发现 1 个测试：`ConfigSnapshotStoreInterfaceTest`。
4. `ctest --test-dir build-ci --output-on-failure -R ConfigSnapshotStoreInterfaceTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：新增接口与提交结果对象命名已直接表达 current/LKG/version 语义，无需额外注释。
2. 正负例覆盖：unit 覆盖 commit/history/LKG 正例与 invalid snapshot/non-monotonic commit 负例。
3. 测试发现性：已通过 `ctest -N -R ConfigSnapshotStoreInterfaceTest` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 IConfigSnapshotStore 头文件、unit 测试、CMake 注册与 TODO 证据文档。

## 17. 本轮执行记录（2026-03-31 / CFG-TODO-005）

### 17.1 选中任务

1. 本轮任务：CFG-TODO-005。
2. 可执行性依据：CFG-TODO-006 已冻结 ConfigDiff，CFG-TODO-005 仅需冻结发布入口与最小回执，不依赖事件总线抽象实现细节。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.3 已明确 ConfigPublisher 输入为已通过校验的新快照或差异内容，输出订阅事件。
2. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已冻结 IConfigPublisher 唯一入口 publish_config_changed(diff)。
3. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.7/6.10 已明确 ConfigChanged 事件必须带版本变化和可观测结果。

外部参考：

1. Azure App Configuration event 文档强调配置变更事件应具备唯一事件 ID、明确事件类型，并向订阅者推送而不是轮询；本轮据此把发布回执收敛为 event_id 与 delivered_subscriber_count，而不提前冻结跨进程总线细节。

D 结论：

1. Design -> Build 映射：新增 IConfigPublisher.h，冻结 ConfigPublishResult 与 publish_config_changed(diff) 接口。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/IConfigPublisher.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigPublisherInterfaceTest，覆盖合法 diff 正例与 invalid diff 负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests && ctest --test-dir build-ci -N -R ConfigPublisherInterfaceTest && ctest --test-dir build-ci --output-on-failure -R ConfigPublisherInterfaceTest。
3. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. infra/include/config/IConfigPublisher.h：新增 ConfigPublishResult 与 publish_config_changed(diff) 接口。
2. tests/unit/infra/ConfigPublisherInterfaceTest.cpp：覆盖合法 diff 正例与 invalid diff 负例。
3. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成头文件与 unit 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests`：通过；unit 45/45 全部通过。
3. `ctest --test-dir build-ci -N -R ConfigPublisherInterfaceTest`：通过，发现 1 个测试：`ConfigPublisherInterfaceTest`。
4. `ctest --test-dir build-ci --output-on-failure -R ConfigPublisherInterfaceTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：新增接口与发布回执对象命名已直接表达 event_id/发布计数语义，无需额外注释。
2. 正负例覆盖：unit 覆盖合法 diff 正例与 invalid diff 负例。
3. 测试发现性：已通过 `ctest -N -R ConfigPublisherInterfaceTest` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 IConfigPublisher 头文件、unit 测试、CMake 注册与 TODO 证据文档。

## 18. 本轮执行记录（2026-03-31 / CFG-TODO-012）

### 18.1 选中任务

1. 本轮任务：CFG-TODO-012。
2. 可执行性依据：CFG-TODO-001 与 CFG-TODO-003 已冻结 config 边界与校验接口，CFG-TODO-012 仅需补齐本地错误码域和映射矩阵，可通过 contract 测试直接固化。

### 18.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.6 已明确 8 个建议错误码，6.8 已明确读取故障、语义故障、安全故障、应用故障四类异常语义。
2. infra/include/InfraErrorCode.h 与 infra/src/InfraErrorCode.cpp 已冻结 infra 私有错误码的映射模式：本地 enum + mapping struct + 名称函数 + contracts::ResultCode 映射函数。
3. tests/unit/infra/InfraErrorCodeTest.cpp 与 tests/contract/smoke/InfraErrorCodeBoundaryContractTest.cpp 已提供“名称稳定 + contract 映射冻结”的最小可执行样板。

外部参考：

1. Azure App Configuration REST 文档明确配置读取会返回 404 Not Found，条件更新/锁定场景会返回 409 Conflict；本轮据此将缺失键与冲突语义冻结为独立本地错误码，而不是混入实现细节。
2. JSON Schema type/reference 文档明确 `type` 关键字用于约束实例类型且类型不匹配会导致校验失败；本轮据此将 TYPE_MISMATCH 与 INVALID_SCHEMA 收敛到 contracts 验证类结果码。

D 结论：

1. Design -> Build 映射：新增 ConfigErrors.h，冻结 ConfigErrorCode、ConfigErrorMapping、config_error_code_name 与 map_config_error_code。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/ConfigErrors.h，并接入 infra/CMakeLists.txt 的 PUBLIC_HEADER。
   - 测试目标：新增 ConfigErrorsTest 覆盖 8 个错误码名称稳定性与映射覆盖；新增 ConfigErrorMappingContractTest 逐项固化映射矩阵与命名边界。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N -R "ConfigErrorsTest|ConfigErrorMappingContractTest" && ctest --test-dir build-ci --output-on-failure -R "ConfigErrorsTest|ConfigErrorMappingContractTest"。
3. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. infra/include/config/ConfigErrors.h：新增 8 个 config 私有错误码、名称函数与 contracts::ResultCode 映射函数。
2. tests/unit/infra/ConfigErrorsTest.cpp：覆盖错误码名称稳定性、映射覆盖完整性与 reason 非空约束。
3. tests/contract/smoke/ConfigErrorMappingContractTest.cpp：逐项冻结 8 个错误码到既有 contracts::ResultCode 的映射矩阵，并校验命名仍留在 INF_CFG_E_* 本地命名域。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt：完成头文件与 unit/contract 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G Ninja`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests dasall_contract_tests`：通过；unit 46/46、contract 97/97 全部通过，新增 `ConfigErrorsTest` 与 `ConfigErrorMappingContractTest` 均被编译并执行。
3. `ctest --test-dir build-ci -N -R "ConfigErrorsTest|ConfigErrorMappingContractTest"`：通过，发现 2 个测试，分别为 `ConfigErrorsTest` 与 `ConfigErrorMappingContractTest`。
4. `ctest --test-dir build-ci --output-on-failure -R "ConfigErrorsTest|ConfigErrorMappingContractTest"`：通过，2/2 tests passed。

Build 合规复核：

1. 代码注释：错误码命名和 mapping reason 已直接表达异常语义，无需新增冗余注释。
2. 正负例覆盖：unit 覆盖 8 个冻结错误码名称与映射覆盖；contract 逐项冻结映射矩阵并校验边界命名空间。
3. 测试发现性：已通过 `ctest -N -R "ConfigErrorsTest|ConfigErrorMappingContractTest"` 回填发现性证据。
4. TODO 证据回写：已回写任务状态、交付物、发现性结果与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 ConfigErrors 头文件、unit/contract 测试、CMake 注册与 TODO 证据文档。

## 19. 本轮执行记录（2026-04-02 / CFG-TODO-007）

### 19.1 选中任务

1. 本轮任务：CFG-TODO-007。
2. 可执行性依据：CFG-TODO-001、CFG-TODO-006 已完成，IConfigCenter 与 ConfigTypes 已冻结，可在不提前实现 Loader/Merger/Validator/SnapshotStore 的前提下先落 Facade 生命周期骨架，并用最小 startup context 占位满足 6.7 主链入口。

### 19.2 研究与 Design 结论

本地证据：

1. docs/architecture/DASALL_infra_config模块详细设计方案.md 6.2/6.7 已明确 ConfigCenterFacade 是统一配置入口，职责是串接启动加载、类型化查询、运行期覆盖与回滚入口。
2. infra/include/config/IConfigCenter.h 已冻结 load_layers、get_typed、apply_override、rollback、subscribe 五个入口，以及 startup context、rollback token、subscription request/handle 的最小边界。
3. infra/include/config/ConfigTypes.h 与 infra/include/config/ConfigErrors.h 已冻结 TypedConfig、ConfigSnapshot、ConfigPatch、ConfigDiff、ConfigApplyResult 及 config 错误映射，足以支撑 Facade 先落单体骨架并保持 contracts 错误语义不外溢。

外部参考：

1. Azure Architecture Center 的 External Configuration Store pattern 强调配置访问层应提供一致、易用的接口，暴露 typed/structured 配置、支持多版本与缓存/回退，并在启动期无法访问外部配置时提供 last known values fallback；本轮据此把 Facade 收敛为“内存 bootstrap snapshot + typed lookup + runtime override + rollback token”的最小可执行骨架，而不提前冻结外部源适配细节。

D 结论：

1. Design -> Build 映射：新增 ConfigCenterFacade.h/ConfigCenterFacade.cpp，落最小 bootstrap snapshot、typed query、runtime override、rollback 与 namespace-filtered subscription 骨架。
2. Build 三件套：
   - 代码目标：新增 infra/include/config/ConfigCenterFacade.h、infra/src/config/ConfigCenterFacade.cpp，并接入 infra/CMakeLists.txt。
   - 测试目标：新增 tests/unit/infra/ConfigCenterFacadeTest.cpp，覆盖未初始化路径、最小 bootstrap 链路、runtime override/rollback 闭环，以及 stale base_version 冲突负例。
   - 验收命令：cmake --build build-ci --target dasall_infra && ctest --test-dir build-ci -R ConfigCenterFacadeTest。
3. D Gate：PASS。

### 19.3 Build 交付与证据

交付物：

1. infra/include/config/ConfigCenterFacade.h：新增 ConfigCenterFacade public header，承接 IConfigCenter 并定义最小 lifecycle state、rollback record 与 subscription record。
2. infra/src/config/ConfigCenterFacade.cpp：实现 bootstrap snapshot 主链、typed lookup、runtime override、rollback 与 namespace filter 通知骨架，失败路径统一走 ConfigErrors 到 contracts::ResultCode 的映射。
3. tests/unit/infra/ConfigCenterFacadeTest.cpp：覆盖未初始化拒绝路径、bootstrap 成功路径、runtime override/rollback 正例，以及 stale base_version conflict 负例。
4. infra/CMakeLists.txt、tests/unit/infra/CMakeLists.txt、tests/unit/CMakeLists.txt：完成 config facade 源文件、public header 与 unit 测试注册。

验收结果：

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。说明：本轮先清理了前次失败遗留的 Ninja 缓存，并使用当前环境可用的 Makefiles 生成器恢复 build-ci。
2. `cmake --build build-ci --target dasall_infra dasall_unit_tests`：通过；新增 ConfigCenterFacade.cpp 与 ConfigCenterFacadeTest 均编译成功。
3. `ctest --test-dir build-ci -N -R ConfigCenterFacadeTest`：通过，发现 1 个测试：`ConfigCenterFacadeTest`。
4. `ctest --test-dir build-ci --output-on-failure -R ConfigCenterFacadeTest`：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：Facade 状态、快照与 patch/rollback 语义可由对象命名与边界类型直接表达，无需新增冗余注释。
2. 正负例覆盖：unit 覆盖 bootstrap/query/override/rollback 正例，以及未初始化、invalid startup context、stale base_version、invalid rollback token 负例。
3. 测试发现性：已通过 `ctest -N -R ConfigCenterFacadeTest` 回填新增测试注册证据。
4. TODO 证据回写：已回写任务状态、交付物、环境恢复动作与验收结果摘要。
5. 提交隔离：本轮提交范围限定为 ConfigCenterFacade 代码、测试、CMake 注册与 TODO 证据文档，不混入后续 008~011 改动。
