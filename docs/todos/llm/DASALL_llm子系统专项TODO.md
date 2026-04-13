# DASALL LLM 子系统专项 TODO

最近更新时间：2026-04-13（完成 LLM-TODO-001/002/003/004/005/006/007/008/009/010/011/012/013/014/015/016/017/018/019/020/021/022/023/024/039/040，阶段 B 公共接口冻结、阶段 C 测试夹具升级、阶段 D Prompt/Provider 资产基线、阶段 E PromptRegistry/TokenEstimator/TemplateRenderer/PromptComposer/PromptPolicy/PromptPipeline 与阶段 F ModelRouter/AdapterRegistry/CallExecution/ResponseNormalizer/UsageAggregator/LLMManager 已落盘）
阶段：Detailed Design -> Special TODO
适用范围：llm/
当前结论：可直接进入 L3/L2 混合执行；module-local 公共接口、Prompt/Provider 资产、Prompt 三段治理、路由与 unary 主链路可落地，shared ModelRoute / PromptPolicyDecision 升格与 streaming 生命周期保持 Blocked/后置。

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_llm子系统详细设计.md
2. docs/architecture/DASALL_Agent_architecture.md
3. docs/architecture/DASALL_Engineering_Blueprint.md
4. docs/adr/ADR-005-architecture-review-baseline.md
5. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
6. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
7. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
8. docs/ssot/InfraConcurrencyPolicy.md
9. docs/ssot/InfraIntegrationTopology.md
10. docs/plans/DASALL_工程落地实现步骤指引.md
11. docs/development/DASALL_工程协作与编码规范.md
12. 可参考的现有 TODO / 交付基线：docs/todos/contracts/deliverables、docs/todos/infrastructure/deliverables、docs/todos/platform/deliverables、docs/todos/services/deliverables
13. 当前 LLM TODO 参考：docs/todos/llm/DASALL_llm子系统TODO落地实施步骤指引.md（仅用于核对既有任务命名与测试出口，不替代本专项 TODO）
14. 当前代码与测试现状：CMakeLists.txt、llm/CMakeLists.txt、llm/include/、llm/src/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/integration/CMakeLists.txt、tests/unit/llm/CMakeLists.txt、tests/mocks/include/MockLLMAdapter.h、build-ci/

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 llm 子系统边界扩张到 memory、runtime、tools、services 的无关实现。
3. 纯讨论事项不伪装为 Done-ready Build 任务。
4. 每项任务都必须具备代码目标、测试目标、验收命令三件套。
5. 设计证据不足处先列 Blocked 或补设计前置项，不伪造实现任务。
6. 现有 docs/todos/llm/DASALL_llm子系统TODO落地实施步骤指引.md 保留为 v1.0 执行参考；本专项 TODO 负责正式门禁、状态与交付证据编排。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 将 llm 从 placeholder-only 工程骨架收敛为“统一模型调用 + Prompt 治理 + 可观测 + 可验证”的 Layer 5 支撑子系统。
2. 为 cognition / runtime 提供稳定的 llm 模块公共接口，且严格保持 ContextPacket 语义装配权在 memory、恢复裁定权在 runtime、工具权限裁定权在 Tool Policy Gate。
3. 以 module-local supporting types 落地 Prompt 三段治理、模型路由、Provider Catalog、Token 预算与 usage 成本归并，避免反向污染 shared contracts。
4. 建立可执行的 Build/Test/Gate 顺序，使 llm 能在不突破 ADR 边界的前提下逐步打通 unary 主链路、failover、Prompt 资产切换与 profile 差异验证。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. llm/include 公共接口面与 llm/src 内部实现骨架。
2. PromptRegistry、PromptComposer、PromptPolicy、PromptPipeline 及其 supporting types。
3. ModelRouter、AdapterRegistry、ResponseNormalizer、UsageAggregator、LLMManager unary 主链路。
4. Prompt 资产与 Provider 资产的 baseline 形态、加载器、CMake 接线、测试拓扑与 observability bridge。
5. llm 相关 unit / integration / contract 不回退门禁与证据回写。

不纳入本专项 TODO 的对象：

1. ContextOrchestrator 的检索、压缩、预算裁剪和写回闭环。
2. Runtime 的恢复准入、重试裁定、最终失败收敛与主循环调度。
3. Tool 执行授权、确认门、审批与真实权限控制。
4. shared contracts 的直接扩张；shared ModelRoute、shared PromptPolicyDecision、shared StreamHandle 只允许进入评审门，不默认推进。
5. provider SDK 的完整生产联调；当前阶段只要求 adapter skeleton、协议映射和最小可测 transport 抽象。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| LLM-TC001 | 架构 5.4；蓝图 3.5；详细设计 1.1、6.1 | Must | llm 是 Layer 5 支撑子系统，负责统一模型调用与 Prompt 治理 | 任务必须围绕 llm/include、llm/src、llm/assets、tests/llm 和 CMake 接线展开 |
| LLM-TC002 | ADR-006；详细设计 1.2、6.5.4、6.13 | Must-Not | llm 不拥有 ContextPacket 组装权，不得自行检索、压缩、改写语义上下文 | 不生成 memory 检索、上下文压缩、历史筛选实现任务 |
| LLM-TC003 | 架构 3.4、5.4；详细设计 6.8、6.13 | Must-Not | llm 输出的是受控语义结果，不是可直接执行的命令 | 不生成 tools/services 执行或权限裁定任务 |
| LLM-TC004 | 架构 4.7、5.4；详细设计 6.2、6.5.1、6.15.4 | Must | adapter 只负责协议与语义映射，不承载治理策略 | PromptPolicy、ModelRouter、adapter skeleton 必须分任务推进 |
| LLM-TC005 | ADR-007；详细设计 6.9.2 | Must | llm 只返回失败分类、attempt trace 和 retryable hint，不持有恢复裁定权 | LLMManager/ResponseNormalizer 任务只能做到失败收敛事实，不延展恢复执行 |
| LLM-TC006 | ADR-005；详细设计 6.4.2、7.2、10.1 | Must | shared contracts 未补齐前，supporting types 保持 module-local | shared ModelRoute、PromptPolicyDecision、StreamHandle 相关事项只能列评审或 Blocked |
| LLM-TC007 | 详细设计 6.5.1 | Must | ILLMAdapter.generate() 返回 AdapterCallResult，不抛异常 | adapter 公共接口与测试必须验证非异常错误传播 |
| LLM-TC008 | 详细设计 6.5.6 | Must | PromptPipeline 固定按 select -> compose -> evaluate 顺序编排，且不做模型调用 | PromptPipeline 必须是独立编排任务，不与 LLMManager 合并 |
| LLM-TC009 | 详细设计 6.10.1、6.15.2；蓝图 8.6 | Must | llm 配置必须映射到 ConfigCenter 四层模型，不得自建平行配置系统 | LLMSubsystemConfig 与资产加载器必须显式消费 profile / deployment / runtime 投影 |
| LLM-TC010 | 详细设计 6.11；SSOT 并发策略 | Must | Prompt/Provider catalog 使用 immutable snapshot + atomic swap；禁止持 L2 锁执行 I/O | PromptAssetRepository、Provider Catalog、AdapterRegistry 健康快照任务必须回链 SSOT |
| LLM-TC011 | 蓝图 5.1、5.2；详细设计 6.10、9.5 | Must | llm 路由、Prompt allowlist、timeout、degrade 均来自 profiles 生效视图 | 必须包含 LLMSubsystemConfig 投影与 profile integration 验证任务 |
| LLM-TC012 | 编码规范 3.2、3.6、3.7 | Must | 公共接口放 include/；失败必须可观测；新增公共接口至少补 1 个 unit 或 contract 测试 | 每个接口/治理/路由任务都必须绑定测试出口 |
| LLM-TC013 | InfraIntegrationTopology SSOT；详细设计 9.3 | Must | 新增核心链路后必须至少有 1 个 integration smoke 用例被 ctest -N 发现 | integration 拓扑注册必须前置，且不能只停留在文档层 |
| LLM-TC014 | 当前代码现状；详细设计 3.1、3.2 | Must | 当前 llm/include 为空、llm/src 仅 placeholder，工程仍处于骨架阶段 | 任务顺序必须先解构建/测试骨架，再进入实现 |
| LLM-TC015 | 当前 Mock 现状；详细设计 6.13 | Must | MockLLMAdapter 仍是 string->string 脚手架，不能直接支撑 ILLMAdapter 主链路 | mock 升级是 unary 主链路和 integration fixture 的前置任务 |
| LLM-TC016 | 详细设计 6.12、9.6 | Must | llm 进入核心链路前必须具备日志、指标、trace、audit 四类观测锚点 | observability 不能后置到“代码跑通以后再补” |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| llm/CMakeLists.txt | 已定义 dasall_llm，但仅编译 src/placeholder.cpp；PUBLIC include 指向空的 llm/include | llm 已纳入顶层构建图，但仍是 placeholder-only |
| llm/include/ | 目录存在但为空 | 模块公共接口面尚未落盘 |
| llm/src/ | 仅有 placeholder.cpp | prompt / route / adapters / execution / observability 目录尚未落盘 |
| llm/assets/prompts/ | 不存在 | PromptAssetRepository 目前没有 baseline Prompt 资产根 |
| llm/assets/providers/ | 不存在 | Provider Catalog 目前没有 baseline Provider 资产根 |
| tests/unit/llm/CMakeLists.txt | 仅占位注释 | llm unit discoverability 缺失 |
| tests/integration/llm/ | 目录不存在；tests/integration/CMakeLists.txt 未接 llm 子目录 | llm integration discoverability 缺失 |
| tests/mocks/include/MockLLMAdapter.h | 仅提供 string 输入/输出的 invoke()，不继承 ILLMAdapter | 无法直接覆盖 unary 主链、fallback 与 health_check |
| tests/CMakeLists.txt | 已提供 dasall_unit_tests、dasall_contract_tests、dasall_integration_tests 聚合目标 | llm 不需要新造顶层 target，只需接线到现有测试体系 |
| build-ci/ | 已存在可复用配置与 ctest 入口 | 可作为本专项统一验收基线；CMake Tools 失灵时可退回显式 cmake/ctest |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：LLM 当前可直接生成 L3/L2 混合专项 TODO，不能整体按纯 L3 推进。

当前最细可执行粒度：

1. L3：module-local 公共接口、supporting types、PromptRegistry 选择面、PromptPolicy 治理输入输出、TokenEstimator、UsageAggregator、unit/integration 注册点。
2. L2：PromptAssetRepository、Provider Catalog 加载器、PromptComposer、PromptPipeline 实现、ModelRouter、AdapterRegistry、ResponseNormalizer、LLMManager、adapter skeleton、observability bridge。
3. L0：shared ModelRoute / PromptPolicyDecision / StreamHandle admission，streaming 生命周期冻结。

证据：

1. 详细设计 6.4.2、6.4.3、6.5 已明确核心接口、supporting types、方法语义、返回语义与非职责边界。
2. 详细设计 6.7、6.9 已明确正常路径、over-budget 路径、failover 路径和错误分类。
3. 详细设计 6.6、8.1、9.1、9.3 已给出建议代码路径、资产形态、测试出口与 CMake 落点。
4. 详细设计 7.1 已给出 Design -> Build 映射表，可直接转换为专项 TODO 任务源。
5. 当前阻塞主要集中在工程骨架、测试 discoverability、shared ABI 升格与 streaming，而不是 llm 核心职责本身不清晰。

### 4.2 可落盘对象提取表（Step 2 输出）

| 类别 | 可落盘对象 | 设计锚点 | 建议落位 | 当前状态 |
|---|---|---|---|---|
| 公共接口 | ILLMAdapter、ILLMManager、IPromptRegistry、IPromptComposer、IPromptPolicy、IPromptPipeline | 6.5.1~6.5.6 | llm/include/；llm/include/prompt/ | 全部未落盘 |
| module-local supporting types | LLMAdapterConfig、LLMGenerateRequest、LLMManagerResult、PromptQuery、PromptRegistryResult、PromptPolicyDecision、PromptPolicyInput、ModelBudgetHint、ResolvedModelRoute、ModelSelectionHint、StreamSessionRef、TokenEstimate、NormalizedUsageRecord、ProviderDescriptor、ModelCatalogEntry | 6.4.2、6.4.3 | llm/include/；llm/include/prompt/；llm/include/route/；llm/include/provider/；llm/include/stream/ | 全部未落盘 |
| Prompt 资产模型 | Prompt package manifest、system.md、task.md、policy_notes.md、few_shots/ | 6.6.1、6.6.1a、6.15.5 | llm/assets/prompts/ | 目录不存在 |
| Provider 资产模型 | catalog.yaml、provider manifest.yaml、models.yaml、pricing/source/effective_at/verification_state 元数据 | 6.6.4、6.10.1、6.15.2 | llm/assets/providers/ | 目录不存在 |
| 错误与状态语义 | PromptPolicyDisposition、LLMFailureCategory、fallback_used、attempted routes、health snapshot | 6.4.3、6.9、6.15.6 | llm/include/；llm/src/route/；llm/src/execution/ | 未落盘 |
| 主流程实现点 | PromptPipeline.run()、ModelRouter.resolve()、AdapterRegistry registry/health、ResponseNormalizer normalize、UsageAggregator aggregate、LLMManager.generate() | 6.7、6.15.1~6.15.8 | llm/src/prompt/；llm/src/route/；llm/src/execution/；llm/src/ | 未落盘 |
| unit 测试出口 | LLMInterfaceSurfaceTest、MockLLMAdapterSurfaceTest、PromptAssetPackageParseTest、ProviderCatalogParseTest、PromptRegistrySelectionTest、TokenEstimatorTest、TemplateRendererTest、PromptComposerSlotMappingTest、PromptPolicyAllowlistTest、ModelRouterPolicyTest、LLMManagerSuccessPathTest、LLMManagerTimeoutPolicyTest、LLMManagerRetryBudgetTest、LLMManagerConcurrencyGuardTest、ResponseNormalizerReasoningContentStripTest、ProviderConfigProjectionTest、AdapterProtocolMappingTest、LLMObservabilityFieldCompletenessTest、LLMAuditEventCoverageTest | 7.1、9.1；现有 v1.0 指引 | tests/unit/llm/ | 目录未接线 |
| integration 测试出口 | LLMSubsystemSmokeIntegrationTest、DeepSeekDualModeSelectionIntegrationTest、LLMFallbackIntegrationTest、LLMPromptSourceSwitchIntegrationTest、LLMPersonaSelectionIntegrationTest、LLMGovernanceFailureIntegrationTest、LLMProfileIntegrationTest、LLMProviderAssetOnboardingIntegrationTest | 9.3；现有 v1.0 指引 | tests/integration/llm/ | 目录不存在 |
| CMake / 注册点 | llm/CMakeLists.txt、tests/unit/llm/CMakeLists.txt、tests/integration/llm/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/integration/CMakeLists.txt | 6.6、8.1、9.6 | 仓库现有 CMake 拓扑 | llm 侧未接入真实源文件和测试目标 |

### 4.3 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| llm 公共 include 面 | 6.6、7.1 | L3 | 目录建议、头文件清单、命名规则明确 | 代码尚未落盘 | 直接拆为 include 布局、接口骨架与测试拓扑任务 |
| ILLMAdapter + LLMAdapterConfig + AdapterCallResult | 6.4.2、6.5.1 | L3 | 方法名、返回语义、错误传播方式已冻结 | transport 抽象只到建议级 | 先冻结 SPI 与 module-local 结果类型，transport 作为 adapter 内部细节 |
| ILLMManager + LLMGenerateRequest + LLMManagerResult | 6.4.2、6.5.2 | L3 | 输入输出、health_check、fallback_used 语义明确 | 无实质缺口 | 直接拆到接口/数据结构任务 |
| IPromptRegistry + PromptQuery + PromptRegistryResult | 6.4.2、6.5.3、6.15.5 | L3 | 选择面、输入维度、返回元数据明确 | 具体匹配算法细节由实现决定 | 直接拆接口与选择逻辑任务 |
| IPromptComposer + ModelBudgetHint | 6.5.4、6.6.1a | L3 | 输入输出、budget hint、over-budget warning 明确 | 仅剩渲染器安全实现待落盘 | 先冻结接口，再把 TemplateRenderer 安全规则独立成任务，最后落 PromptComposer 主流程 |
| IPromptPolicy + PromptPolicyDecision/Input | 6.4.3、6.5.5、6.15.3 | L3 | 决策枚举、执行顺序、fail-closed 原则明确 | redaction 细节以实现收敛 | 直接拆接口与治理实现任务 |
| IPromptPipeline + PromptPipelineResult | 6.5.6 | L3 | 设计动机、固定顺序、禁止事项与测试门明确 | 无实质缺口 | 直接拆 facade 接口与编排实现任务 |
| ProviderDescriptor + ModelCatalogEntry | 6.4.2、6.6.4、6.15.2 | L3 | 字段、truth-source、pricing/context 约束明确 | 无实质缺口 | 先冻结 supporting types，再做资产加载器 |
| PromptAssetRepository + Prompt 资产 | 6.6.1、6.6.2、6.15.5 | L2 | 资产目录、overlay 顺序、坏包回退原则明确 | 资产解析与模板渲染仍需分任务落盘 | 先做仓储与 baseline 资产，TemplateRenderer 安全规则单列任务 |
| Provider Catalog 加载器 + Provider 资产 | 6.6.4、6.10.1、6.15.2 | L2 | baseline / deployment / snapshot 三层、verification_state、truth-source 已明确 | 真实下载通道不在本阶段 | 先做 baseline 加载器与本地资产 |
| TokenEstimator | 6.15.7 | L3 | 输入输出、近似算法、安全余量、消费者明确 | 无实质缺口 | 直接拆独立实现任务 |
| TemplateRenderer 安全渲染器 | 6.6.1a | L2 | simple_var 语法、安全边界、warning/截断与单轮替换规则明确 | 仅缺少工程落盘与专门测试 | 先拆成独立渲染器任务，再由 PromptComposer 复用 |
| PromptComposer 实现 | 6.5.4、6.6.1a、6.15.5 | L2 | 槽位装配、few-shot、warning、budget 约束明确 | 依赖安全渲染器先落盘 | 与 TemplateRenderer 分拆，聚焦装配流程与 over-budget 行为 |
| PromptPolicy 实现 | 6.5.5、6.15.3 | L3 | 治理顺序与输出枚举明确 | 无实质缺口 | 直接拆实现任务 |
| ModelRouter | 6.15.1 | L2 | 候选集装配、硬过滤、评分、fallback 展开顺序明确 | internal score card 未冻结 | 拆为组件级实现与单测，不扩张 shared route |
| AdapterRegistry | 6.2、6.6、6.11 | L2 | 生命周期、health snapshot、copy-on-write 约束明确 | 真实 probe sink 细节未单列 | 先做 registry + snapshot，不做复杂 exporter 绑定 |
| Call execution 治理 | 6.7、6.9.2、6.10、6.11、6.15.6 | L2 | timeout_policy、retry budget、bounded semaphore、degrade/fallback 顺序明确 | 代码仍需从 LLMManager 编排里显式拆出测试目标 | 单列执行治理任务，避免与 LLMManager 编排混成一个主目标 |
| ResponseNormalizer | 6.15.4 | L2 | 语义归一化与 provider-private 剥离边界明确 | 各 provider raw payload 只到族级说明 | 先做 shared 语义收口，不细化到所有厂商私有分支 |
| UsageAggregator | 6.15.8 | L3 | 输入字段、cost 估算、禁止事项与测试门明确 | 无实质缺口 | 直接拆实现任务 |
| ProviderConfig 投影与实例接入 | 6.6.4、6.10.1、6.14、6.15.2 | L2 | auth_ref、header_refs、base_url alias、activation flag 与 asset-only onboarding 边界明确 | 真实注入链与验证用例尚未落盘 | 单列 provider config projection 与 asset-only onboarding 任务 |
| OpenAICompatible / Ollama / Local adapters | 6.6、6.14、7.1 | L2 | family 边界、init/generate/health_check 语义明确 | 真实 endpoint/secret 联调未接线 | 先做 skeleton 与 protocol mapping 测试 |
| observability bridges | 6.12、7.1、9.6 | L2 | 必填字段、指标、span、audit 场景明确 | 具体 sink 适配接口以 infra 现有实现对齐 | 拆为统一 observability 接线任务 |
| streaming 生命周期 | 6.4.2、7.2、10.1 | L0 | 只有占位方向与后置建议 | 取消、背压、共享 handle、ownership 未冻结 | 保持 Blocked，不进入首轮 Build |
| shared supporting object admission | 7.2、10.1、12.1 | L0 | 升格风险和迁移路径已有结论 | 消费者矩阵、contract tests、兼容窗口未齐 | 只列 Review Gate，继续 module-local |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| llm 公共 include 面与测试拓扑 | 6.6、8.1、9.6 | 目录 / CMake / 测试注册 | LLM-TODO-001、002、003、004 | 先解构建与测试 discoverability，再进入核心实现 |
| Adapter SPI 与 manager facade | 6.4.2、6.5.1、6.5.2 | 接口定义 | LLM-TODO-005、006 | 冻结 adapter 与 manager 公共面，确保错误语义不漂移 |
| Prompt 三段治理公共面 | 6.5.3~6.5.6 | 接口定义 | LLM-TODO-007、008、009、010 | 分别冻结 Registry / Composer / Policy / Pipeline 的职责和输入输出 |
| route / provider / stream / usage supporting types | 6.4.2、6.4.3 | 数据结构定义 | LLM-TODO-011 | 统一落 module-local supporting types，不推进 shared contracts |
| LLMSubsystemConfig 配置投影 | 6.10、8.2 | 配置与 Profile 裁剪 | LLM-TODO-012 | 从 RuntimePolicySnapshot / profile 投影 llm 消费视图 |
| Prompt 资产与 PromptAssetRepository | 6.6.1、6.6.2、6.15.5 | 资产 / 仓储 | LLM-TODO-013 | 先落 baseline Prompt 资产和仓储解析器 |
| Provider Catalog 与 provider 资产 | 6.6.4、6.10.1、6.15.2 | 资产 / 仓储 | LLM-TODO-014 | 先落 baseline Provider Catalog 和 truth-source 约束 |
| Prompt 三段治理实现 | 6.15.3、6.15.5、6.5.6 | 组件实现 | LLM-TODO-015、016、039、017、018、019 | 先 registry，再 token 预算，再模板安全渲染，再 composer / policy / pipeline |
| 路由与调用核心 | 6.15.1、6.15.4、6.15.6、6.15.8 | 生命周期 / 路由 / 结果映射 | LLM-TODO-020、021、040、022、023、024 | 完成 route -> registry -> call execution -> normalize -> aggregate -> manager unary 主链 |
| Provider 注入与实例接入 | 6.10.1、6.14、6.15.2 | 配置投影 / 资产式接入 | LLM-TODO-041、042 | 打通 auth_ref/header_refs/base_url alias 投影，并验证既有 family 支持只加 Provider 资产接入 |
| provider adapter skeleton | 6.14、7.1 | 适配器实现 | LLM-TODO-025、026、027 | 分 family 落骨架，避免一个任务混入三类协议主目标 |
| 可观测与质量门 | 6.12、9.6 | observability / Gate | LLM-TODO-028、038 | 字段完整性、审计事件、指标、trace 与 Gate 回写 |
| integration smoke / failure / profile / prompt switch | 9.3、9.4、10.3 | 集成测试与门禁 | LLM-TODO-029、030、031、032、033、034、035、042 | 至少覆盖 smoke、dual-mode、fallback、Prompt source、persona、governance、profile 与 asset-only onboarding |
| streaming 与 shared admission 后置项 | 7.2、10.1、12.1 | 补设计 / 评审门 | LLM-TODO-036、037 | 明确不可直接推进的边界，防止 Build 越权 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | LLM-TODO-005、006、007、008、009、010 |
| 数据结构定义类任务 | 是 | LLM-TODO-011 |
| 生命周期与初始化类任务 | 是 | LLM-TODO-012、021、024、040、041 |
| 适配器 / 桥接类任务 | 是 | LLM-TODO-022、025、026、027、028、041 |
| 异常与错误处理类任务 | 是 | LLM-TODO-005、009、022、024、040 |
| 模板安全与注入防护类任务 | 是 | LLM-TODO-039 |
| 配置与 Profile 裁剪类任务 | 是 | LLM-TODO-012、014、035、041、042 |
| 测试与门禁类任务 | 是 | LLM-TODO-002、003、004、029、030、031、032、033、034、035、042 |
| 文档 / 交付证据回写类任务 | 是 | LLM-TODO-038 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LLM-TODO-001 | Done | 新增 llm 公共 include 布局 | 详细设计 6.6、8.1；蓝图 3.5；编码规范 3.2 | 6.6 目录与工程落点建议 | L2 | llm/include/；llm/include/prompt/；llm/include/provider/；llm/include/route/；llm/include/stream/；llm/CMakeLists.txt | llm 模块公共 include 根与稳定子目录 | unit：为 LLMInterfaceSurfaceTest 提供稳定 include 根 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm` | 无 | LLM-BLK-001 | 建立 include 根且不破坏 dasall_llm 现有构建 | llm/include 目录骨架、llm/CMakeLists.txt 接线调整、docs/todos/llm/deliverables/LLM-TODO-001-llm公共include布局设计收敛.md | 仅当 llm 公共 include 根稳定存在且 dasall_llm 可继续构建时完成 |
| LLM-TODO-002 | Done | 注册 llm unit 测试拓扑 | 详细设计 8.1、9.1；当前 tests/unit/llm 现状 | 8.1 测试目录建议；9.1 单元测试范围 | L2 | tests/unit/llm/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/llm/InterfaceSurfaceTest.cpp | llm unit discoverability 入口 | unit：ctest -N 可发现 llm unit 入口，且带 unit 标签 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N` | LLM-TODO-001 | LLM-BLK-002 | tests/unit/llm 被顶层 unit 聚合 target 发现 | llm unit CMake 接线、InterfaceSurfaceTest 占位/骨架、docs/todos/llm/deliverables/LLM-TODO-002-llm-unit测试拓扑注册设计收敛.md | 仅当 ctest -N 能发现至少 1 个 llm unit 用例时完成 |
| LLM-TODO-003 | Done | 注册 llm integration 测试拓扑 | 详细设计 9.3、9.6；InfraIntegrationTopology | 9.3 集成测试路径；9.6 Gate 建议清单 | L2 | tests/integration/llm/CMakeLists.txt；tests/integration/CMakeLists.txt；tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp | llm integration discoverability 入口 | integration：ctest -N 可发现 llm integration 入口，且带 integration 标签 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -N` | LLM-TODO-001 | LLM-BLK-003 | tests/integration/llm 被顶层 integration 聚合 target 发现 | llm integration CMake 接线、LLMSubsystemSmokeIntegrationTest 占位/骨架、docs/todos/llm/deliverables/LLM-TODO-003-llm-integration测试拓扑设计收敛.md | 仅当 ctest -N 能发现至少 1 个 llm integration 用例时完成 |
| LLM-TODO-004 | Done | 升级 MockLLMAdapter 为生产接口 mock | 当前 Mock 现状；详细设计 6.13；v1.0 指引 | 6.13 与 tests/mocks 协作要点 | L2 | tests/mocks/include/MockLLMAdapter.h；tests/unit/llm/MockLLMAdapterSurfaceTest.cpp | MockLLMAdapter::init()、generate()、stream_generate()、health_check() | unit：MockLLMAdapterSurfaceTest 覆盖可编程返回、计数与 health_check | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R MockLLMAdapterSurfaceTest --output-on-failure` | LLM-TODO-005、LLM-TODO-002 | LLM-BLK-004 | ILLMAdapter 公共接口先落盘 | MockLLMAdapter.h、MockLLMAdapterSurfaceTest.cpp、docs/todos/llm/deliverables/LLM-TODO-004-MockLLMAdapter生产接口mock设计收敛.md | 仅当 MockLLMAdapter 继承 ILLMAdapter 且能支撑 unary/fallback 测试时完成 |
| LLM-TODO-005 | Done | 定义 ILLMAdapter SPI 与适配配置对象 | 详细设计 6.4.2、6.5.1、7.1 | 6.5.1 ILLMAdapter | L3 | llm/include/ILLMAdapter.h；llm/include/LLMAdapterConfig.h；llm/src/adapters/AdapterCallResult.h | ILLMAdapter::init()、generate()、stream_generate()、health_check()；LLMAdapterConfig；AdapterCallResult | unit：LLMInterfaceSurfaceTest 校验纯虚接口、非异常错误传播和字段可见性 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | ILLMAdapter.h、LLMAdapterConfig.h、AdapterCallResult.h、InterfaceSurfaceTest 更新、docs/todos/llm/deliverables/LLM-TODO-005-ILLMAdapter-SPI与适配配置对象设计收敛.md | 仅当 generate() 返回 AdapterCallResult、接口不抛异常且编译通过时完成 |
| LLM-TODO-006 | Done | 定义 ILLMManager 输入输出与失败语义 | 详细设计 6.4.2、6.4.3、6.5.2、7.1 | 6.5.2 ILLMManager | L3 | llm/include/ILLMManager.h；llm/include/LLMGenerateRequest.h；llm/include/LLMManagerResult.h | ILLMManager::generate()、stream_generate()、health_check()；LLMGenerateRequest；LLMManagerResult；LLMFailureCategory | unit：LLMInterfaceSurfaceTest 校验 manager 输入输出与失败分类字段 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | ILLMManager.h、LLMGenerateRequest.h、LLMManagerResult.h、docs/todos/llm/deliverables/LLM-TODO-006-ILLMManager输入输出与失败语义设计收敛.md | 仅当 manager 结果可表达 success / failure / fallback_used 且不吞错时完成 |
| LLM-TODO-007 | Done | 定义 PromptRegistry 选择面接口 | 详细设计 6.4.2、6.5.3、6.15.5 | 6.5.3 IPromptRegistry | L3 | llm/include/prompt/IPromptRegistry.h；llm/include/prompt/PromptQuery.h；llm/include/prompt/PromptRegistryResult.h；llm/include/prompt/PromptRegistryConfig.h | IPromptRegistry::init()、select()；PromptQuery；PromptRegistryResult | unit：LLMInterfaceSurfaceTest 校验选择面输入维度与返回元数据 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | IPromptRegistry.h、PromptQuery.h、PromptRegistryResult.h、PromptRegistryConfig.h、docs/todos/llm/deliverables/LLM-TODO-007-PromptRegistry选择面接口设计收敛.md | 仅当 PromptRegistry 只承载选择职责、不介入消息装配时完成 |
| LLM-TODO-008 | Done | 定义 PromptComposer 预算输入与接口 | 详细设计 6.4.2、6.5.4 | 6.5.4 IPromptComposer | L3 | llm/include/prompt/IPromptComposer.h；llm/include/prompt/ModelBudgetHint.h；llm/include/prompt/PromptComposerConfig.h | IPromptComposer::compose()；ModelBudgetHint | unit：LLMInterfaceSurfaceTest 校验 compose 签名与 budget hint 字段 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | IPromptComposer.h、ModelBudgetHint.h、PromptComposerConfig.h | 仅当 compose() 明确依赖 budget hint 且不回写 memory 时完成 |
| LLM-TODO-009 | Done | 定义 PromptPolicy 治理接口与决策对象 | 详细设计 6.4.3、6.5.5、6.15.3 | 6.5.5 IPromptPolicy | L3 | llm/include/prompt/IPromptPolicy.h；llm/include/prompt/PromptPolicyDecision.h；llm/include/prompt/PromptPolicyInput.h；llm/include/prompt/PromptPolicyConfig.h | IPromptPolicy::evaluate()；PromptPolicyDecision；PromptPolicyInput | unit：LLMInterfaceSurfaceTest 校验 Allow / Deny / OverBudget / RequireRecompose 决策面 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | IPromptPolicy.h、PromptPolicyDecision.h、PromptPolicyInput.h、PromptPolicyConfig.h | 仅当 policy 输入输出完整且保持 fail-closed 语义时完成 |
| LLM-TODO-010 | Done | 定义 PromptPipeline facade 接口与返回类型 | 详细设计 6.5.6、7.1 | 6.5.6 IPromptPipeline | L3 | llm/include/prompt/IPromptPipeline.h；llm/include/prompt/PromptPipelineConfig.h；llm/include/prompt/PromptPipelineResult.h | IPromptPipeline::run()；PromptPipelineConfig；PromptPipelineResult | unit：LLMInterfaceSurfaceTest 校验 facade 只编排三段、不含模型调用输入 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-007、LLM-TODO-008、LLM-TODO-009 | 无 | 无 | IPromptPipeline.h、PromptPipelineConfig.h、PromptPipelineResult.h | 仅当 pipeline 接口固定表达 select -> compose -> evaluate 且不引入模型调用时完成 |
| LLM-TODO-011 | Done | 定义 route、provider、stream、usage supporting types | 详细设计 6.4.2、6.4.3、6.15.1、6.15.2、6.15.7、6.15.8 | 6.4.2 supporting types；6.4.3 对象示意 | L3 | llm/include/provider/ProviderDescriptor.h；llm/include/provider/ModelCatalogEntry.h；llm/include/route/ResolvedModelRoute.h；llm/include/route/ModelSelectionHint.h；llm/include/stream/StreamSessionRef.h；llm/include/TokenEstimate.h；llm/include/NormalizedUsageRecord.h | ProviderDescriptor；ModelCatalogEntry；ResolvedModelRoute；ModelSelectionHint；StreamSessionRef；TokenEstimate；NormalizedUsageRecord | unit：LLMInterfaceSurfaceTest 校验 supporting type 可编译且保持 module-local | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` | LLM-TODO-001、LLM-TODO-002 | 无 | 无 | route/provider/stream/usage supporting type 头文件 | 仅当 supporting types 落盘且未反向推进 shared contracts 时完成 |
| LLM-TODO-012 | Done | 实现 LLMSubsystemConfig 配置投影 | 详细设计 6.10、8.2；蓝图 8.6 | 6.10 配置项与默认策略 | L2 | llm/include/LLMSubsystemConfig.h；llm/src/LLMSubsystemConfig.cpp | RuntimePolicySnapshot / profile 投影到 llm 消费视图 | unit：LLMSubsystemConfigProjectionTest 覆盖 route、allowlist、timeout、active scene/persona 默认值 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R LLMSubsystemConfigProjectionTest --output-on-failure` | LLM-TODO-006、LLM-TODO-009、LLM-TODO-011 | 无 | 无 | LLMSubsystemConfig.h、LLMSubsystemConfig.cpp、ProjectionTest | 仅当 llm 只消费投影视图而不直接持有全量 RuntimePolicySnapshot，且 Provider 实例注入细节继续留给独立任务时完成 |
| LLM-TODO-013 | Done | 实现 PromptAssetRepository 与 baseline Prompt 资产 | 详细设计 6.6.1、6.6.2、6.15.5；v1.0 指引 | 6.6.1 Prompt 资产包形态；6.15.5 PromptAssetRepository 结论 | L2 | llm/src/prompt/PromptAssetRepository.h；llm/src/prompt/PromptAssetRepository.cpp；llm/assets/prompts/planner/default/manifest.yaml；llm/assets/prompts/planner/default/system.md；llm/assets/prompts/planner/default/task.md | PromptAssetRepository；baseline Prompt package | unit：PromptAssetPackageParseTest、PromptSourceOverlayTest 覆盖 manifest 解析、source overlay、坏包回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "PromptAsset(PackageParse|SourceOverlay)Test" --output-on-failure` | LLM-TODO-007、LLM-TODO-002 | 无 | 无 | PromptAssetRepository.*、baseline Prompt 资产样例 | 仅当 baseline / deployment / snapshot 顺序可验证且坏包保留上一 valid catalog 时完成 |
| LLM-TODO-014 | Done | 实现 Provider Catalog 加载器与 baseline Provider 资产 | 详细设计 6.6.4、6.10.1、6.15.2；v1.0 指引 | 6.15.2 Provider Catalog 加载器 | L2 | llm/src/provider/ProviderCatalogRepository.h；llm/src/provider/ProviderCatalogRepository.cpp；llm/assets/providers/catalog.yaml；llm/assets/providers/deepseek/manifest.yaml；llm/assets/providers/deepseek/models.yaml | Provider Catalog baseline、truth-source 约束、verification_state | unit：ProviderCatalogParseTest、ProviderCatalogOverlayTest、ProviderModelMetadataParseTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ProviderCatalog(Parse|Overlay)Test|ProviderModelMetadataParseTest" --output-on-failure` | LLM-TODO-011、LLM-TODO-002 | 无 | 无 | ProviderCatalogRepository.*、catalog.yaml、DeepSeek 样例资产 | 仅当 pricing/context/effective_at/verification_state 与 auth_ref/header_refs/base_url alias 的 mutable overlay 声明可测，且静态资产不保存明文 secret 时完成 |
| LLM-TODO-015 | Done | 实现 PromptRegistry 选择逻辑 | 详细设计 6.5.3、6.15.5；7.1 | 6.15.5 PromptRegistry 轻量设计收敛 | L3 | llm/src/prompt/PromptRegistry.cpp | IPromptRegistry::select()；显式 release > scene/persona > default 选择顺序 | unit：PromptRegistrySelectionTest、PromptRegistryTrustSourceTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "PromptRegistry(Selection|TrustSource)Test" --output-on-failure` | LLM-TODO-007、LLM-TODO-013 | 无 | 无 | PromptRegistry.cpp、选择规则单测 | 仅当选择顺序稳定且 trusted source 过滤 fail-closed 时完成 |
| LLM-TODO-016 | Done | 实现 TokenEstimator 预估器 | 详细设计 6.15.7；v1.0 指引 | 6.15.7 TokenEstimator | L3 | llm/src/TokenEstimator.cpp | TokenEstimate 计算、safety margin、over_budget 判定 | unit：TokenEstimatorTest 覆盖中英混合估算；PromptComposerOverBudgetTest 与 ModelRouterPolicyTest 消费 TokenEstimate | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "TokenEstimatorTest|PromptComposerOverBudgetTest|ModelRouterPolicyTest" --output-on-failure` | LLM-TODO-011、LLM-TODO-002 | 无 | 无 | TokenEstimator.cpp、估算规则单测 | 仅当 over_budget 判定和安全余量可自动化断言时完成 |
| LLM-TODO-039 | Done | 实现 TemplateRenderer 安全规则与可注入渲染接口 | 详细设计 6.6.1a；TODO 拆分建议 | 6.6.1a 模板安全规范 | L2 | llm/src/prompt/TemplateRenderer.h；llm/src/prompt/TemplateRenderer.cpp | simple_var 渲染、未匹配变量 warning、嵌套渲染拒绝、超长值截断、特殊字符转义；PromptComposer 可注入渲染接口 | unit：TemplateRendererTest 覆盖正常替换、未匹配变量 warning、嵌套渲染拒绝、超长值截断、特殊字符转义 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R TemplateRendererTest --output-on-failure` | LLM-TODO-002 | 无 | 无 | TemplateRenderer.*、TemplateRendererTest | 仅当渲染过程只执行一轮替换、不支持代码执行或外部读取，且特殊字符转义行为可断言时完成 |
| LLM-TODO-017 | Done | 实现 PromptComposer 装配流程 | 详细设计 6.5.4、6.6.1a、6.15.5；v1.0 指引 | 6.5.4 IPromptComposer；6.6.1a 模板安全规范 | L2 | llm/src/prompt/PromptComposer.cpp | PromptComposeResult、slot mapping、few-shot 注入、warning 生成 | unit：PromptComposerSlotMappingTest、PromptComposerOverBudgetTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "PromptComposer(SlotMapping|OverBudget)Test" --output-on-failure` | LLM-TODO-008、LLM-TODO-013、LLM-TODO-016、LLM-TODO-039 | 无 | 无 | PromptComposer.cpp、对应单测 | 仅当 composer 只做装配与预算告警、依赖独立 TemplateRenderer 完成渲染，且 over-budget 不自行重排上下文时完成 |
| LLM-TODO-018 | Done | 实现 PromptPolicy 治理流程 | 详细设计 6.5.5、6.15.3；v1.0 指引 | 6.15.3 PromptPolicy 组件设计收敛 | L3 | llm/src/prompt/PromptPolicy.cpp | allowlist、trusted source、tool visibility patch、redaction、render budget 检查 | unit：PromptPolicyAllowlistTest、PromptPolicyToolVisibilityTest、PromptPolicyProfileDiffTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "PromptPolicy(Allowlist|ToolVisibility|ProfileDiff)Test" --output-on-failure` | LLM-TODO-009、LLM-TODO-016、LLM-TODO-017 | 无 | 无 | PromptPolicy.cpp、治理单测 | 仅当 policy 严格按固定顺序治理且缺少 allowlist / trusted source 时默认拒绝时完成 |
| LLM-TODO-019 | Done | 实现 PromptPipeline 三段编排 | 详细设计 6.5.6、7.1 | 6.5.6 IPromptPipeline；7.1 LLM-D7/P0-1 | L2 | llm/src/prompt/PromptPipeline.cpp | PromptPipeline::run()；select -> compose -> evaluate 编排 | unit：覆盖 select 失败、OverBudget 透传、policy deny、Allow 四条路径 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit` | LLM-TODO-010、LLM-TODO-015、LLM-TODO-017、LLM-TODO-018 | 无 | 无 | PromptPipeline.cpp、三段编排单测 | 仅当 pipeline 只做三段编排、不吞错、不发起模型调用时完成 |
| LLM-TODO-020 | Done | 实现 ModelRouter 路由选择 | 详细设计 6.15.1；7.1；v1.0 指引 | 6.15.1 ModelRouter 组件设计收敛 | L2 | llm/src/route/ModelRouter.h；llm/src/route/ModelRouter.cpp | 候选集装配、硬过滤、确定性评分、fallback chain 展开 | unit：ModelRouterPolicyTest、ModelRouterFallbackTest、ModelRouterReasoningModeSelectionTest、ModelRouterStabilityTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ModelRouter(Policy|Fallback|ReasoningModeSelection|Stability)Test" --output-on-failure` | LLM-TODO-011、LLM-TODO-012、LLM-TODO-014 | 无 | 无 | ModelRouter.*、双模式与 fallback 单测 | 仅当路由选择稳定、可解释且不绕过 profile/degrade_policy 时完成 |
| LLM-TODO-021 | Done | 实现 AdapterRegistry 生命周期与健康快照 | 详细设计 6.2、6.6、6.11、6.15.6 | 6.11 并发与资源治理；6.15.6 LLMManager 轻量收敛 | L2 | llm/src/route/AdapterRegistry.h；llm/src/route/AdapterRegistry.cpp | adapter 注册、capability 标签、health snapshot、copy-on-write 更新 | unit：AdapterHealthProbeTest 覆盖健康探针与快照读取；LLMManagerFallbackTest 消费 registry 输出 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "AdapterHealthProbeTest|LLMManagerFallbackTest" --output-on-failure` | LLM-TODO-014、LLM-TODO-020 | 无 | 无 | AdapterRegistry.*、health snapshot 读写单测 | 仅当 registry 读路径不持锁做 I/O、health snapshot 可原子发布时完成 |
| LLM-TODO-040 | Done | 实现 unary 调用执行治理 | 详细设计 6.7、6.9.2、6.10、6.11、6.15.6；v1.1 P2-1 | 6.15.6 LLMManager 轻量设计收敛；6.11 并发与资源治理 | L2 | llm/src/LLMManager.cpp | timeout_policy、retry_budget、circuit_breaker_threshold、active llm calls bounded semaphore + reject | unit：LLMManagerTimeoutPolicyTest、LLMManagerRetryBudgetTest、LLMManagerConcurrencyGuardTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "LLMManager(TimeoutPolicy|RetryBudget|ConcurrencyGuard)Test" --output-on-failure` | LLM-TODO-004、LLM-TODO-012、LLM-TODO-020、LLM-TODO-021 | 无 | 无 | LLMManager.cpp 中的 call execution 治理逻辑、对应单测 | 仅当同 route 重试严格受 timeout_policy / retry_budget 约束、bounded semaphore 拒绝和熔断阈值可观测，且不通过 detach 线程或持锁 I/O 规避治理时完成 |
| LLM-TODO-022 | Done | 实现 ResponseNormalizer 语义归一化 | 详细设计 6.15.4；v1.0 指引 | 6.15.4 ResponseNormalizer 组件设计收敛 | L2 | llm/src/execution/ResponseNormalizer.h；llm/src/execution/ResponseNormalizer.cpp | DirectResponse / ToolCallIntent / ClarificationRequest / ReplanSuggestion / Refusal 归一化；reasoning_content 剥离 | unit：ResponseNormalizerSemanticMappingTest、ResponseNormalizerReasoningContentStripTest、ResponseNormalizerUsageTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ResponseNormalizer(SemanticMapping|ReasoningContentStrip|Usage)Test" --output-on-failure` | LLM-TODO-005、LLM-TODO-011、LLM-TODO-002 | 无 | 无 | ResponseNormalizer.*、语义映射单测 | 仅当 provider-private 字段被剥离且 malformed payload fail-closed 时完成 |
| LLM-TODO-023 | Done | 实现 UsageAggregator 用量与成本归并 | 详细设计 6.15.8；6.12；v1.0 指引 | 6.15.8 UsageAggregator | L3 | llm/src/UsageAggregator.cpp | prompt_tokens、completion_tokens、prompt_cache_hit_tokens、prompt_cache_miss_tokens、estimated_cost_usd 归并 | unit：ResponseNormalizerUsageTest 校验 usage 片段归并；LLMObservabilityFieldCompletenessTest 校验成本字段可观测 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "ResponseNormalizerUsageTest|LLMObservabilityFieldCompletenessTest" --output-on-failure` | LLM-TODO-014、LLM-TODO-022 | 无 | 无 | UsageAggregator.cpp、usage/cost 断言 | 仅当 usage 归并不回写静态 catalog 且 pricing 缺失时可 graceful fallback 时完成 |
| LLM-TODO-024 | Done | 实现 LLMManager unary 编排与结果装配 | 详细设计 6.7、6.15.6；7.1；v1.0 指引 | 6.15.6 LLMManager 轻量设计收敛 | L2 | llm/src/LLMManager.cpp | ILLMManager::generate()；PromptPipeline + ModelRouter + AdapterRegistry + call execution + ResponseNormalizer + UsageAggregator 串联 | unit：LLMManagerSuccessPathTest、LLMManagerFallbackTest、LLMManagerFailureMappingTest | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "LLMManager(SuccessPath|Fallback|FailureMapping)Test" --output-on-failure` | LLM-TODO-004、LLM-TODO-019、LLM-TODO-020、LLM-TODO-021、LLM-TODO-040、LLM-TODO-022、LLM-TODO-023 | 无 | 无 | LLMManager.cpp、unary 主链单测 | 仅当 manager 只负责组件编排、结果装配与失败映射，且成功 / fallback / failure mapping 三条主路径均可二值判定时完成 |
| LLM-TODO-025 | Done | 实现 OpenAICompatibleAdapter skeleton | 详细设计 6.6、6.14、7.1；v1.0 指引 | 6.14 新 Provider 接入流程；7.1 LLM-D8 | L2 | llm/src/adapters/OpenAICompatibleAdapter.h；llm/src/adapters/OpenAICompatibleAdapter.cpp；llm/include/ILLMTransport.h | OpenAI-compatible family 的 init / generate / health_check 最小骨架 | unit：AdapterProtocolMappingTest、AdapterHealthProbeTest 覆盖请求映射与健康检查 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure` | LLM-TODO-005、LLM-TODO-014、LLM-TODO-021、LLM-TODO-022 | LLM-BLK-007（真实 endpoint 联调） | 先用 ILLMTransport mock 完成协议映射与 health_check 骨架 | OpenAICompatibleAdapter.*、ILLMTransport.h、adapter 单测 | 仅当 adapter 可通过 mock transport 运行且不泄漏 provider raw payload 时完成 |
| LLM-TODO-026 | Done | 实现 OllamaAdapter skeleton | 详细设计 6.6、6.14、7.1；v1.0 指引 | 6.14 新 Provider 接入流程；7.1 LLM-D8 | L2 | llm/src/adapters/OllamaAdapter.h；llm/src/adapters/OllamaAdapter.cpp | LAN family 的 init / generate / health_check 最小骨架 | unit：AdapterProtocolMappingTest、AdapterHealthProbeTest 覆盖 LAN 协议映射与健康检查 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure` | LLM-TODO-005、LLM-TODO-014、LLM-TODO-021、LLM-TODO-022 | LLM-BLK-007（真实 endpoint 联调） | 先用 mock transport / local fixture 完成 skeleton 验证 | OllamaAdapter.*、adapter 单测 | 仅当 adapter 骨架满足 LAN family 协议边界且错误语义可判定时完成 |
| LLM-TODO-027 | Done | 实现 LocalLLMAdapter skeleton | 详细设计 6.6、6.14、7.1；v1.0 指引 | 6.14 新 Provider 接入流程；7.1 LLM-D8 | L2 | llm/src/adapters/LocalLLMAdapter.h；llm/src/adapters/LocalLLMAdapter.cpp | local runtime family 的 init / generate / health_check 最小骨架 | unit：AdapterProtocolMappingTest、AdapterHealthProbeTest 覆盖本地 runtime 映射与可达性 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure` | LLM-TODO-005、LLM-TODO-014、LLM-TODO-021、LLM-TODO-022 | LLM-BLK-007（真实 runtime 联调） | 先以 fake local runtime / mock transport 骨架验证 | LocalLLMAdapter.*、adapter 单测 | 仅当 local adapter 在 edge_minimal / factory_test 约束下仍可提供最小骨架时完成 |
| LLM-TODO-041 | Done | 实现 ProviderConfig 投影与 mutable overlay 规则 | 详细设计 6.10.1、6.14、6.15.2；LLM-C022/023 | 6.10 配置项与默认策略；6.14 新 Provider 接入流程 | L2 | llm/src/LLMSubsystemConfig.cpp；llm/src/route/AdapterRegistry.cpp | auth_ref、header_refs、base_url alias、activation flag、provider instance / snapshot version 投影到 LLMAdapterConfig | unit：ProviderConfigProjectionTest、ProviderCatalogOverlayTest 覆盖 mutable overlay、secret ref 边界与 adapter init 输入 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "Provider(ConfigProjection|CatalogOverlay)Test" --output-on-failure` | LLM-TODO-012、LLM-TODO-014、LLM-TODO-021 | LLM-BLK-007 | Provider 资产、profile override 与 adapter init 之间的最小投影链可用 | ProviderConfig 投影逻辑、ProjectionTest | 仅当既有 adapter family 的 provider instance 可从资产与投影视图推导出 adapter init 配置，且 overlay 只允许改写显式 mutable 字段时完成 |
| LLM-TODO-028 | Done | 接线 LLM observability bridges | 详细设计 6.12、9.6；v1.0 指引 | 6.12 可观测性设计；9.6 O Gate | L2 | llm/src/observability/LLMTraceBridge.h；llm/src/observability/LLMTraceBridge.cpp；llm/src/observability/LLMMetricsBridge.h；llm/src/observability/LLMMetricsBridge.cpp；llm/src/observability/LLMAuditBridge.h；llm/src/observability/LLMAuditBridge.cpp；tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp；tests/unit/llm/LLMAuditEventCoverageTest.cpp | llm 调用链日志、metrics、trace、audit 字段接线 | unit：LLMObservabilityFieldCompletenessTest、LLMAuditEventCoverageTest；integration：LLMSubsystemSmokeIntegrationTest 校验主链观测字段完整 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "LLM(ObservabilityFieldCompleteness|AuditEventCoverage)Test" --output-on-failure` | LLM-TODO-023、LLM-TODO-024、LLM-TODO-003 | 无 | 无 | observability bridge 源文件、字段完整性与审计覆盖单测 | 仅当 prompt_id / prompt_version / model_name / route / latency / error_type / token / cost / selection_reason_codes / reasoning_mode_requested / reasoning_mode_effective / prompt_cache_hit_tokens / prompt_cache_miss_tokens 字段可被统一观测，且 trusted source 失败、reasoning_content 剥离、元数据漂移三类审计事件可断言时完成 |
| LLM-TODO-029 | Done | 验证 LLM smoke integration | 详细设计 9.3、9.6；v1.0 指引 | 9.3 LLMSubsystemSmokeIntegrationTest | L2 | tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp | PromptPipeline + LLMManager + MockAdapter 最小闭环 | integration：LLMSubsystemSmokeIntegrationTest 通过，且 ctest -N 持续可发现 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMSubsystemSmokeIntegrationTest --output-on-failure` | LLM-TODO-003、LLM-TODO-004、LLM-TODO-019、LLM-TODO-024、LLM-TODO-028 | 无 | 无 | SmokeIntegrationTest、最小 integration fixture | 仅当 Prompt 三段治理和 unary 调用主路径一次性打通时完成 |
| LLM-TODO-030 | Done | 验证 DeepSeek 双模式 integration | 详细设计 6.10.4、6.15.1、9.3；v1.0 指引 | 9.3 DeepSeekDualModeSelectionIntegrationTest | L2 | tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp | deepseek-chat / deepseek-reasoner 双模式选择与 reason code 审计 | integration：DeepSeekDualModeSelectionIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R DeepSeekDualModeSelectionIntegrationTest --output-on-failure` | LLM-TODO-014、LLM-TODO-020、LLM-TODO-029 | 无 | 无 | 双模式 integration 用例与观测断言 | 仅当 complexity / SLA / budget / requires_reasoning 触发的升降档可自动验证时完成 |
| LLM-TODO-031 | Done | 验证 fallback integration | 详细设计 6.7.2、6.9、9.3；v1.0 指引 | 9.3 LLMFallbackIntegrationTest | L2 | tests/integration/llm/LLMFallbackIntegrationTest.cpp | primary 失败、fallback 成功、fallback exhausted 三类结果 | integration：LLMFallbackIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMFallbackIntegrationTest --output-on-failure` | LLM-TODO-024、LLM-TODO-025、LLM-TODO-026、LLM-TODO-027、LLM-TODO-029 | 无 | 无 | fallback integration 用例与 attempted routes 断言 | 仅当跨 route 失败收敛完整且 fallback_used / attempted routes 可观测时完成 |
| LLM-TODO-032 | Done | 验证 Prompt source switch integration | 详细设计 6.6.2、6.10.1、9.3；v1.0 指引 | 9.3 LLMPromptSourceSwitchIntegrationTest | L2 | tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp | baseline / deployment override / trusted snapshot 切换与回退 | integration：LLMPromptSourceSwitchIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMPromptSourceSwitchIntegrationTest --output-on-failure` | LLM-TODO-013、LLM-TODO-015、LLM-TODO-029 | 无 | 无 | Prompt source switch integration 用例 | 仅当坏 snapshot 不进入生产路由且可自动回退到上一 valid catalog 时完成 |
| LLM-TODO-033 | Done | 验证 persona 选择 integration | 详细设计 6.6.3、9.3；v1.0 指引 | 9.3 LLMPersonaSelectionIntegrationTest | L2 | tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp | scene_id / persona_id 选择与审计锚点 | integration：LLMPersonaSelectionIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMPersonaSelectionIntegrationTest --output-on-failure` | LLM-TODO-013、LLM-TODO-015、LLM-TODO-029 | 无 | 无 | Persona selection integration 用例 | 仅当角色/场景切换只影响 release 选择，不改变 Prompt 三段实现边界时完成 |
| LLM-TODO-034 | Done | 验证治理失败 integration | 详细设计 6.5.6、6.9、9.3；v1.0 指引 | 9.3 LLMGovernanceFailureIntegrationTest | L2 | tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp | allowlist deny、trusted source reject、over-budget 回流三条治理失败路径 | integration：LLMGovernanceFailureIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMGovernanceFailureIntegrationTest --output-on-failure` | LLM-TODO-018、LLM-TODO-019、LLM-TODO-029 | 无 | 无 | Governance failure integration 用例 | 仅当治理失败时 adapter 不被调用且失败语义完整时完成 |
| LLM-TODO-035 | Done | 验证 profile 差异 integration | 详细设计 6.10、9.3、9.5；v1.0 指引 | 9.3 LLMProfileIntegrationTest；9.5 兼容性检查点 | L2 | tests/integration/llm/LLMProfileIntegrationTest.cpp | desktop_full / cloud_full / edge_balanced / edge_minimal 的 route、allowlist、timeout 差异 | integration：LLMProfileIntegrationTest 通过；contract：现有 llm/prompt contracts 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests dasall_contract_tests && ctest --test-dir build-ci -R LLMProfileIntegrationTest --output-on-failure && ctest --test-dir build-ci --output-on-failure -L contract` | LLM-TODO-012、LLM-TODO-020、LLM-TODO-029 | 无 | 无 | Profile integration 用例、contract 不回退记录 | 仅当 llm profile 差异仅来自投影视图而非平行配置系统时完成 |
| LLM-TODO-042 | Todo | 验证 asset-only Provider instance onboarding integration | 详细设计 6.10.1、6.14、9.3；LLM-C023 | 6.14 新 Provider 接入流程；9.3 integration 路径 | L2 | tests/integration/llm/LLMProviderAssetOnboardingIntegrationTest.cpp | 对既有 adapter family，仅新增 provider package、auth_ref、profile route 即可使 provider instance 被选中，无需新增 adapter 代码 | integration：LLMProviderAssetOnboardingIntegrationTest 通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R LLMProviderAssetOnboardingIntegrationTest --output-on-failure` | LLM-TODO-014、LLM-TODO-020、LLM-TODO-025、LLM-TODO-029、LLM-TODO-041 | LLM-BLK-007 | ProviderConfig 投影与既有 family adapter skeleton 已可复用 | asset-only onboarding integration 用例 | 仅当既有 family 下的 provider instance 能通过“只加资产 + profile route”路径被发现并启用，且 profile 未声明时不会被隐式激活时完成 |
| LLM-TODO-036 | Blocked | 补齐 streaming 生命周期设计并后置实现 | 详细设计 7.2、10.1、12.1；v1.0 指引 | 7.2 shared StreamHandle 缺失；10.1 streaming 风险 | L0 | 无新增 shared 头文件；保持 llm/include/stream/StreamSessionRef.h 为 module-local；待补 llm/src/stream/StreamSessionRegistry.cpp | StreamSessionRef、stream_generate 生命周期、bounded session / cancel / cleanup | unit：StreamSessionLifecycleTest（解阻后执行） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R StreamSessionLifecycleTest --output-on-failure` | LLM-TODO-011、LLM-TODO-024 | LLM-BLK-005 | 冻结 cancel / ownership / backpressure / shared handle 语义并补设计评审 | streaming 设计补丁、StreamSessionRegistry 方案、生命周期单测 | 仅当 streaming 仍不影响 unary 验收且生命周期语义被冻结后才可解锁 |
| LLM-TODO-037 | Blocked | 评审 shared supporting object admission | 详细设计 7.2、10.1、12.1 | 7.2 暂不可直接映射项；10.1 breaking risk | L0 | 无新增 shared 头文件；继续保持 llm/include/route/ResolvedModelRoute.h、llm/include/prompt/PromptPolicyDecision.h、llm/include/stream/StreamSessionRef.h 为 module-local | shared ModelRoute、shared PromptPolicyDecision、shared StreamHandle admission 候选 | contract：现有 LLMRequest / LLMResponse / PromptComposeRequest / PromptComposeResult 契约测试不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract` | LLM-TODO-005、LLM-TODO-006、LLM-TODO-007、LLM-TODO-008、LLM-TODO-009、LLM-TODO-010、LLM-TODO-011、LLM-TODO-029、LLM-TODO-031、LLM-TODO-035 | LLM-BLK-006 | 形成跨模块消费者矩阵、迁移窗口、兼容 contract tests 和 Go/No-Go 评审结论 | admission 评审纪要、兼容矩阵、风险清单 | 仅当评审给出清晰 Go/No-Go 且 contract gate 不回退时完成 |
| LLM-TODO-038 | Todo | 回写 llm 专项 Gate 与交付证据 | 计划文档；编码规范；本专项 TODO | 9.6 Gate 建议清单；11 风险阻塞 | L2 | docs/todos/llm/DASALL_llm子系统专项TODO.md；docs/worklog/DASALL_开发执行记录.md | build / unit / contract / integration / risk / blocker 证据回写 | process：显式记录命令、结果、阻塞变化、回退策略与后继任务 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_llm dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract && ctest --test-dir build-ci --output-on-failure -L integration` | LLM-TODO-029、LLM-TODO-030、LLM-TODO-031、LLM-TODO-032、LLM-TODO-033、LLM-TODO-034、LLM-TODO-035、LLM-TODO-042 | LLM-BLK-008（工具状态异常时只影响验证手段） | 若 CMake Tools 预设异常，则回退显式 cmake/ctest 命令并在证据中注明 | Gate 回写条目、worklog 证据、残余风险清单 | 仅当每个 Gate 都有可追溯证据且残余风险被显式记录时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 构建与测试骨架 | LLM-TODO-001、002、003 | 串行起步 | 先解 include / unit / integration discoverability，再进入实现 |
| B 公共接口冻结 | LLM-TODO-005、006、007、008、009、010、011 | 005、006、007、008、009、011 可并行；010 依赖 007~009 | 先把 module-local 公共面冻住，避免后续实现漂移 |
| C 测试夹具升级 | LLM-TODO-004 | 串行插入 | MockLLMAdapter 依赖 005，且是 unary / integration 前置 |
| D 配置与资产 | LLM-TODO-012、013、014 | 012 与 013/014 可并行 | 先冻结 llm 消费的配置投影视图和 baseline 资产根 |
| E Prompt 治理链 | LLM-TODO-015、016、039、017、018、019 | 015 与 016 并行；039 可与 015/016 并行；017 依赖 013/016/039；018 依赖 017；019 收口 | 按 Registry -> TokenEstimator -> TemplateRenderer -> Composer -> Policy -> Pipeline 顺序推进 |
| F 路由与调用核心 | LLM-TODO-020、021、040、022、023、024 | 020 与 022 可并行；021 依赖 020/014；040 依赖 020/021/012；023 依赖 014/022；024 最后收口 | 先 route/registry，再补 call execution、normalize、usage 和 manager 编排 |
| G Provider 注入、skeleton 与观测 | LLM-TODO-041、025、026、027、028 | 041 与 025/026/027 可并行；028 依赖 024；asset-only onboarding 留到集成阶段验证 | 保持 provider projection 与 adapter family 分任务推进，避免把实例接入和协议骨架混成一项任务 |
| H 集成验证 | LLM-TODO-029、030、031、032、033、034、035、042 | 029 先行；030~035、042 在 smoke 打通后可并行 | 先 smoke，再做 dual-mode、fallback、source switch、persona、governance、profile 与 asset-only onboarding |
| I 证据收口 | LLM-TODO-038 | 串行 | Gate、阻塞状态和风险回退统一回写 |
| J 后置与评审 | LLM-TODO-036、037 | 串行后置 | streaming 与 shared admission 均需在 unary 稳定后评审 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 未通过后动作 |
|---|---|---|---|---|
| LLM-GATE-01 | include/CMake 接线门 | 阶段 A 结束 | llm/include 稳定存在，dasall_llm 可构建，unit/integration 拓扑已被发现 | 退回 LLM-TODO-001、002、003 |
| LLM-GATE-02 | 公共接口冻结门 | 阶段 B 结束 | ILLMAdapter、ILLMManager、IPrompt* 与 supporting types 全部落 module-local 头文件且 LLMInterfaceSurfaceTest 通过 | 退回 LLM-TODO-005~011 |
| LLM-GATE-03 | 资产基线门 | 阶段 D 结束 | Prompt / Provider baseline 资产可解析，overlay 与坏包回退测试通过 | 退回 LLM-TODO-013、014 |
| LLM-GATE-04 | Prompt 治理门 | 阶段 E 结束 | Registry、TemplateRenderer、Composer、Policy、Pipeline 全部具备 unit test，且 over-budget/deny 路径可自动断言 | 退回 LLM-TODO-015、016、039、017、018、019 |
| LLM-GATE-05 | unary 主链门 | 阶段 F 结束 | ModelRouter、AdapterRegistry、call execution、ResponseNormalizer、UsageAggregator、LLMManager 单测通过 | 退回 LLM-TODO-020、021、040、022、023、024 |
| LLM-GATE-06 | adapter/observability 门 | 阶段 G 结束 | 至少一个 adapter skeleton 可注入 manager；ProviderConfigProjectionTest 与 LLMObservabilityFieldCompletenessTest / LLMAuditEventCoverageTest 通过 | 退回 LLM-TODO-041、025、026、027、028 |
| LLM-GATE-07 | integration discoverability 门 | 阶段 H 前 | ctest -N 可发现 llm integration 用例，且 smoke 用例先通过 | 退回 LLM-TODO-003、029 |
| LLM-GATE-08 | contracts 不回退门 | 阶段 H / I 前 | 现有 llm/prompt contract 测试不回退；未新增未评审 shared object | 退回 LLM-TODO-035、037 |
| LLM-GATE-09 | profile 对齐门 | 阶段 H 结束 | LLMProfileIntegrationTest 与 LLMProviderAssetOnboardingIntegrationTest 通过，且 llm 未出现平行配置体系 | 退回 LLM-TODO-012、014、035、041、042 |
| LLM-GATE-10 | streaming / shared admission 评审门 | 阶段 J 前 | Go/No-Go 评审完成，解阻条件明确，contract gate 不回退 | 未评审不得推进 LLM-TODO-036、037 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| LLM-BLK-001 | llm/include 为空，模块公共面不存在 | LLM-TODO-001、005~028、039~041 | include 根与公共头骨架落盘 | 完成 LLM-TODO-001 | 在解阻前保持 llm 为 placeholder-only，不宣称核心实现阶段已启动 |
| LLM-BLK-002 | tests/unit/llm 仅占位注释，unit discoverability 缺失 | LLM-TODO-002、005~028、039~041 | unit CMake 接线和最小 surface test 可被 ctest -N 发现 | 完成 LLM-TODO-002 | 若暂时无法 discover，仅允许 compile-only 自检，状态保持 In Progress |
| LLM-BLK-003 | tests/integration/llm 不存在，integration discoverability 缺失 | LLM-TODO-003、029~035、042 | integration CMake 接线和 smoke 测试入口可被发现 | 完成 LLM-TODO-003 | 不允许在未 discover 前宣称 llm 进入核心链路可交付 |
| LLM-BLK-004 | MockLLMAdapter 仍是 string 脚手架 | LLM-TODO-004、024、029~034 | 生产接口 mock 支持 init/generate/stream_generate/health_check | 完成 LLM-TODO-004 | 保持 unary / integration 任务阻塞，不用 string mock 伪装主链完成 |
| LLM-BLK-005 | StreamHandle / streaming 生命周期未冻结 | LLM-TODO-036 | 取消、ownership、bounded session、backpressure 语义补设计完成 | 补充 streaming 设计与评审门 | 继续保持 stream_generate 占位，不把 streaming 作为首轮验收门 |
| LLM-BLK-006 | shared ModelRoute / PromptPolicyDecision / StreamHandle admission 未冻结 | LLM-TODO-037 | 形成消费者矩阵、迁移窗口与 contract 验证基线 | 先收集 module-local 真实消费者与兼容性风险 | 继续保持 module-local supporting types，不发起 shared contracts 变更 |
| LLM-BLK-007 | secret / endpoint / profile 到 llm 的真实注入链未打通 | LLM-TODO-041、042；并限制 LLM-TODO-025、026、027 只能做到 skeleton | LLMSubsystemConfig、Provider Catalog overlay 与 infra/config/secret 的最小投影可用 | 先完成 LLM-TODO-041，再用 LLM-TODO-042 验证 asset-only onboarding | 在真实注入未打通前，不宣称 Cloud / LAN / Local 真调用就绪 |
| LLM-BLK-008 | CMake Tools 预设/目标发现可能失效 | LLM-TODO-038 及所有验证动作 | 显式 cmake/ctest 命令跑通并记录 | 回退使用 `cmake -S . -B build-ci -G "Unix Makefiles"` 与 `ctest --test-dir build-ci ...` | 若 IDE 目标空白，只影响验证手段，不改变代码门禁结论 |

## 9. 验收与质量门

### 9.1 验收命令基线

1. 配置基线：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
2. 模块构建基线：
   - `cmake --build build-ci --target dasall_llm`
3. 测试发现性基线：
   - `ctest --test-dir build-ci -N`
4. 单测基线：
   - `ctest --test-dir build-ci --output-on-failure -L unit`
5. 契约基线：
   - `ctest --test-dir build-ci --output-on-failure -L contract`
6. 集成基线：
   - `ctest --test-dir build-ci --output-on-failure -L integration`

说明：

1. build-ci 当前是仓库内已存在的统一验证目录，适合作为专项 TODO 的标准验收入口。
2. 若 VS Code CMake Tools 出现“未选择预设”或空 targets/tests，验证仍以显式 cmake/ctest 命令为准，不降低 Gate 标准。

### 9.2 质量门逐项回答

| 质量门 | 判定标准 | 绑定任务 |
|---|---|---|
| 架构一致性 | llm 不直接拥有 ContextPacket 组装、恢复裁定或工具权限 | LLM-TODO-008、009、019、024、037 |
| ADR 边界一致性 | ADR-006/007/008 均不被改写；PromptPipeline 不演化为第二主控 | LLM-TODO-009、010、019、024 |
| contracts 兼容性 | LLMRequest/LLMResponse/PromptSpec/PromptRelease/PromptComposeRequest/PromptComposeResult 契约测试不回退 | LLM-TODO-035、037 |
| 工程可实现性 | llm/include、llm/src、llm/assets、tests/llm、CMake 接线全部落到实际目录，且 provider config projection 有显式落点 | LLM-TODO-001~003、013、014、041 |
| 调用治理完整性 | timeout、retry budget、并发拒绝与熔断阈值均有显式任务与自动化断言 | LLM-TODO-040 |
| Provider 实例接入闭环 | auth_ref/header_refs/base_url alias 投影可测，既有 family 支持 asset-only onboarding | LLM-TODO-041、042 |
| 测试可验证性 | ctest -N 可发现 llm unit/integration；至少 1 个 smoke integration 与 1 个 asset-only onboarding integration 通过 | LLM-TODO-002、003、029、042 |
| 观测与审计完整性 | selection_reason_codes、reasoning mode、prompt cache、cost 字段完整，且关键审计事件可断言 | LLM-TODO-028、030 |
| 原子任务可执行性 | 每项任务只有一个主目标，且三件套齐全 | 本表全部任务 |
| 粒度可评审性 | L3 任务直接到接口/对象；L2 任务限定为单组件或单测试组；L0 任务显式 Blocked | 4.3 粒度表与 6.1 任务表 |

## 10. 风险与回退策略

### 10.1 主要风险表

| 风险 | 描述 | 等级 | 缓解措施 | 回退策略 |
|---|---|---|---|---|
| Prompt 与 Context 边界漂移 | PromptComposer 或 PromptPolicy 反向获得上下文检索/裁剪权 | High | 所有相关任务强制回链 ADR-006；over-budget 只回流 Runtime | 退回到 PromptPipeline 返回 OverBudget，不在 llm 内二次语义裁剪 |
| shared contracts 过早冻结 | 为图省事把 ResolvedModelRoute / PromptPolicyDecision / StreamHandle 推入 contracts | High | shared admission 单列 Blocked 评审任务 | 继续保持 module-local，不推进 shared 头文件 |
| provider 细节外泄 | raw payload、reasoning_content、vendor 参数穿透到上游接口 | High | ResponseNormalizer 强制剥离 provider-private 字段 | 对外只保留 shared 语义与 observability 必需事实 |
| 资产刷新看到半成品快照 | Prompt / Provider catalog 刷新过程中读者看到不一致版本 | Medium | immutable snapshot + atomic swap；解析失败保留上一 valid snapshot | 刷新失败时拒绝切换，沿用上一份 valid catalog |
| fallback 无证据 | 路由切换发生但缺少 logs / metrics / trace / audit | Medium | observability 任务前置，强制埋点 route/fallback 字段 | 未补齐字段前，不允许宣称核心链路完成 |
| 真 Provider 联调阻塞 | secret / endpoint / profile 注入链未通，adapter 迟迟无法真实联调 | Medium | skeleton 先基于 mock transport / fake endpoint 验证 | 先完成 local/mock 路径，不等待真实云端联调 |
| streaming 过早推进 | shared handle、取消、背压未定就引入 streaming 主验收 | Medium | streaming 单独 Blocked，不绑在 unary Gate 上 | 保留 stream_generate 占位，不作为首轮 Gate 完成条件 |
| 工具态验证漂移 | CMake Tools 不稳定导致“看似没有目标/测试” | Low | 统一使用显式 cmake/ctest 命令复核 | 在交付证据中注明工具态异常，不降低测试标准 |

### 10.2 回退策略清单

1. 若 PromptPolicyDecision 语义在实现期摇摆，先回退到最小四态：Allow、Deny、OverBudget、RequireRecompose，不扩写 shared 语义。
2. 若 route 维度在实现期超出当前 contracts 能力，继续以 ResolvedModelRoute module-local 表达，不推动 shared ModelRoute。
3. 若 Cloud / LAN 真联调阻塞，先用 Local / Mock adapter 完成 unary 主链与 integration smoke。
4. 若 Provider Catalog overlay 逻辑过于复杂，先冻结 baseline + deployment 两层，trusted snapshot 仅作为受控扩展点，不影响主链验收。
5. 若 observability sink 接线晚于主链实现，先要求字段在内存内结构可断言，再补 infra bridge，不允许 silent success。

## 11. 可行性结论

### 11.1 是否可直接进入执行

可以，但必须按本专项 TODO 的顺序进入执行，而不是直接跳到 adapter 或 LLMManager 实现。

当前建议的起步顺序是：

1. 先完成 LLM-TODO-001、002、003，解 include 与测试 discoverability。
2. 再完成 LLM-TODO-005~011，冻结 llm module-local 公共面。
3. 随后完成 LLM-TODO-004、012、013、014、015、016、039、017、018、019，先打通配置投影、资产基线与 Prompt 三段治理。
4. 最后进入 LLM-TODO-020、021、040、022、023、024、041、025、026、027、028、029~035、042，完成 unary 主链、provider 注入、adapter skeleton、observability 和 integration gates。

### 11.2 当前可落到的最细粒度

1. L3：Adapter / Manager / Prompt 三段治理接口、PromptQuery / PromptPolicyDecision / ResolvedModelRoute / TokenEstimate / NormalizedUsageRecord 等 supporting types、TokenEstimator、UsageAggregator。
2. L2：PromptAssetRepository、Provider Catalog 加载器、TemplateRenderer、PromptComposer、PromptPipeline、ModelRouter、AdapterRegistry、call execution 治理、ResponseNormalizer、LLMManager、ProviderConfig 投影、adapter skeleton、observability bridges、integration fixture 与测试组。
3. L0：streaming 生命周期、shared supporting object admission。

### 11.3 阻止进一步细化的设计缺口

当前阻止接口/函数级继续细化的缺口主要只有两类：

1. streaming：缺少共享 handle、取消、背压、ownership 的冻结结论，因此只能保持 Blocked。
2. shared admission：缺少 shared ModelRoute / PromptPolicyDecision / StreamHandle 的跨模块消费者矩阵、兼容窗口和 contract test 基线，因此不能默认推进 shared contracts。

TemplateRenderer 安全规则、调用执行治理、Provider 注入闭环与 asset-only onboarding 已在本修正版中转为显式 Build 任务；除此之外，LLM 子系统的核心 Build 面已经具备足够的接口、对象、流程、错误语义、目录、测试与门禁证据，可直接进入执行。

## 12. 阶段 A 执行记录

### 12.1 LLM-TODO-001

1. 状态：Done；LLM-BLK-001 已解阻。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-001-llm公共include布局设计收敛.md](deliverables/LLM-TODO-001-llm%E5%85%AC%E5%85%B1include%E5%B8%83%E5%B1%80%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[llm/include/.gitkeep](../../../llm/include/.gitkeep)、[llm/include/prompt/.gitkeep](../../../llm/include/prompt/.gitkeep)、[llm/include/provider/.gitkeep](../../../llm/include/provider/.gitkeep)、[llm/include/route/.gitkeep](../../../llm/include/route/.gitkeep)、[llm/include/stream/.gitkeep](../../../llm/include/stream/.gitkeep)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 成功，输出为 `ninja: no work to do.`，说明 include 根与显式存在性校验未破坏现有 placeholder-only 构建。
5. 后继任务：LLM-TODO-002 已具备执行条件，可进入 llm unit 测试拓扑注册。

### 12.2 LLM-TODO-002

1. 状态：Done；LLM-BLK-002 已解阻。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-002-llm-unit测试拓扑注册设计收敛.md](deliverables/LLM-TODO-002-llm-unit%E6%B5%8B%E8%AF%95%E6%8B%93%E6%89%91%E6%B3%A8%E5%86%8C%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签执行中显示 `LLMInterfaceSurfaceTest` 通过；`ListTests_CMakeTools` 已列出 `LLMInterfaceSurfaceTest`；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。
5. 后继任务：LLM-TODO-003 已具备执行条件，可进入 llm integration 测试拓扑注册。

### 12.3 LLM-TODO-003

1. 状态：Done；LLM-BLK-003 已解阻。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-003-llm-integration测试拓扑设计收敛.md](deliverables/LLM-TODO-003-llm-integration%E6%B5%8B%E8%AF%95%E6%8B%93%E6%89%91%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/CMakeLists.txt](../../../tests/integration/CMakeLists.txt)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)、[tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_integration_tests` 成功，并在 integration 标签执行链中显示 `LLMSubsystemSmokeIntegrationTest` 作为第 36 个 integration 用例通过；`ListTests_CMakeTools` 已列出 `LLMSubsystemSmokeIntegrationTest`；`RunCtest_CMakeTools` 定向执行 `LLMSubsystemSmokeIntegrationTest` 结果为 1/1 通过。
5. 阶段结论：阶段 A 的 LLM-TODO-001、002、003 已全部完成，LLM-GATE-01 达到通过条件。

## 13. 阶段 B 执行记录

### 13.1 LLM-TODO-005

1. 状态：Done；`ILLMAdapter` 四入口 SPI、`LLMAdapterConfig` 与 module-local `AdapterCallResult` 已冻结落盘。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-005-ILLMAdapter-SPI与适配配置对象设计收敛.md](deliverables/LLM-TODO-005-ILLMAdapter-SPI%E4%B8%8E%E9%80%82%E9%85%8D%E9%85%8D%E7%BD%AE%E5%AF%B9%E8%B1%A1%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/include/ILLMAdapter.h](../../../llm/include/ILLMAdapter.h)、[llm/include/LLMAdapterConfig.h](../../../llm/include/LLMAdapterConfig.h)、[llm/src/adapters/AdapterCallResult.h](../../../llm/src/adapters/AdapterCallResult.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。附带的 `DartConfiguration.tcl` 缺失提示未影响测试返回码，记为 CTest 工具噪声而非 blocker。
5. 边界结论：`ILLMAdapter.h` 只前向声明 `HealthStatus`、`StreamSessionRef`、`IStreamObserver` 与 `AdapterCallResult`，因此 005 只冻结 adapter SPI，不提前推进 011、012 或 streaming 生命周期任务。
6. 后继任务：LLM-TODO-006 已具备执行条件，可继续冻结 `ILLMManager` 输入输出与失败语义；LLM-TODO-004 也已失去接口缺失这一前置阻塞。

### 13.2 LLM-TODO-006

1. 状态：Done；`ILLMManager` 统一入口 SPI、`LLMGenerateRequest` 运行时 handoff 对象，以及 `LLMManagerResult` 的 success/failure/fallback 语义已冻结落盘。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-006-ILLMManager输入输出与失败语义设计收敛.md](deliverables/LLM-TODO-006-ILLMManager%E8%BE%93%E5%85%A5%E8%BE%93%E5%87%BA%E4%B8%8E%E5%A4%B1%E8%B4%A5%E8%AF%AD%E4%B9%89%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/include/ILLMManager.h](../../../llm/include/ILLMManager.h)、[llm/include/LLMGenerateRequest.h](../../../llm/include/LLMGenerateRequest.h)、[llm/include/LLMManagerResult.h](../../../llm/include/LLMManagerResult.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。附带的 `DartConfiguration.tcl` 缺失提示未影响测试返回码，记为 CTest 工具噪声而非 blocker。
5. 边界结论：`LLMGenerateRequest` 通过 opaque `selection_hint` 冻结了对 `ModelSelectionHint` 的依赖方向，但没有提前定义 route supporting types；`LLMManagerResult` 则把 `attempted_routes`、`failure_category` 与 `fallback_used` 统一收进 manager 失败事实，保持恢复裁定权仍归 Runtime。
6. 后继任务：LLM-TODO-007 已具备执行条件，可继续冻结 PromptRegistry 选择面接口；LLM-TODO-012 仍需等待 009/011 完成后再进入配置投影实现。

### 13.3 LLM-TODO-007

1. 状态：Done；`IPromptRegistry` 选择 SPI、`PromptQuery` 输入维度、`PromptRegistryConfig` 初始化配置，以及 `PromptRegistryResult` 审计元数据边界已冻结落盘。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-007-PromptRegistry选择面接口设计收敛.md](deliverables/LLM-TODO-007-PromptRegistry选择面接口设计收敛.md)。
3. 代码交付：[llm/include/prompt/IPromptRegistry.h](../../../llm/include/prompt/IPromptRegistry.h)、[llm/include/prompt/PromptQuery.h](../../../llm/include/prompt/PromptQuery.h)、[llm/include/prompt/PromptRegistryConfig.h](../../../llm/include/prompt/PromptRegistryConfig.h)、[llm/include/prompt/PromptRegistryResult.h](../../../llm/include/prompt/PromptRegistryResult.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。本轮未观察到由 007 引入的新编译告警；附带的 `DartConfiguration.tcl` 缺失提示仍记为 CTest 工具噪声而非 blocker。
5. 边界结论：`PromptRegistryResult` 把 success/failure 判定收敛为“`release` 与 optional `code` 互斥”的 module-local 结果对象，并要求 `selected_prompt_id` / `selected_version` 与共享 `PromptRelease` 保持一致，从而把选择审计事实固定在 Registry 边界内，而不提前扩张到 Composer / Policy / Pipeline。
6. 后继任务：LLM-TODO-008 已具备执行条件，可继续冻结 PromptComposer 的预算输入与接口；LLM-TODO-015 仍需等待 013 完成后再进入 Registry 选择逻辑实现。

### 13.4 LLM-TODO-008

1. 状态：Done；`IPromptComposer` 装配 SPI、`ModelBudgetHint` 模型预算提示，以及 `PromptComposerConfig` 初始化配置已冻结落盘。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-008-PromptComposer预算输入与接口设计收敛.md](deliverables/LLM-TODO-008-PromptComposer预算输入与接口设计收敛.md)。
3. 代码交付：[llm/include/prompt/IPromptComposer.h](../../../llm/include/prompt/IPromptComposer.h)、[llm/include/prompt/ModelBudgetHint.h](../../../llm/include/prompt/ModelBudgetHint.h)、[llm/include/prompt/PromptComposerConfig.h](../../../llm/include/prompt/PromptComposerConfig.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。附带的 `DartConfiguration.tcl` 缺失提示未影响测试返回码，记为 CTest 工具噪声而非 blocker。
5. 边界结论：`compose()` 显式依赖共享 `PromptComposeRequest` / `PromptRelease` 与 module-local `ModelBudgetHint`，从接口层把预算预估能力固定在 llm 内部，同时保持 PromptComposer 不拥有 policy 裁定权、不回写 memory，也不复制 shared prompt contracts。
6. 后继任务：LLM-TODO-009 已具备执行条件，可继续冻结 PromptPolicy 治理接口与决策对象；LLM-TODO-017 仍需等待 013/016/039 完成后再进入 PromptComposer 实现。

### 13.5 LLM-TODO-009

1. 状态：Done；`IPromptPolicy` 治理 SPI、`PromptPolicyDecision` 四态决策对象，以及 `PromptPolicyInput` / `PromptPolicyConfig` fail-closed 输入配置已冻结落盘。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-009-PromptPolicy治理接口与决策对象设计收敛.md](deliverables/LLM-TODO-009-PromptPolicy治理接口与决策对象设计收敛.md)。
3. 代码交付：[llm/include/prompt/IPromptPolicy.h](../../../llm/include/prompt/IPromptPolicy.h)、[llm/include/prompt/PromptPolicyDecision.h](../../../llm/include/prompt/PromptPolicyDecision.h)、[llm/include/prompt/PromptPolicyInput.h](../../../llm/include/prompt/PromptPolicyInput.h)、[llm/include/prompt/PromptPolicyConfig.h](../../../llm/include/prompt/PromptPolicyConfig.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：`PromptPolicyDecision` 把治理输出收敛为 `Allow` / `Deny` / `OverBudget` / `RequireRecompose` 四态，并通过 fail-closed 默认值和一致性断言确保只有 `Allow` 可输出 `governed_messages`；这使 PromptPolicy 保持“治理裁定 owner”而不演化为第二个 Context / Tool / Runtime 主控。
6. 后继任务：LLM-TODO-010 已具备执行条件，可继续冻结 PromptPipeline facade 接口与返回类型；LLM-TODO-018 仍需等待 016/017 完成后再进入治理实现。

### 13.6 LLM-TODO-010

1. 状态：Done；`IPromptPipeline` facade SPI、`PromptPipelineConfig` 三段初始化聚合对象，以及 `PromptPipelineResult` 运行返回类型已冻结落盘。
2. 设计交付：`docs/todos/llm/deliverables/LLM-TODO-010-PromptPipeline-facade接口与返回类型设计收敛.md`。
3. 代码交付：`llm/include/prompt/IPromptPipeline.h`、`llm/include/prompt/PromptPipelineConfig.h`、`llm/include/prompt/PromptPipelineResult.h`、`tests/unit/llm/InterfaceSurfaceTest.cpp`。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：`IPromptPipeline::run()` 只暴露 `PromptQuery`、`PromptComposeRequest`、`PromptPolicyInput` 三段输入，并通过 `PromptPipelineResult` 收敛 registry / compose / policy 产物，避免 Runtime 继续硬编码 llm 内部顺序，同时不把模型调用并入 facade。
6. 后继任务：LLM-TODO-011 已具备执行条件，可继续冻结 route、provider、stream、usage supporting types；LLM-TODO-019 仍需等待 010/018 完成后再进入 pipeline 实现。

### 13.7 LLM-TODO-011

1. 状态：Done；provider、route、stream、token、usage 七个 module-local supporting types 已冻结落盘。
2. 设计交付：`docs/todos/llm/deliverables/LLM-TODO-011-route-provider-stream-usage-supporting-types设计收敛.md`。
3. 代码交付：`llm/include/provider/ProviderDescriptor.h`、`llm/include/provider/ModelCatalogEntry.h`、`llm/include/route/ResolvedModelRoute.h`、`llm/include/route/ModelSelectionHint.h`、`llm/include/stream/StreamSessionRef.h`、`llm/include/TokenEstimate.h`、`llm/include/NormalizedUsageRecord.h`、`tests/unit/llm/InterfaceSurfaceTest.cpp`。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm` 与 `dasall_unit_tests` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 结果为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：supporting types 虽按 provider/route/stream 子目录落盘，但命名空间继续对齐现有 `ILLMAdapter` / `LLMGenerateRequest` 的前向声明，保持在顶层 `dasall::llm`，并继续维持 module-local，不推进 shared admission。
6. 后继任务：LLM-TODO-012 已具备执行条件，可继续实现 `LLMSubsystemConfig` 配置投影；`LLM-TODO-016`、`018`、`019` 也已获得 011 的 supporting type 依赖前置。

## 14. 阶段 C 执行记录

### 14.1 LLM-TODO-004

1. 状态：Done；LLM-BLK-004 已解阻，`MockLLMAdapter` 已升级为基于 `ILLMAdapter` 的生产接口 mock。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-004-MockLLMAdapter生产接口mock设计收敛.md](deliverables/LLM-TODO-004-MockLLMAdapter生产接口mock设计收敛.md)。
3. 代码交付：[tests/mocks/include/MockLLMAdapter.h](../../../tests/mocks/include/MockLLMAdapter.h)、[tests/unit/llm/MockLLMAdapterSurfaceTest.cpp](../../../tests/unit/llm/MockLLMAdapterSurfaceTest.cpp)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，unit 链路中 `dasall_runtime_smoke_test`、`LLMInterfaceSurfaceTest` 与 `MockLLMAdapterSurfaceTest` 均通过；`RunCtest_CMakeTools` 定向执行 `MockLLMAdapterSurfaceTest` 结果为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮只升级 tests/mocks 夹具，不扩张 llm shared/public contracts；为落地 `health_check()` 仅在 mock 头内补了最小 test-local `HealthStatus` 定义，同时保留 legacy `invoke()` 兼容层以避免打破现有 runtime smoke test。
6. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-012 仍是下一项可直接执行的原子任务；同时 004 已为 024 与 029~034 清除了 mock 夹具前置障碍。

### 14.2 LLM-TODO-012

1. 状态：Done；`LLMSubsystemConfig` 已将 `RuntimePolicySnapshot` / profile 裁剪为 llm 消费视图，并保持 llm 不直接持有全量 snapshot。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-012-LLMSubsystemConfig配置投影设计收敛.md](deliverables/LLM-TODO-012-LLMSubsystemConfig配置投影设计收敛.md)。
3. 代码交付：[llm/include/LLMSubsystemConfig.h](../../../llm/include/LLMSubsystemConfig.h)、[llm/src/LLMSubsystemConfig.cpp](../../../llm/src/LLMSubsystemConfig.cpp)、[tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp](../../../tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_llm_interface_surface_unit_test` 与 `dasall_llm_subsystem_config_projection_unit_test` 成功；`RunCtest_CMakeTools` 定向执行 `LLMInterfaceSurfaceTest` 与 `LLMSubsystemConfigProjectionTest` 结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 llm 配置消费面收敛为 route map、prompt allowlist/trusted source、degrade policy、llm timeout、prompt/provider 资产源与 selector overlay；`active_scene` / `active_persona` 默认保持空值，表示“不显式覆盖”，从而继续允许 PromptRegistry 按设计回退到 profile selector 与包内 default release，而不是在 012 上静默固化一套平行配置体系。
6. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-013 已具备执行条件，可继续实现 `PromptAssetRepository` 与 baseline Prompt 资产；与 provider instance init 相关的 `auth_ref` / `header_refs` / `base_url alias` 细节仍留给后续 041 处理。

## 15. 阶段 D 执行记录

### 15.1 LLM-TODO-013

1. 状态：Done；`PromptAssetRepository` 与 baseline Prompt 资产已落盘，013 为 015/017/032/033 提供了可消费的 Prompt 目录基线。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-013-PromptAssetRepository与baseline-Prompt资产设计收敛.md](deliverables/LLM-TODO-013-PromptAssetRepository与baseline-Prompt资产设计收敛.md)。
3. 代码交付：[llm/src/asset/KeyValueYamlParser.h](../../../llm/src/asset/KeyValueYamlParser.h)、[llm/src/prompt/PromptAssetDescriptor.h](../../../llm/src/prompt/PromptAssetDescriptor.h)、[llm/src/prompt/PromptAssetRepository.h](../../../llm/src/prompt/PromptAssetRepository.h)、[llm/src/prompt/PromptAssetRepository.cpp](../../../llm/src/prompt/PromptAssetRepository.cpp)、[llm/assets/prompts/planner/default/manifest.yaml](../../../llm/assets/prompts/planner/default/manifest.yaml)、[llm/assets/prompts/planner/default/system.md](../../../llm/assets/prompts/planner/default/system.md)、[llm/assets/prompts/planner/default/task.md](../../../llm/assets/prompts/planner/default/task.md)、[tests/unit/llm/PromptAssetPackageParseTest.cpp](../../../tests/unit/llm/PromptAssetPackageParseTest.cpp)、[tests/unit/llm/PromptSourceOverlayTest.cpp](../../../tests/unit/llm/PromptSourceOverlayTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已发现 `dasall_unit_tests`、`dasall_prompt_asset_package_parse_unit_test` 与 `dasall_prompt_source_overlay_unit_test`；`ListTests_CMakeTools` 已列出 `PromptAssetPackageParseTest` 与 `PromptSourceOverlayTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，unit 链路 `218/218` 全部通过，其中新增的 `PromptAssetPackageParseTest` 与 `PromptSourceOverlayTest` 均通过；`RunCtest_CMakeTools` 定向执行 `PromptAssetPackageParseTest` 与 `PromptSourceOverlayTest` 结果均为 1/1 通过。显式 `ctest --test-dir build-ci -N -R PromptAssetPackageParseTest` 与 `ctest --test-dir build-ci -N -R PromptSourceOverlayTest` 也确认两条用例已被 discover。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮只把 Prompt 资产装载、manifest/Markdown 解析、content hash 和 snapshot overlay 收敛进 Repository，不把 stage/task/language/model_family 的运行态选择提前揉进仓储，也不把 Prompt 文本编译进 llm 二进制，从而继续守住 ADR-006 的 Prompt owner 边界。
6. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-014 已具备执行条件，可继续实现 `ProviderCatalogRepository` 与 baseline Provider 资产；015 现在也失去“Prompt 资产根不存在”这一前置阻塞。

### 15.2 LLM-TODO-014

1. 状态：Done；`ProviderCatalogRepository` 与 baseline Provider 资产已落盘，014 为 020/021/023/030/041/042 提供了可消费的 Provider truth-source 基线。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-014-ProviderCatalogRepository与baseline-Provider资产设计收敛.md](deliverables/LLM-TODO-014-ProviderCatalogRepository与baseline-Provider资产设计收敛.md)。
3. 代码交付：[llm/src/provider/ProviderCatalogRepository.h](../../../llm/src/provider/ProviderCatalogRepository.h)、[llm/src/provider/ProviderCatalogRepository.cpp](../../../llm/src/provider/ProviderCatalogRepository.cpp)、[llm/assets/providers/catalog.yaml](../../../llm/assets/providers/catalog.yaml)、[llm/assets/providers/deepseek/manifest.yaml](../../../llm/assets/providers/deepseek/manifest.yaml)、[llm/assets/providers/deepseek/models.yaml](../../../llm/assets/providers/deepseek/models.yaml)、[tests/unit/llm/ProviderCatalogParseTest.cpp](../../../tests/unit/llm/ProviderCatalogParseTest.cpp)、[tests/unit/llm/ProviderCatalogOverlayTest.cpp](../../../tests/unit/llm/ProviderCatalogOverlayTest.cpp)、[tests/unit/llm/ProviderModelMetadataParseTest.cpp](../../../tests/unit/llm/ProviderModelMetadataParseTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListTests_CMakeTools` 已列出 `ProviderCatalogParseTest`、`ProviderCatalogOverlayTest` 与 `ProviderModelMetadataParseTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，unit 链路 `221/221` 全部通过；`RunCtest_CMakeTools` 定向执行三条 Provider 资产测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 Provider Catalog 收敛为 provider manifest、models metadata 与 overlay 规则的唯一 owner，只允许 `auth_ref`、`header_refs`、`base_url_alias` 与 `activation_flag` 走 mutable overlay，禁止 deployment/snapshot 静默改写 pricing、context window、metadata_effective_at 与 feature-level `verification_state` 的静态真相源，同时继续保持真实密钥只以 reference 形态存在于资产中。
6. 阶段结论：阶段 D 的 013/014 已全部完成，LLM-GATE-03 达到通过条件。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-015 已具备执行条件，可继续实现 `PromptRegistry` 选择逻辑。

## 16. 阶段 E 执行记录

### 16.1 LLM-TODO-015

1. 状态：Done；`PromptRegistry` 选择逻辑已落盘，015 为 017/019/032/033 提供了稳定的 Prompt release 选择面。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-015-PromptRegistry选择逻辑设计收敛.md](deliverables/LLM-TODO-015-PromptRegistry选择逻辑设计收敛.md)。
3. 代码交付：[llm/include/prompt/PromptQuery.h](../../../llm/include/prompt/PromptQuery.h)、[llm/src/prompt/PromptRegistry.h](../../../llm/src/prompt/PromptRegistry.h)、[llm/src/prompt/PromptRegistry.cpp](../../../llm/src/prompt/PromptRegistry.cpp)、[tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../tests/unit/llm/PromptRegistrySelectionTest.cpp)、[tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `223/223` 全部通过，其中新增的 `PromptRegistrySelectionTest` 与 `PromptRegistryTrustSourceTest` 均通过；`RunCtest_CMakeTools` 定向执行两条 PromptRegistry 测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 Prompt 选择稳定性、显式 release override 与 trusted source fail-closed 收敛进 Registry owner 边界，没有把消息装配、policy allowlist 或 provider 私有分支混进 `PromptRegistry`，也没有反向扩张 shared Prompt contracts。
6. blocker 修复：015 发现 007 的接口冻结文档与详细设计 6.6.3 / 6.15.5 对显式 `prompt_release_id` 的要求存在冲突，因此在本轮将该字段作为 direct blocker fix 最小补入 `PromptQuery`，并在交付物中显式记账；未借机扩张到 shared contracts 或 PromptComposeRequest/Result。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-016 已具备执行条件，可继续实现 `TokenEstimator` 预估器。

### 16.2 LLM-TODO-016

1. 状态：Done；`TokenEstimator` 预估器已落盘，016 为 017/018/020 提供了统一的 token 预算事实源。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-016-TokenEstimator预估器设计收敛.md](deliverables/LLM-TODO-016-TokenEstimator预估器设计收敛.md)。
3. 代码交付：[llm/src/TokenEstimator.h](../../../llm/src/TokenEstimator.h)、[llm/src/TokenEstimator.cpp](../../../llm/src/TokenEstimator.cpp)、[tests/unit/llm/TokenEstimatorTest.cpp](../../../tests/unit/llm/TokenEstimatorTest.cpp)、[tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp)、[tests/unit/llm/ModelRouterPolicyTest.cpp](../../../tests/unit/llm/ModelRouterPolicyTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `226/226` 全部通过，其中新增的 `TokenEstimatorTest`、`PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 均通过；`RunCtest_CMakeTools` 定向执行三条 016 验收测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮只落地 provider-neutral 的 token 预估与 `over_budget` 事实输出，没有把 PromptComposer 的消息装配、PromptPolicy 的治理顺序或 ModelRouter 的完整候选评分提前揉进 TokenEstimator。
6. 验收补位：由于专项 TODO 将 `PromptComposerOverBudgetTest` 与 `ModelRouterPolicyTest` 写入 016 验收命令，而 017/020 仍未开始，本轮按最小原则补入了同名消费型测试锚点，仅验证 `TokenEstimate` 的消费者契约，不提前实现真实 PromptComposer / ModelRouter 逻辑。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-039 已具备执行条件，可继续实现 `TemplateRenderer` 安全规则与可注入渲染接口。

### 16.3 LLM-TODO-039

1. 状态：Done；`TemplateRenderer` 安全渲染器已落盘，039 为 017 提供了可注入的模板渲染 owner 和可复用的 warning 语义。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-039-TemplateRenderer安全规则与可注入渲染接口设计收敛.md](deliverables/LLM-TODO-039-TemplateRenderer安全规则与可注入渲染接口设计收敛.md)。
3. 代码交付：[llm/src/prompt/TemplateRenderer.h](../../../llm/src/prompt/TemplateRenderer.h)、[llm/src/prompt/TemplateRenderer.cpp](../../../llm/src/prompt/TemplateRenderer.cpp)、[tests/unit/llm/TemplateRendererTest.cpp](../../../tests/unit/llm/TemplateRendererTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `227/227` 全部通过，其中新增的 `TemplateRendererTest` 通过；`ListBuildTargets_CMakeTools` 与 `ListTests_CMakeTools` 已分别发现 `dasall_template_renderer_unit_test` 与 `TemplateRendererTest`；`RunCtest_CMakeTools` 定向执行 `TemplateRendererTest` 结果为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把模板变量替换、未匹配 warning、嵌套拒绝、长度截断和字面化转义收敛进 module-local renderer owner 边界，没有扩张 shared Prompt contracts，也没有把 PromptComposer 的消息装配或 PromptPolicy 的注入防御提前揉进 039。
6. 设计收敛：039 采用 `ITemplateRenderer` + `TemplateRenderResult` 的内部注入面，值内 `{{` / `}}` 使用反斜杠字面化，并按 UTF-8 码点而非字节执行长度截断；这样 017 后续可以直接复用 warning 和布尔标志，而不需要重写第二套模板错误面。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-017 已具备执行条件，可继续实现 `PromptComposer` 装配流程。

### 16.4 LLM-TODO-017

1. 状态：Done；`PromptComposer` 已落盘，017 将 Prompt 治理链从 Prompt 选择 / 模板渲染 / token 预估推进到了可执行的 provider-neutral message 装配面。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-017-PromptComposer装配流程设计收敛.md](deliverables/LLM-TODO-017-PromptComposer装配流程设计收敛.md)。
3. 代码交付：[llm/src/prompt/PromptComposer.h](../../../llm/src/prompt/PromptComposer.h)、[llm/src/prompt/PromptComposer.cpp](../../../llm/src/prompt/PromptComposer.cpp)、[tests/unit/llm/PromptComposerSlotMappingTest.cpp](../../../tests/unit/llm/PromptComposerSlotMappingTest.cpp)、[tests/unit/llm/PromptComposerOverBudgetTest.cpp](../../../tests/unit/llm/PromptComposerOverBudgetTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已发现 `dasall_prompt_composer_slot_mapping_unit_test`、`dasall_prompt_composer_over_budget_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `PromptComposerSlotMappingTest` 与 `PromptComposerOverBudgetTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `228/228` 全部通过；`RunCtest_CMakeTools` 定向执行两条 017 验收测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 PromptComposer 收敛为“只做装配、warning 汇总和 token 预估”的 module-local owner，没有让它回读 memory / knowledge，也没有扩张 shared `PromptComposeRequest` / `PromptRelease` / `PromptComposeResult`；over-budget 路径现已固定为“保留完整消息并返回 warning”，不在 llm 内自行做预算驱动的 few-shot 删除或语义重裁剪。
6. 设计收敛：017 仅映射当前 contracts 已冻结的字段；对于 `user_goal` 之类当前并不存在的 richer semantic slots，Composer 保留占位文本并回传 `unmatched_variable:*` warning，而不是伪造语义输入。few-shot 注入则通过 `inline:` 与注入式 resolver 收敛，并对无法解析的引用显式记账 `unresolved_few_shot_ref:*`。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-018 已具备执行条件，可继续实现 `PromptPolicy` 治理流程。

### 16.5 LLM-TODO-018

1. 状态：Done；`PromptPolicy` 已落盘，018 将 Prompt 治理链推进到了发送前的最终治理裁定面，并把 allowlist / trusted source / tool visibility / redaction / render budget 固定为可测试顺序。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-018-PromptPolicy治理流程设计收敛.md](deliverables/LLM-TODO-018-PromptPolicy治理流程设计收敛.md)。
3. 代码交付：[llm/include/prompt/PromptPolicyInput.h](../../../llm/include/prompt/PromptPolicyInput.h)、[llm/src/prompt/PromptPolicy.h](../../../llm/src/prompt/PromptPolicy.h)、[llm/src/prompt/PromptPolicy.cpp](../../../llm/src/prompt/PromptPolicy.cpp)、[llm/src/LLMSubsystemConfig.cpp](../../../llm/src/LLMSubsystemConfig.cpp)、[tests/unit/llm/PromptPolicyAllowlistTest.cpp](../../../tests/unit/llm/PromptPolicyAllowlistTest.cpp)、[tests/unit/llm/PromptPolicyToolVisibilityTest.cpp](../../../tests/unit/llm/PromptPolicyToolVisibilityTest.cpp)、[tests/unit/llm/PromptPolicyProfileDiffTest.cpp](../../../tests/unit/llm/PromptPolicyProfileDiffTest.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已发现 `dasall_prompt_policy_allowlist_unit_test`、`dasall_prompt_policy_tool_visibility_unit_test`、`dasall_prompt_policy_profile_diff_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `PromptPolicyAllowlistTest`、`PromptPolicyToolVisibilityTest` 与 `PromptPolicyProfileDiffTest`；`Build_CMakeTools` 定向构建三条 PromptPolicy 目标成功，随后构建 `dasall_llm_subsystem_config_projection_unit_test` 与 `dasall_unit_tests` 也成功，并在 unit 标签链路中显示 `231/231` 全部通过；`RunCtest_CMakeTools` 定向执行三条 018 验收测试及 `LLMSubsystemConfigProjectionTest` 结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 PromptPolicy 收敛为“只做治理裁定、redaction 和记账”的 module-local owner，不授予工具权限、不读取 memory 原始候选、不重排 ContextPacket，也不把 provider 私有参数混回 Prompt 选择共享语义；over-budget 路径现已固定为“返回 `OverBudget` 并回流 Runtime”。
6. direct blocker fix：018 发现 009 冻结时的 `PromptPolicyInput` 仍缺少实际选中的 release/source/tools 事实，导致 Policy 无法在不猜测上下文的前提下完成 trusted source 与 tool visibility 裁定。因此本轮仅在 module-local 输入面补入 `selected_release_scope`、`selected_trusted_source` 与 `visible_tools`，并同步验证 `cloud_full` 与 `edge_minimal` 的 profile 差异确实来自 profiles 投影视图，而非代码硬编码。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-019 已具备执行条件，可继续实现 `PromptPipeline` 三段编排，并把 015/017/018 的实际结果串接起来。

### 16.6 LLM-TODO-019

1. 状态：Done；`PromptPipeline` 已落盘，019 将 Prompt 三段治理真正收口为统一 façade，并把 Runtime 侧对 Registry / Composer / Policy 的内部顺序依赖压回 llm 子系统内部。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-019-PromptPipeline三段编排设计收敛.md](deliverables/LLM-TODO-019-PromptPipeline三段编排设计收敛.md)。
3. 代码交付：[llm/src/prompt/PromptPipeline.h](../../../llm/src/prompt/PromptPipeline.h)、[llm/src/prompt/PromptPipeline.cpp](../../../llm/src/prompt/PromptPipeline.cpp)、[tests/unit/llm/PromptPipelineTest.cpp](../../../tests/unit/llm/PromptPipelineTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已发现 `dasall_prompt_pipeline_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `PromptPipelineTest`；`Build_CMakeTools` 定向构建 `dasall_prompt_pipeline_unit_test` 成功；`RunCtest_CMakeTools` 定向执行 `PromptPipelineTest` 结果为 1/1 通过；随后 `Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `232/232` 全部通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 PromptPipeline 收敛为“只做三段编排、失败即停、结果透传”的 module-local façade owner，不新增治理逻辑、不访问 memory、不发起模型调用，也不绕过 PromptPolicy 直接产出 governed messages。
6. 设计收敛：019 使用 `render_budget_tokens -> ModelBudgetHint.context_window` 的最小桥接让 017 保持既有 budget hint 输入，同时在运行时把 `selected_release_scope`、`selected_trusted_source` 与 `visible_tools` 富化进 `PromptPolicyInput`，从而真正闭合 018 的 direct blocker，而不扩 public/shared interface。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-020 已具备执行条件，可继续实现 `ModelRouter` 路由选择。

## 17. 阶段 F 执行记录

### 17.1 LLM-TODO-020

1. 状态：Done；`ModelRouter` 已落盘，020 为 021/024 提供了 profile route envelope 到具体 `provider_id/model_id` 的稳定解析面。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-020-ModelRouter路由选择设计收敛.md](deliverables/LLM-TODO-020-ModelRouter%E8%B7%AF%E7%94%B1%E9%80%89%E6%8B%A9%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/route/ModelRouter.h](../../../llm/src/route/ModelRouter.h)、[llm/src/route/ModelRouter.cpp](../../../llm/src/route/ModelRouter.cpp)、[tests/unit/llm/ModelRouterTestSupport.h](../../../tests/unit/llm/ModelRouterTestSupport.h)、[tests/unit/llm/ModelRouterPolicyTest.cpp](../../../tests/unit/llm/ModelRouterPolicyTest.cpp)、[tests/unit/llm/ModelRouterFallbackTest.cpp](../../../tests/unit/llm/ModelRouterFallbackTest.cpp)、[tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp](../../../tests/unit/llm/ModelRouterReasoningModeSelectionTest.cpp)、[tests/unit/llm/ModelRouterStabilityTest.cpp](../../../tests/unit/llm/ModelRouterStabilityTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_model_router_policy_unit_test`、`dasall_model_router_fallback_unit_test`、`dasall_model_router_reasoning_mode_selection_unit_test`、`dasall_model_router_stability_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出四条 ModelRouter 用例；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，unit 标签链路 `235/235` 全部通过；`RunCtest_CMakeTools` 定向执行 `ModelRouterPolicyTest`、`ModelRouterFallbackTest`、`ModelRouterReasoningModeSelectionTest` 与 `ModelRouterStabilityTest` 均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 profile 中的抽象 route 名继续保留为输入 envelope，但 `ResolvedModelRoute` 的输出已经收敛到具体 `provider_id/model_id`；route owner 只消费 `LLMSubsystemConfig`、`ProviderCatalogSnapshot`、`ModelSelectionHint` 与 module-local health snapshot，不扩 public/shared contracts，也不在代码里写死除了测试资产外的厂商专有分支。`timeout_policy` 虽随 012 投影进入 `LLMSubsystemConfig`，但本轮仍明确不进入选路评分，相关 timeout / retry / breaker 执行治理继续留给 040/024。
6. 设计收敛：020 已固定 `候选集装配 -> 硬过滤 -> 确定性评分 -> fallback chain 展开` 四步；其中硬过滤覆盖 context window、output hard limit、tools verified、reasoning capability、provider trusted source、summary verification_state 与 health blocked，评分再叠加 route order、tier match、reasoning/task 偏好、interactive SLA、hard-cap budget、visible reasoning 与 previous_route_failures。为满足详细设计 6.6.6 的“interactive / hard_cap 可降档”要求，本轮显式加入 `interactive_hard_cap_downgrade` 偏置，并使用 `score desc -> route_order asc -> route_id lexicographic asc` 做稳定 tie-break。`selection_reason_codes` 继续只表达最终胜出理由，而成功 fallback 下主路被拒绝的 `health_blocked` / `tools_unverified` 等事实已补入 module-local `audit_evidence`，用于后续内部审计与观测接线。
7. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-021 已具备执行条件，可继续实现 `AdapterRegistry` 生命周期与健康快照，并直接消费 020 预留的 route key / health snapshot 注入面。

### 17.2 LLM-TODO-021

1. 状态：Done；`AdapterRegistry` 已落盘，021 为 024/040/041 提供了 concrete route key 到 adapter handle 的统一 owner，以及可被 020 直接消费的 lock-free health snapshot。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-021-AdapterRegistry生命周期与健康快照设计收敛.md](deliverables/LLM-TODO-021-AdapterRegistry生命周期与健康快照设计收敛.md)。
3. 代码交付：[llm/include/HealthStatus.h](../../../llm/include/HealthStatus.h)、[llm/src/route/AdapterRegistry.h](../../../llm/src/route/AdapterRegistry.h)、[llm/src/route/AdapterRegistry.cpp](../../../llm/src/route/AdapterRegistry.cpp)、[tests/mocks/include/MockLLMAdapter.h](../../../tests/mocks/include/MockLLMAdapter.h)、[tests/unit/llm/AdapterHealthProbeTest.cpp](../../../tests/unit/llm/AdapterHealthProbeTest.cpp)、[tests/unit/llm/LLMManagerFallbackTest.cpp](../../../tests/unit/llm/LLMManagerFallbackTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_adapter_health_probe_unit_test`、`dasall_llm_manager_fallback_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `AdapterHealthProbeTest` 与 `LLMManagerFallbackTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `237/237` 全部通过，其中新增的两条 021 用例均通过；`RunCtest_CMakeTools` 定向执行 `AdapterHealthProbeTest` 与 `LLMManagerFallbackTest` 结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 registry owner 收敛为“只管理 concrete route registration、adapter handle lookup、health probe 聚合与 failure counter”，不复制 020 的路由评分，不提前混入 040 的 timeout/retry/circuit breaker 执行逻辑，也不触碰 041 的 provider config projection。`ModelRouter` 继续只读取 `ModelRouterHealthSnapshot` 这一 module-local 投影视图。
6. direct blocker fix：021 执行前，`ILLMAdapter::health_check()` 与 `ILLMManager::health_check()` 只有前向声明的 `HealthStatus`，真实定义仍停留在 test-local 的 `MockLLMAdapter` 内，导致 registry 无法安全 materialize health probe 返回值。本轮仅将 `HealthStatus` 提升为 llm 公共 leaf type，并让 `MockLLMAdapter` 复用该定义；SPI 方法签名保持不变，未把 health shape 推入 infra 或 shared contracts。
7. 设计收敛：021 采用 `std::shared_ptr<const AdapterRegistrySnapshot>` 的 atomic load/store 发布 immutable snapshot，写路径在短锁内 copy-on-write，读路径不持锁；`probe_health()` 明确在锁外调用 `adapter->health_check()`，`record_call_failure()` 则在 040 之前先提供最小 failure counter / blocked 阈值入口，使 020 能通过 `health_blocked` 与 `health_failure_penalty` 同时表达 hard filter 和降权事实。
8. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-040 已具备执行条件，可继续实现 unary 调用执行治理，并直接消费 021 的 failure counter / blocked 路由状态。

### 17.3 LLM-TODO-040

1. 状态：Done；040 已在 llm 内补齐 unary 调用执行治理 owner，直接消费 021 的 failure counter / blocked route，而没有把 breaker/health 语义再拆成新的独立组件。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-040-LLMManager调用执行治理设计收敛.md](deliverables/LLM-TODO-040-LLMManager%E8%B0%83%E7%94%A8%E6%89%A7%E8%A1%8C%E6%B2%BB%E7%90%86%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/LLMManager.h](../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp)、[tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp](../../../tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp)、[tests/unit/llm/LLMManagerRetryBudgetTest.cpp](../../../tests/unit/llm/LLMManagerRetryBudgetTest.cpp)、[tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp](../../../tests/unit/llm/LLMManagerConcurrencyGuardTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_manager_timeout_policy_unit_test`、`dasall_llm_manager_retry_budget_unit_test`、`dasall_llm_manager_concurrency_guard_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `LLMManagerTimeoutPolicyTest`、`LLMManagerRetryBudgetTest` 与 `LLMManagerConcurrencyGuardTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `240/240` 全部通过；`RunCtest_CMakeTools` 定向执行三条 040 用例结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮只落 `LLMCallExecutor` 的同 route timeout/retry/concurrency 治理，不提前实现 024 的 route chain 编排、022 的 ResponseNormalizer、023 的 UsageAggregator，也不回改 021 的 registry owner。`circuit_breaker_threshold` 的阈值判定仍由 AdapterRegistry blocked route 表达，040 只负责在执行路径消费这一路由状态。
6. 无 blocker：040 开始前未发现新的前置 BLOCK 任务；021 已经提供 `record_call_failure()` / `record_call_success()` 与 blocked route 状态入口，足以支撑本轮执行治理闭环。
7. 设计收敛：040 采用“deadline 传播 + 后验超时判定”的同步 unary 策略：每次调用都把有效 timeout 写入 `LLMRequest.timeout_ms` 并测量真实耗时；超时后即使 adapter 返回 success 也按 `ProviderTimeout` 收敛。retry 仅对 retryable transport failure 与 synthetic timeout 生效，且严格受 `retry_budget + 1` 上限约束；并发治理则使用无等待的 atomic slot acquisition，以 `worker_threads` 作为 inflight 上限，达到上限后立即 reject，不写入 route health counter。
8. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-022 已具备执行条件，可继续实现 `ResponseNormalizer` 语义归一化，为 023/024 收口做准备。

### 17.4 LLM-TODO-022

1. 状态：Done；`ResponseNormalizer` 已落盘，022 为 023/024 提供了共享 `LLMResponse` 的统一收口点，以及 prompt cache hit/miss 等 provider usage side-channel 的稳定输入面。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-022-ResponseNormalizer语义归一化设计收敛.md](deliverables/LLM-TODO-022-ResponseNormalizer%E8%AF%AD%E4%B9%89%E5%BD%92%E4%B8%80%E5%8C%96%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/adapters/AdapterCallResult.h](../../../llm/src/adapters/AdapterCallResult.h)、[llm/src/execution/ResponseNormalizer.h](../../../llm/src/execution/ResponseNormalizer.h)、[llm/src/execution/ResponseNormalizer.cpp](../../../llm/src/execution/ResponseNormalizer.cpp)、[tests/unit/llm/ResponseNormalizerSemanticMappingTest.cpp](../../../tests/unit/llm/ResponseNormalizerSemanticMappingTest.cpp)、[tests/unit/llm/ResponseNormalizerReasoningContentStripTest.cpp](../../../tests/unit/llm/ResponseNormalizerReasoningContentStripTest.cpp)、[tests/unit/llm/ResponseNormalizerUsageTest.cpp](../../../tests/unit/llm/ResponseNormalizerUsageTest.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_response_normalizer_semantic_mapping_unit_test`、`dasall_response_normalizer_reasoning_content_strip_unit_test` 与 `dasall_response_normalizer_usage_unit_test`；`ListTests_CMakeTools` 已列出 `ResponseNormalizerSemanticMappingTest`、`ResponseNormalizerReasoningContentStripTest` 与 `ResponseNormalizerUsageTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `243/243` 全部通过；`RunCtest_CMakeTools` 定向执行三条 022 用例结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 shared `LLMResponse` 的结构校验、finish_reason 规范化、provider-private `reasoning_content` 剥离与 raw usage fragment 提取收敛为 module-local `ResponseNormalizer` owner，没有把成本计算提前塞进 022，也没有在 shared contracts 中引入 provider raw payload / trace id / cache hit/miss 字段。
6. direct blocker fix：022 开始前，[llm/src/adapters/AdapterCallResult.h](../../../llm/src/adapters/AdapterCallResult.h) 只有 `response/error/result_code` 三元组，无法表达 `reasoning_content` 或 prompt cache hit/miss usage。当前轮次仅在该 module-local 类型内补入 `AdapterUsageFragment` 与 `AdapterProviderDiagnostics` side channel，不改 public/shared ABI。
7. 设计收敛：022 采用“adapter 已产出 shared `LLMResponse`，normalizer 再做 fail-closed 守卫与 metadata 富化”的最小实现路径；unknown finish_reason 统一收敛为 `unknown` 并写入审计事件，malformed payload 统一映射为 module-local ProviderProtocol failure，`ValidationFieldMissing` 仅作为现有 shared code carrier 使用。
8. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-023 已具备执行条件，可继续实现 `UsageAggregator` 用量与成本归并。

### 17.5 LLM-TODO-023

1. 状态：Done；`UsageAggregator` 已落盘，023 为 024 与后续 028 提供了统一的 token/cost 归并 owner，以及最小可消费的成本观测锚点。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-023-UsageAggregator用量与成本归并设计收敛.md](deliverables/LLM-TODO-023-UsageAggregator%E7%94%A8%E9%87%8F%E4%B8%8E%E6%88%90%E6%9C%AC%E5%BD%92%E5%B9%B6%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/UsageAggregator.h](../../../llm/src/UsageAggregator.h)、[llm/src/UsageAggregator.cpp](../../../llm/src/UsageAggregator.cpp)、[tests/unit/llm/ResponseNormalizerUsageTest.cpp](../../../tests/unit/llm/ResponseNormalizerUsageTest.cpp)、[tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_observability_field_completeness_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `ResponseNormalizerUsageTest` 与 `LLMObservabilityFieldCompletenessTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `244/244` 全部通过；`RunCtest_CMakeTools` 定向执行两条 023 验收测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：本轮把 raw usage fragment 到 `NormalizedUsageRecord` 的 token/cost 归并收敛为 module-local `UsageAggregator` owner，没有回写静态 Provider Catalog，也没有把成本估值塞回 shared `LLMResponse` 或提前实现 028 的 bridge sink。
6. acceptance 补位：023 的验收命令引用了 `LLMObservabilityFieldCompletenessTest`，而该完整 bridge 用例按专项 TODO 原本属于 028。当前轮次按最小原则先新增同名 unit 用例，只验证 `provider_id`、`model_id`、`pricing_ref` 与 `estimated_cost_usd` 这些成本锚点已可被观测；完整字段矩阵继续留给 028 在同名文件上扩展。
7. 设计收敛：当 usage fragment 未显式拆出 cache hit/miss 时，023 默认把全部 `prompt_tokens` 视作 miss tokens；当 pricing metadata 缺失时，023 选择 `estimated_cost_usd = 0.0` 的 graceful fallback，但仍保留 token totals 和 provider/model identity，不阻塞主链。
8. 后继任务：按专项 TODO 的串行顺序，LLM-TODO-024 已具备执行条件，可继续实现 `LLMManager` unary 编排与结果装配。

### 17.6 LLM-TODO-024

1. 状态：Done；`LLMManager` 已在 llm 内补齐 unary concrete owner，024 把 019 的 `PromptPipeline`、020 的 `ModelRouter`、021 的 `AdapterRegistry`、040 的执行治理、022 的 `ResponseNormalizer` 与 023 的 `UsageAggregator` 收束成一条可执行主链，同时没有扩张新的 public/shared ABI。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-024-LLMManager-unary编排与结果装配设计收敛.md](deliverables/LLM-TODO-024-LLMManager-unary%E7%BC%96%E6%8E%92%E4%B8%8E%E7%BB%93%E6%9E%9C%E8%A3%85%E9%85%8D%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/LLMManager.h](../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp)、[tests/unit/llm/LLMManagerSuccessPathTest.cpp](../../../tests/unit/llm/LLMManagerSuccessPathTest.cpp)、[tests/unit/llm/LLMManagerFallbackTest.cpp](../../../tests/unit/llm/LLMManagerFallbackTest.cpp)、[tests/unit/llm/LLMManagerFailureMappingTest.cpp](../../../tests/unit/llm/LLMManagerFailureMappingTest.cpp)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_manager_success_path_unit_test`、`dasall_llm_manager_fallback_unit_test`、`dasall_llm_manager_failure_mapping_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `LLMManagerSuccessPathTest`、`LLMManagerFallbackTest` 与 `LLMManagerFailureMappingTest`；`Build_CMakeTools` 先构建六个 manager 相关可执行目标（success/fallback/failure mapping + timeout/retry/concurrency）成功，随后构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `246` 条单测全部通过；`RunCtest_CMakeTools` 定向执行六条 manager 用例结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：024 只让 manager 做组件编排、结果装配和 failure category 收口，不在 manager 内重写 prompt 选择、route 评分、provider raw payload 解析或成本计算；`stream_generate()` 继续明确保持 unary 阶段的 fail-closed 占位。
6. 设计收敛：024 采用“默认自举 + 单测可注入”的最小 concrete owner 方案：默认路径会自举 `PromptPipeline`、`ModelRouter`、`LLMCallExecutor` 与 Provider Catalog snapshot；单测路径则允许注入现成 snapshot / registry / pipeline，避免 024 为了可测性额外新增 public 工厂接口。成功路径会把 route、selection reason、provider trace 与 usage/cost anchor 追加到 `LLMResponse.tags`，为后续 028 的 observability bridge 保留稳定输入面；多 route 失败则统一升格为 `FallbackExhausted`，并保留完整 `attempted_routes` 与最后一次 failure code/error。
7. direct blocker fix：024 初版实现一度把 `ProviderCatalogRepository` concrete 依赖直接暴露到 [llm/src/LLMManager.h](../../../llm/src/LLMManager.h)，导致 manager 相关单测编译面扩大。当前轮次已改为前向声明 + implementation-only shared_ptr，继续把 provider catalog concrete owner 留在实现文件中，避免新的 header 级 ABI 泄漏。
8. 后继任务：按专项 TODO 的串行顺序，下一步可继续推进 `LLM-TODO-025`，开始实现 `OpenAICompatibleAdapter` skeleton；若优先补观测闭环，则可转入 `LLM-TODO-028` 统一接线 logs/metrics/trace/audit bridge。

### 17.7 LLM-TODO-041

1. 状态：Done；041 已把 provider runtime overlay 到 adapter init 的最小投影链闭合，`LLMAdapterConfig` 现在可显式承载 provider instance、base_url alias、activation flag 与 snapshot version。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-041-ProviderConfig投影与mutable-overlay规则设计收敛.md](deliverables/LLM-TODO-041-ProviderConfig%E6%8A%95%E5%BD%B1%E4%B8%8Emutable-overlay%E8%A7%84%E5%88%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/include/LLMAdapterConfig.h](../../../llm/include/LLMAdapterConfig.h)、[llm/include/LLMSubsystemConfig.h](../../../llm/include/LLMSubsystemConfig.h)、[llm/src/LLMSubsystemConfig.cpp](../../../llm/src/LLMSubsystemConfig.cpp)、[llm/src/route/AdapterRegistry.h](../../../llm/src/route/AdapterRegistry.h)、[llm/src/route/AdapterRegistry.cpp](../../../llm/src/route/AdapterRegistry.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[tests/unit/llm/ProviderConfigProjectionTest.cpp](../../../tests/unit/llm/ProviderConfigProjectionTest.cpp)、[tests/unit/llm/ProviderCatalogOverlayTest.cpp](../../../tests/unit/llm/ProviderCatalogOverlayTest.cpp)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_provider_config_projection_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 已列出 `ProviderConfigProjectionTest` 与 `ProviderCatalogOverlayTest`；`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `247/247` 全部通过；`RunCtest_CMakeTools` 定向执行两条 041 验收测试结果均为 1/1 通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：041 继续保持 014/6.15.2 的 owner 边界不变，`ProviderCatalogRepository` 仍只负责 truth-source 与 mutable overlay；`LLMSubsystemConfig` 负责 provider runtime overlay 到 `LLMAdapterConfig` 的 fail-closed 投影；`AdapterRegistry` 只补 adapter `init()` + route registration 的最小 glue code，没有提前引入 concrete adapter family、endpoint 解析器或 `LLMManager` 自动装配逻辑。
6. blocker 结论：041 已完成 `LLM-BLK-007` 在 llm 内部的最小解阻动作，把 `auth_ref`、`header_refs`、`base_url alias`、activation flag 与 snapshot version 从资产/overlay 安全投影到 adapter init 输入；但这仍不等于 Cloud/LAN/Local 真实 endpoint 已联调就绪，042 与 025/026/027 后续任务仍需分别验证 asset-only onboarding 与 family skeleton。
7. 设计收敛：041 新增 `ProviderRuntimeProjectionView` 作为 module-local provider runtime 视图，projection 阶段会复用 timeout/retry 投影、校验 `auth_ref` 与 `header_refs` 只能是 reference、合并 descriptor/runtime capability tags，并在 provider instance 被显式禁用时于 registry 层 fail-closed，确保 adapter init 前就能阻断非法或未激活实例。
8. 后继任务：按专项 TODO 的串行顺序，下一步可继续推进 `LLM-TODO-025`，开始实现 `OpenAICompatibleAdapter` skeleton；`LLM-TODO-042` 还需等待 025 与 smoke integration 准备完成后再做 asset-only onboarding 验证。

### 17.8 LLM-TODO-025

1. 状态：Done；025 已在 llm 内补齐首个 concrete provider family skeleton，为后续 026/027 与 042 提供可复用的 transport / adapter 接缝，同时没有扩大 shared contracts 或绕过 022/024 已冻结的 owner 边界。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-025-OpenAICompatibleAdapter-skeleton设计收敛.md](deliverables/LLM-TODO-025-OpenAICompatibleAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/include/ILLMTransport.h](../../../llm/include/ILLMTransport.h)、[llm/src/adapters/OpenAICompatibleAdapter.h](../../../llm/src/adapters/OpenAICompatibleAdapter.h)、[llm/src/adapters/OpenAICompatibleAdapter.cpp](../../../llm/src/adapters/OpenAICompatibleAdapter.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../tests/unit/llm/AdapterProtocolMappingTest.cpp)、[tests/unit/llm/AdapterHealthProbeTest.cpp](../../../tests/unit/llm/AdapterHealthProbeTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../tests/unit/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_adapter_protocol_mapping_unit_test`、`dasall_adapter_health_probe_unit_test` 与 `dasall_unit_tests`；第一次 `ListTests_CMakeTools` 暴露出 `AdapterProtocolMappingTest` 未注册的 discoverability 缺口，当前轮次已补齐 `add_test()` 注册并重新构建，随后 unit 总数从 `247` 增至 `248`，且 `ListTests_CMakeTools` 与聚合 build 均能看到 `AdapterProtocolMappingTest` / `AdapterHealthProbeTest`。`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `248/248` 全部通过；`RunCtest_CMakeTools` 定向执行 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest` 均为 `100% tests passed, 0 tests failed out of 1`。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：025 把 provider-specific 细节限制在 llm 模块内，新增的 `ILLMTransport` 只承载 URL、auth/header refs、base_url alias、snapshot version、body 与 timeout 等 adapter-internal transport 信息；`OpenAICompatibleAdapter` 只负责 OpenAI-compatible family 的 unary 请求映射、最小响应转换与 health probe，不接手 Prompt 选择、route 评分、secret 解析或 provider raw payload 向 shared contracts 的泄漏。
6. blocker 结论：025 继续尊重 `LLM-BLK-007` 的范围约束，只做 mock transport 下的 skeleton，不宣称真实 endpoint 联调已完成。`auth_ref` / `header_refs` 仍以 reference 形式透传给 transport，base_url alias 也只作为下游 endpoint indirection 元数据保留，未在本轮引入真实 secret 或 endpoint resolver。
7. 设计收敛：025 采用“shared request -> OpenAI-compatible chat-completions -> shared response draft + diagnostics”的最小实现路径。`generate()` 从 `request.model_route` 提取 concrete model id，把 prefixed `system:` / `user:` 消息映射成 role/content 数组；2xx 响应路径提取 `content`、`finish_reason`、`usage`、provider trace id 与 `reasoning_content` side channel，其中 `reasoning_content` 继续停留在 `AdapterProviderDiagnostics` 供 022 剥离，不进入 shared `LLMResponse`。`health_check()` 则用 `GET {base_url}/models` 将 concrete adapter 健康状态划分为 healthy / degraded / unavailable 三态，供 021 的 registry 快照消费。
8. 后继任务：按专项 TODO 的阶段 G 顺序，下一步可继续推进 `LLM-TODO-026`，实现 `OllamaAdapter` skeleton；若要优先做跨 family 复用验证，则需等待 smoke integration 条件满足后再进入 `LLM-TODO-042`。

### 17.9 LLM-TODO-026

1. 状态：Done；026 已为 llm 补齐第二个 concrete provider family skeleton，把 LAN/Ollama native 协议映射、usage 推导与健康探针接到既有 transport/test seam 上，没有扩大 shared contracts 或 024/022 的 owner 边界。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-026-OllamaAdapter-skeleton设计收敛.md](deliverables/LLM-TODO-026-OllamaAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/adapters/OllamaAdapter.h](../../../llm/src/adapters/OllamaAdapter.h)、[llm/src/adapters/OllamaAdapter.cpp](../../../llm/src/adapters/OllamaAdapter.cpp)、[tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../tests/unit/llm/AdapterProtocolMappingTest.cpp)、[tests/unit/llm/AdapterHealthProbeTest.cpp](../../../tests/unit/llm/AdapterHealthProbeTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 继续列出 `dasall_adapter_protocol_mapping_unit_test`、`dasall_adapter_health_probe_unit_test` 与 `dasall_unit_tests`；`ListTests_CMakeTools` 继续列出 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest`。`Build_CMakeTools` 构建 `dasall_unit_tests` 成功，并在 unit 标签链路中显示 `248/248` 全部通过，其中扩展后的 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest` 均通过；`RunCtest_CMakeTools` 定向执行两条 026 验收测试结果均为 `100% tests passed, 0 tests failed out of 1`。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：026 继续复用 025 冻结的 `ILLMTransport`，adapter 只消费 041 已投影好的 `auth_ref`、`header_refs`、`base_url_alias` 与 `snapshot_version`，并把 Ollama native 的 provider-specific 细节限制在 llm 模块内部。`OllamaAdapter` 只负责 `/api/chat` 请求映射、`/api/tags` 健康探针、usage 推导与 diagnostics 保留，不接手 Prompt 选择、route 评分、secret 解析或 normalizer owner。
6. blocker 结论：026 继续尊重 `LLM-BLK-007` 的范围约束，只在 mock transport 下完成 skeleton，不宣称真实 LAN endpoint、本地模型拉取或 auth/header 注入链已联通。`auth_ref` 仍以 reference 形式透传给 transport，未在 adapter 内部放松 reference contract。
7. 设计收敛：026 采用“shared request -> Ollama native chat -> shared response draft + diagnostics”的最小路径。`generate()` 把 prefixed `developer:` 消息下沉为 Ollama 支持的 `system` role，其余 `system/user/assistant/tool` 直映射到 `messages` 数组；`response_format == json_object` 映射为 `format = json`，`max_output_tokens` 映射为 `options.num_predict`；成功路径从 `message.content`、`done_reason`、`prompt_eval_count` 与 `eval_count` 收敛 shared 响应和 usage，并把 `message.thinking` 保留在 `AdapterProviderDiagnostics.reasoning_content`。`health_check()` 则用 `GET {base_url}/api/tags` 将 concrete adapter 健康状态划分为 healthy / degraded / unavailable 三态，供 021 的 registry 快照消费。
8. 后继任务：按专项 TODO 的阶段 G 顺序，下一步可继续推进 `LLM-TODO-027`，实现 `LocalLLMAdapter` skeleton；若要先验证跨 family 的配置式接入复用，则仍需等待 smoke integration 条件满足后再进入 `LLM-TODO-042`。

### 17.10 LLM-TODO-027

1. 状态：Done；027 已为 llm 补齐第三个 concrete provider family skeleton，把 Local runtime 协议映射、usage / diagnostics 收敛与健康探针接到既有 transport/test seam 上，没有扩大 shared contracts 或 024/022 的 owner 边界。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-027-LocalLLMAdapter-skeleton设计收敛.md](deliverables/LLM-TODO-027-LocalLLMAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/adapters/LocalLLMAdapter.h](../../../llm/src/adapters/LocalLLMAdapter.h)、[llm/src/adapters/LocalLLMAdapter.cpp](../../../llm/src/adapters/LocalLLMAdapter.cpp)、[tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../tests/unit/llm/AdapterProtocolMappingTest.cpp)、[tests/unit/llm/AdapterHealthProbeTest.cpp](../../../tests/unit/llm/AdapterHealthProbeTest.cpp)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 继续列出 `dasall_adapter_protocol_mapping_unit_test` 与 `dasall_adapter_health_probe_unit_test`；`ListTests_CMakeTools` 继续列出 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest`。`Build_CMakeTools` 定向构建 `dasall_adapter_protocol_mapping_unit_test` 与 `dasall_adapter_health_probe_unit_test` 成功，新增 `LocalLLMAdapter.cpp` 已完成编译并链接入 `dasall_llm`。`RunCtest_CMakeTools` 定向执行两条 027 验收测试结果均为 `100% tests passed, 0 tests failed out of 1`。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：027 继续复用 025/026 冻结的 `ILLMTransport`，adapter 只消费 041 已投影好的 `auth_ref`、`header_refs`、`base_url_alias` 与 `snapshot_version`，并把 local runtime 的 provider-specific 细节限制在 llm 模块内部。`LocalLLMAdapter` 只负责 `/generate` 请求映射、`/health` 健康探针、usage / diagnostics 收敛与 transport failure 映射，不接手 Prompt 选择、route 评分、secret 解析或 normalizer owner。
6. blocker 结论：027 继续尊重 `LLM-BLK-007` 的范围约束，只在 mock transport 下完成 skeleton，不宣称真实 local runtime、本地 IPC 或 runtime session 生命周期已联通。`local-runtime:///...` 当前只是 module-local transport 地址形式，未在本轮引入新的 shared endpoint contract。
7. 设计收敛：027 采用“shared request -> local runtime generate -> shared response draft + diagnostics”的最小路径。`generate()` 从 `request.model_route` 提取 concrete model id，沿用 `local-runtime/local-small` fixture，把 prefixed `developer:` 消息下沉为 `system` role，其余消息映射到 `messages` 数组，并固定补齐 `stream:false` 与 `execution_mode = local_runtime`；成功路径从 `output_text`、`tool_calls`、`refusal_reason`、`finish_reason`、`input_tokens`、`output_tokens`、`total_tokens`、`runtime_session_id` 与 `reasoning_trace` 收敛 shared 响应、usage 与 provider diagnostics。`health_check()` 则用 `GET {base_url}/health` 将 concrete adapter 健康状态划分为 healthy / degraded / unavailable 三态，供 021 的 registry 快照消费。
8. 后继任务：按专项 TODO 的阶段 G 顺序，下一步可继续推进 `LLM-TODO-028`，接线 llm observability bridges；待 smoke integration 条件满足后，再结合 `LLM-TODO-042` 验证 asset-only provider instance onboarding。

### 17.11 LLM-TODO-028

1. 状态：Done；028 已为 llm 补齐 logs/metrics/trace/audit 三个 observability bridge，把 6.12 冻结的关键字段收敛为统一摘要输入与三类审计事件输入，没有扩大 shared contracts 或 024 的 manager owner 边界。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-028-LLM-observability-bridges设计收敛.md](deliverables/LLM-TODO-028-LLM-observability-bridges%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/observability/LLMMetricsBridge.h](../../../llm/src/observability/LLMMetricsBridge.h)、[llm/src/observability/LLMMetricsBridge.cpp](../../../llm/src/observability/LLMMetricsBridge.cpp)、[llm/src/observability/LLMTraceBridge.h](../../../llm/src/observability/LLMTraceBridge.h)、[llm/src/observability/LLMTraceBridge.cpp](../../../llm/src/observability/LLMTraceBridge.cpp)、[llm/src/observability/LLMAuditBridge.h](../../../llm/src/observability/LLMAuditBridge.h)、[llm/src/observability/LLMAuditBridge.cpp](../../../llm/src/observability/LLMAuditBridge.cpp)、[tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp)、[tests/unit/llm/LLMAuditEventCoverageTest.cpp](../../../tests/unit/llm/LLMAuditEventCoverageTest.cpp)、[tests/unit/llm/CMakeLists.txt](../../../tests/unit/llm/CMakeLists.txt)、[llm/CMakeLists.txt](../../../llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_observability_field_completeness_unit_test`、`dasall_llm_audit_event_coverage_unit_test` 与 `dasall_llm_smoke_integration_test`；`ListTests_CMakeTools` 已列出 `LLMObservabilityFieldCompletenessTest`、`LLMAuditEventCoverageTest` 与 `LLMSubsystemSmokeIntegrationTest`。`Build_CMakeTools` 定向构建上述三个 target 成功，新增 observability bridge 源文件已编译并链接入 `dasall_llm`。`RunCtest_CMakeTools` 定向执行两条 028 unit 测试和 `LLMSubsystemSmokeIntegrationTest` 结果均为 `100% tests passed, 0 tests failed out of 1`。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：028 继续遵守 6.12/9.6 的 owner 切分，只新增 module-local bridge 与 signal contract，不扩 shared `LLMRequest` / `LLMResponse` / `LLMManagerResult`，也不把 observability 失败反写成 llm 主链失败。由于专项 TODO 只冻结 trace/metrics/audit 三个 bridge，本轮将结构化 call summary log 折叠进 `LLMMetricsBridge`，并继续使用 infra 已冻结的五元指标标签面，通过 stage token 投影 route/model/reason/provider 维度。
6. blocker 结论：028 无前置 BLOCK 任务。bridge 本轮只冻结“统一观测输入面 + sink 投影 + 单测可断言”这条最小闭环，不宣称 024 的 runtime hot path 已经自动织入所有 bridge；真正的主链时序编织和更强 integration 断言仍交由后续 smoke/integration owner 收口。
7. 设计收敛：028 采用“三信号、三 bridge”的最小路径：`LLMCallSummary` 统一承载 17 个关键日志字段和 12 个指标所需聚合输入，`LLMTraceSpanSignal` 冻结 `llm.prompt.select`、`llm.prompt.compose`、`llm.prompt.policy`、`llm.route.resolve`、`llm.adapter.invoke`、`llm.response.normalize` 六个低基数 span 名及其 attrs，`LLMAuditEvent` 则只覆盖 trusted source 失败、`reasoning_content` 剥离和 metadata drift 三类关键审计事件。所有 bridge 都采用 fail-closed + local degraded status，sink 缺失或 payload 非法时只保留本地错误，不污染 llm 调用结果。
8. 后继任务：按专项 TODO 的阶段 G 顺序，下一步可继续推进 `LLM-TODO-029`，把现有 bridge 输入面消费进 llm smoke integration，并在最小主链闭环里增强观测字段断言；若优先验证配置式扩展，则仍需等 integration 条件进一步收口后再推进 `LLM-TODO-042`。

### 17.12 LLM-TODO-029

1. 状态：Done；029 已将 [llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp) 的 unary 成功路径接到 028 冻结的 `LLMMetricsBridge` 与 `LLMTraceBridge`，并把 [tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp) 从 discoverability 锚点升级为真实的 `PromptPipeline + LLMManager + MockLLMAdapter + ResponseNormalizer` 最小闭环。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-029-LLM-smoke-integration设计收敛.md](deliverables/LLM-TODO-029-LLM-smoke-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/src/LLMManager.h](../../../llm/src/LLMManager.h)、[llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)、[tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp](../../../tests/integration/llm/LLMSubsystemSmokeIntegrationTest.cpp)。
4. 验证结果：`ListBuildTargets_CMakeTools` 继续列出 `dasall_llm_smoke_integration_test`，`ListTests_CMakeTools` 继续列出 `LLMSubsystemSmokeIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_llm_smoke_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMSubsystemSmokeIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 时，integration 聚合链路中的 36 条用例全部通过，其中 `LLMSubsystemSmokeIntegrationTest` 作为第 36 个 integration 用例通过。CTool/CTest 仍附带 `DartConfiguration.tcl` 缺失提示，但未影响返回码与断言，继续记为工具噪声而非 blocker。
5. 边界结论：029 只把 `LLMMetricsBridge` 与 `LLMTraceBridge` 织入 manager hot path，没有扩 shared `LLMRequest` / `LLMResponse` 以承载完整 `InfraContext`。`LLMAuditBridge` 继续在 smoke fixture 内消费主链产出的 `reasoning_content_stripped` 事实，避免 llm 为了发审计而私扩 session/trace/task/lease ABI；这一边界与 ADR-006/007/008 和 028 的 owner 切分保持一致。
6. blocker 结论：029 无新的前置 BLOCK 任务，但在真实 PromptPipeline 下暴露并修复了两个集成接缝：`PromptQuery.language` 必须与 prompt 资产中的 `zh-cn` 对齐，且 llm integration target 必须显式引入 `infra/include` 才能编译 observability fixture。二者都已在本轮最小修复并纳入 smoke 验证。
7. 设计收敛：029 采用“真实 Prompt 资产 + 真实 route 选择 + 单一 Mock provider”的最小 smoke 路径。`PromptPipeline` 直接命中 planner baseline 资产，`ModelRouter` 基于 `cloud.reasoning` 主路由与 tool/hard-cap 条件降级到 `deepseek-prod/deepseek-chat`，`MockLLMAdapter` 返回带 `reasoning_content`、provider trace id 与 usage fragment 的最小 provider-neutral 响应，`ResponseNormalizer` 负责剥离 provider-private reasoning content 并发出审计事实，`LLMManager` 则把 prompt/model/route/token/cost/reasoning 字段统一投影到 structured log、metrics samples 与 trace span attrs。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-030`，验证 DeepSeek 双模式 integration；若要优先验证配置式扩展路径，则 029 完成后也已具备继续推进 `LLM-TODO-042` 的 smoke 前提。

### 17.13 LLM-TODO-030

1. 状态：Done；030 已新增 [tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp)，在 029 的真实 smoke 基座上补齐 DeepSeek 同一 provider 下 `deepseek-chat` / `deepseek-reasoner` 双模式切换验证，并通过 [tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt) 将新用例接入 llm integration discoverability。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-030-DeepSeek双模式integration设计收敛.md](deliverables/LLM-TODO-030-DeepSeek%E5%8F%8C%E6%A8%A1%E5%BC%8Fintegration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp)、[tests/integration/llm/LLMIntegrationTestSupport.h](../../../tests/integration/llm/LLMIntegrationTestSupport.h)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_deepseek_dual_mode_selection_integration_test`，`ListTests_CMakeTools` 已列出 `DeepSeekDualModeSelectionIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_deepseek_dual_mode_selection_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `DeepSeekDualModeSelectionIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 时，integration 聚合链路中的 37 条用例全部通过，其中 `DeepSeekDualModeSelectionIntegrationTest` 作为第 37 个 integration 用例通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：030 没有改写生产 provider 资产、`ModelRouter` 策略或 `LLMManager` 逻辑，而是把 dual-mode 断言收敛到 integration fixture：reasoning 工作负载通过 `requires_reasoning + prefers_visible_reasoning + high complexity` 触发 `deepseek-reasoner`，interactive/hard-cap/tools 工作负载通过 `tier_degraded + interactive_latency_bias + budget_low_cost + interactive_hard_cap_downgrade` 触发 `deepseek-chat`。`reasoning_content` 仍只作为 provider-private 字段存在，经 `ResponseNormalizer` 剥离后只留下 `audit=reasoning_content_stripped` 与 `reasoning_content_stripped=true` 证据。
6. blocker 结论：030 无新的前置 BLOCK 任务，但在真实 prompt/route 闭环下暴露了两个集成条件：planner baseline prompt 资产只接受 `task_type = plan`，且不能把 pre-route `request.model_route` 误当成 prompt `model_family` 传给 `PromptRegistry`。两处接缝都已在测试请求层最小修复，并保持生产逻辑不变。
7. 设计收敛：030 采用“真实 prompt 资产 + 真实 routing + 双 concrete Mock route”最小路径。测试内将 catalog snapshot 中 reasoner 的 tools verification 调整为 verified，只为了验证 chat 降档是由复杂度/SLA/预算驱动，而不是被 capability reject 伪通过；该调整严格停留在 integration fixture，不回写 [llm/assets/providers/deepseek/models.yaml](../../../llm/assets/providers/deepseek/models.yaml) 的生产元数据。reasoner 正例断言 `selection_reason=requires_reasoning`、`selection_reason=visible_reasoning_preferred` 与 structured log/trace 中的 `reasoning_mode_requested/effective = thinking`；chat 正例断言 `selection_reason=tier_degraded`、`interactive_latency_bias`、`budget_low_cost`、`interactive_hard_cap_downgrade` 与 `reasoning_mode_effective = non_thinking`。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-031`，验证 fallback integration；030 已经把 dual-mode 切换证据固定下来，后续失败注入与 profile 差异验证都可以直接复用这套 integration support 基座。

### 17.14 LLM-TODO-031

1. 状态：Done；031 已新增 [tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp)，在 029/030 的真实 smoke 基座上补齐 cloud、LAN、local 三段 fallback 的集成证据，并通过 [tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt) 将新用例接入 llm integration discoverability。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-031-fallback-integration设计收敛.md](deliverables/LLM-TODO-031-fallback-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_fallback_integration_test`，`ListTests_CMakeTools` 已列出 `LLMFallbackIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_llm_fallback_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMFallbackIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 时，integration 聚合链路中的 38 条用例全部通过，其中 `LLMFallbackIntegrationTest` 作为第 38 个 integration 用例通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：031 没有改写生产 `LLMManager`、`ModelRouter`、adapter skeleton 或 provider 资产，而是把 fallback 断言收敛到 integration fixture：测试继续使用真实 `PromptPipeline + LLMManager + ResponseNormalizer + observability bridges`，只在 catalog snapshot 内裁掉与 031 无关的 `deepseek-reasoner` 候选，确保 fallback 证据稳定落在 `deepseek-prod/deepseek-chat -> lan-ollama/lan-general -> local-runtime/local-small` 三条 route 上。
6. blocker 结论：031 无新的前置 BLOCK 任务，但在 fallback exhausted 路径下暴露了一个真实 manager 收口细节：最终 `error.details.message` 会保留最后失败 route 的 provider 超时信息，而 `error.details.stage` 会被统一投影成 `llm.manager.execute_unary`，而不是原始 adapter stage。该行为已在 integration 断言中显式固定，并保持生产逻辑不变。
7. 设计收敛：031 采用“三条 route、三类结果、统一观测断言”的最小路径。正例 1 验证 cloud 失败后 LAN fallback 成功，并断言 `fallback_used = true`、`attempted_routes = [cloud, lan]`、structured log 中的 `from_route/to_route` 与 `outcome = degraded`；正例 2 验证 cloud 与 LAN 失败后 local degrade-chain 成功，并断言 `attempted_routes = [cloud, lan, local]`；负例则验证三条 route 全部失败后 `failure_category = FallbackExhausted`，同时保留完整 attempted routes 与最后失败消息。metrics 与 trace 也同步断言 `llm_fallback_total` 和 degraded span attrs 已被真实发射。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-032`，验证 Prompt source switch integration；031 已经把跨 route fallback 收口与观测字段证据固定下来，后续 source/persona/governance/profile 测试可继续复用同一 integration support 基座。

### 17.15 LLM-TODO-032

1. 状态：Done；032 已新增 [tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp](../../../tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp)，并补齐 [llm/include/prompt/PromptRegistryConfig.h](../../../llm/include/prompt/PromptRegistryConfig.h)、[llm/src/prompt/PromptRegistry.cpp](../../../llm/src/prompt/PromptRegistry.cpp)、[llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp) 的真实 source-chain projection，使 baseline / deployment / snapshot prompt source 能进入真实 manager 闭环，而不是只停留在 repository unit 语义。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-032-prompt-source-switch-integration设计收敛.md](deliverables/LLM-TODO-032-prompt-source-switch-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[llm/include/prompt/PromptRegistryConfig.h](../../../llm/include/prompt/PromptRegistryConfig.h)、[llm/src/prompt/PromptRegistry.cpp](../../../llm/src/prompt/PromptRegistry.cpp)、[llm/src/LLMManager.cpp](../../../llm/src/LLMManager.cpp)、[tests/unit/llm/PromptRegistrySelectionTest.cpp](../../../tests/unit/llm/PromptRegistrySelectionTest.cpp)、[tests/unit/llm/PromptRegistryTrustSourceTest.cpp](../../../tests/unit/llm/PromptRegistryTrustSourceTest.cpp)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../tests/unit/llm/InterfaceSurfaceTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)、[tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp](../../../tests/integration/llm/LLMPromptSourceSwitchIntegrationTest.cpp)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_prompt_source_switch_integration_test`、`dasall_llm_interface_surface_unit_test`、`dasall_prompt_registry_selection_unit_test`、`dasall_prompt_registry_trust_source_unit_test`、`dasall_unit_tests` 与 `dasall_integration_tests`；`ListTests_CMakeTools` 已列出 `LLMPromptSourceSwitchIntegrationTest`、`LLMInterfaceSurfaceTest`、`PromptRegistrySelectionTest` 与 `PromptRegistryTrustSourceTest`。`Build_CMakeTools` 定向构建 `dasall_llm_prompt_source_switch_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMPromptSourceSwitchIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_unit_tests` 时，unit 聚合链路中的 249 条用例全部通过；构建 `dasall_integration_tests` 时，integration 聚合链路中的 39 条用例全部通过，其中 `LLMPromptSourceSwitchIntegrationTest` 作为第 39 个 integration 用例通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：032 没有扩张 Prompt 治理 owner、Provider 路由 owner 或 shared contracts，而是把 prompt source switch 的真实缺口收敛在 llm 模块内部：`PromptRegistryConfig` 继续只是 init-time 输入面，`PromptRegistry` 负责保留 previous valid catalog，`LLMManager` 只负责把 ConfigCenter 已投影好的 `prompt_asset_sources` 透传进 prompt pipeline。坏 snapshot 的失败信息继续显式上抛，但不会清空已发布 catalog。
6. blocker 结论：032 无新的前置 BLOCK 任务，但在真实 smoke 基座下暴露了一个必须先修的生产接缝：尽管 `PromptAssetRepository` 已支持 baseline / deployment / snapshot overlay，`PromptRegistryConfig` 与 `LLMManager` 之前只传 baseline root，导致 deployment / snapshot 无法进入真实 manager 主链。该接缝已在本轮最小修复并由 unit/integration 一并固定。
7. 设计收敛：032 采用“真实 prompt pipeline + 单一路由 provider catalog + 临时 prompt package overlay”的最小路径。baseline、deployment、snapshot 三层 prompt package 都在测试内动态生成，并通过 adapter 入参中的 `prompt_version` 与 composed `messages` 断言实际命中的 source；reload 负例则先在 snapshot 路径命中 `2026.04.13`，再将 snapshot manifest 损坏，验证 `PromptRegistry::init()` 返回失败但 manager 继续使用上一份有效 catalog。为避免额外 metrics 依赖引入编译噪声，032 在 integration 文件内本地定义 `make_registration(...)` helper，而没有直接引入通用 support header。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-033`，验证 persona 选择 integration；032 已经把 prompt source chain 的生产投影与 reload failure retain 行为固定下来，后续 persona / governance / profile 测试可继续复用同一真实 prompt/manager integration 基座。

### 17.16 LLM-TODO-033

1. 状态：Done；033 已新增 [tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp](../../../tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp)，在 029/032 的真实 prompt/manager integration 基座上补齐 scene/persona 变体选择证据，并通过 [tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt) 将新用例接入 llm integration discoverability。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-033-persona-selection-integration设计收敛.md](deliverables/LLM-TODO-033-persona-selection-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp](../../../tests/integration/llm/LLMPersonaSelectionIntegrationTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_persona_selection_integration_test` 与 `dasall_integration_tests`；`ListTests_CMakeTools` 已列出 `LLMPersonaSelectionIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_llm_persona_selection_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMPersonaSelectionIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 时，integration 聚合链路中的 40 条用例全部通过，其中 `LLMPersonaSelectionIntegrationTest` 作为第 40 个 integration 用例通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：033 没有改写 `PromptRegistry`、`PromptPipeline`、`LLMManager` 或 shared contracts，而是验证现有 scene/persona 选择链已经进入真实 manager hot path：`PromptRegistryResult.selection_reason` 固定选择路径，`response.prompt_id` / `prompt_version` 固定最终 release 身份，adapter 入参中的 composed `messages` 固定实际下发的 persona prompt 内容。
6. blocker 结论：033 无新的前置 BLOCK 任务；研究确认 013、015、029 已足以支撑 persona integration，scene/persona 选择所需的 `PromptQuery`、`PromptRegistry`、`PromptPipeline` 与 `LLMManager` 投影链都已在生产代码中落盘，因此本轮不需要额外生产修补。
7. 设计收敛：033 采用“动态 prompt package 变体 + 单一路由 provider catalog + 真实 manager dispatch”的最小路径。测试内生成 `general-planner-default`、`operator-planner`、`general-explainer` 与 `cloud-profile` 四个 prompt release 变体，分别覆盖 `scene_persona_selector` 精确命中、同一 stage 下的 persona 变体命中、scene/persona miss 后回落到 `profile_selector`，以及 profile 也 miss 后回落到 `default_release`。每条用例都先直接查询真实 `PromptRegistry` 固定 `selection_reason` / `selected_version`，再执行 `LLMManager.generate()` 校验 `prompt_version` 和 composed `messages`。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-034`，验证治理失败 integration；033 已经把 scene/persona 选择证据固定下来，后续 governance/profile 差异验证可继续复用同一真实 prompt/manager integration 基座。

### 17.17 LLM-TODO-034

1. 状态：Done；034 已新增 [tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp](../../../tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp)，在 029/033 的真实 prompt/manager integration 基座上补齐 allowlist deny、trusted source reject 与 over-budget 三条治理失败路径，并通过 [tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt) 将新用例接入 llm integration discoverability。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-034-governance-failure-integration设计收敛.md](deliverables/LLM-TODO-034-governance-failure-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp](../../../tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_governance_failure_integration_test` 与 `dasall_integration_tests`；`ListTests_CMakeTools` 已列出 `LLMGovernanceFailureIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_llm_governance_failure_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMGovernanceFailureIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 时，integration 聚合链路中的 41 条用例全部通过，其中 `LLMGovernanceFailureIntegrationTest` 作为第 41 个 integration 用例通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：034 没有改写 `PromptPolicy`、`PromptPipeline`、`PromptRegistry`、`LLMManager` 或 shared contracts，而是验证现有治理失败链已经能在真实 manager 路径中阻断 adapter dispatch。allowlist deny 与 over-budget 当前都映射为 `PromptGovernance / PolicyDenied`；trusted source reject 则由于在 registry 选择阶段 fail-closed、未选中 release，当前映射为 `PromptAsset / ValidationFieldMissing`。三条路径的共同点都是 `response == nullopt`、`attempted_routes` 为空且 adapter 调用计数为 0。
6. blocker 结论：034 无新的前置 BLOCK 任务；研究确认 018、019、029 已足以支撑治理失败 integration，现有生产实现已经覆盖三条失败路径，缺失的只是 integration 证据而不是治理逻辑本身。
7. 设计收敛：034 采用“动态 prompt package + 单一路由 provider catalog + 真实 manager failure mapping”的最小路径。allowlist deny 用例先让 prompt selection 成功，再通过 `allowed_prompt_releases = canary` 触发 `prompt_release_not_allowed`；trusted-source 用例通过不受信任的 `trusted_source` 触发 `PromptRegistry` 的 `trusted_source_rejected`；over-budget 用例则用超长 prompt 内容和极小 `runtime_budget.max_tokens` 触发 `render_budget_exceeded`。每条用例都断言失败码、失败分类、`error.details.stage == llm.manager.generate` 和 adapter 调用计数为 0。
8. 后继任务：按专项 TODO 的阶段 H 顺序，下一步可继续推进 `LLM-TODO-035`，验证 profile 差异 integration；034 已经把治理失败收口证据固定下来，后续 profile 差异验证可继续复用同一真实 prompt/manager integration 基座。

### 17.18 LLM-TODO-035

1. 状态：Done；035 已新增 [tests/integration/llm/LLMProfileIntegrationTest.cpp](../../../tests/integration/llm/LLMProfileIntegrationTest.cpp)，在真实 `RuntimePolicySnapshot -> project_llm_subsystem_config(...) -> PromptPipeline + LLMManager` 闭环中补齐 route、prompt allowlist 与 timeout 三类 profile 差异证据，并通过 [tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt) 将新用例接入 llm integration discoverability。
2. 设计交付：[docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md](deliverables/LLM-TODO-035-profile-diff-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)。
3. 代码交付：[tests/integration/llm/LLMProfileIntegrationTest.cpp](../../../tests/integration/llm/LLMProfileIntegrationTest.cpp)、[tests/integration/llm/CMakeLists.txt](../../../tests/integration/llm/CMakeLists.txt)。
4. 验证结果：`ListBuildTargets_CMakeTools` 已列出 `dasall_llm_profile_integration_test`、`dasall_integration_tests` 与 `dasall_contract_tests`；`ListTests_CMakeTools` 已列出 `LLMProfileIntegrationTest`。`Build_CMakeTools` 定向构建 `dasall_llm_profile_integration_test` 成功，随后 `RunCtest_CMakeTools` 定向执行 `LLMProfileIntegrationTest` 结果为 `100% tests passed, 0 tests failed out of 1`。进一步构建 `dasall_integration_tests` 与 `dasall_contract_tests` 时，integration 聚合链路中的 42 条用例全部通过，其中 `LLMProfileIntegrationTest` 作为第 42 个 integration 用例通过；contract 聚合链路中的 152 条用例也全部通过。若 CTest 继续附带 `DartConfiguration.tcl` 缺失提示，仍按既有结论记为工具噪声而非 blocker。
5. 边界结论：035 没有改写 `RuntimePolicySnapshot`、`LLMSubsystemConfig` projector、`PromptPipeline`、`LLMManager` 或 shared contracts，而是验证现有 profile 投影链已经进入真实 manager hot path。route、allowed_prompt_releases、trusted_sources 与 timeout 都来自 `project_llm_subsystem_config(...)`，测试本身只负责把不同 profile snapshot 投影进同一 llm integration 基座，而不是再发明一套平行 llm config 系统。
6. blocker 结论：035 无新的前置 BLOCK 任务；研究过程中确认 012、020、029 已足以支撑 profile integration，但暴露了一个必须写入证据的 stage 接缝：projection unit fixture 中常见的 `planner/responder` stage key 与真实 `PromptRegistry` 接受的 `planning/execution/reflection/response` 不同。本轮通过在 integration snapshot 中直接使用真实 llm stage 名称 `planning` 收口该接缝，并保持 projector 生产逻辑不变。
7. 设计收敛：035 采用“真实 profile snapshot 投影 + 动态 prompt package + 多 route adapter fixture”的最小路径。`cloud_full` 与 `edge_minimal` 共同验证主 route 与 prompt allowlist 差异：前者命中 `deepseek-prod/deepseek-chat` 并允许 canary release，后者命中 `local-runtime/local-small` 且在相同 canary release 下返回 `PromptGovernance / PolicyDenied` 并阻断 adapter dispatch。`desktop_full` 与 `edge_balanced` 则进一步固定 timeout 投影差异，分别向 cloud 与 LAN adapter 传入不同的 `timeout_ms`。整组用例共同证明 llm profile 差异来自 `RuntimePolicySnapshot` 投影视图，而不是测试内手写平行配置。
8. 后继任务：030~035 已全部闭环，下一步可按专项 TODO 继续推进 `LLM-TODO-042` 的 asset-only Provider onboarding integration，或进入 `LLM-TODO-038` 统一回写 llm 专项 Gate 与阶段 H 证据链。
