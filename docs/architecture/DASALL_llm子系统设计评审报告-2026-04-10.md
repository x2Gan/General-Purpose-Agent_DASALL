# DASALL LLM 子系统设计评审报告

版本：v1.0
评审日期：2026-04-10
评审对象：docs/architecture/DASALL_llm子系统详细设计.md v1.0
评审角色：架构评审委员会

---

## 1. 架构覆盖（Step 1）

### 1.1 覆盖状态

| 架构职责（源自 DASSALL_Agent_architecture.md 5.4） | 设计覆盖 | 证据 | 覆盖质量 |
|---|---|---|---|
| 统一模型调用抽象（5.4.1） | ✅ 覆盖 | §6.5.1 ILLMAdapter、§6.5.2 ILLMManager | 接口定义明确，init/generate/stream_generate/health_check 四入口齐全 |
| Cloud→LAN→Local 路由与降级（5.4.2） | ✅ 覆盖 | §6.2 ModelRouter、§6.15.1 组件收敛、§6.10 配置策略 | 路由算法四步固定顺序、fallback chain 受 degrade_policy 约束 |
| 输出语义归一化（5.4.3） | ✅ 覆盖 | §6.15.4 ResponseNormalizer、§6.9 异常语义 | Direct/ToolCall/Clarification/Replan/Refusal 五类语义完整 |
| Model Route 表达（5.4.4） | ✅ 覆盖 | §6.4.2 ResolvedModelRoute、§7.1 LLM-D3 | module-local 实现，不冻结 shared object |
| Prompt 资产化（5.4.5） | ✅ 覆盖 | §6.6.1 Prompt 包形态、§6.6.2 免重编译迭代 | manifest+Markdown 外部包，三层装载优先级 |
| Prompt 选择与装配（5.4.6） | ✅ 覆盖 | §6.5.3 IPromptRegistry、§6.5.4 IPromptComposer | 选择与装配职责清晰分离 |
| Prompt 三段治理（5.4.7） | ✅ 覆盖 | §6.5.3-6.5.5、§6.15.3 PromptPolicy 收敛 | Registry/Composer/Policy 完整 |
| ContextOrchestrator 边界（ADR-006） | ✅ 覆盖 | §1.1 非职责声明、§2.1 LLM-C005/C006 | 明确不检索/压缩 memory |
| Tool Policy Gate 边界（ADR-007） | ✅ 覆盖 | §2.1 LLM-C007、§6.15.3 禁止事项 | 只治理 visible tools 描述，不授予执行权 |
| Recovery 边界（ADR-007/008） | ✅ 覆盖 | §1.1 非职责、§6.9.2 恢复策略 | 失败分类回传 Runtime，不持恢复裁定权 |
| Provider 资产化（v1 新增） | ✅ 覆盖 | §6.6.4-6.6.5 Provider Catalog | 配置式/代码式接入边界清晰 |
| 模型挡位抽象（v1 新增） | ✅ 覆盖 | §6.6.6-6.6.8 双模式建模 | tier_family/latency_tier/cost_tier/reasoning_depth_tier |
| 可观测性 | ✅ 覆盖 | §6.12 日志/指标/Trace/Audit | 17 个最小日志字段，12 个建议指标 |

### 1.2 缺失职责

| ID | 缺失职责 | 影响 | 优先级 |
|---|---|---|---|
| GAP-1 | **Token 预估器设计缺失**：§6.6.8 提到需要 tokenizer 或等价估算器做预调用预算检查，但设计中未给出 TokenEstimator 组件定位、接口或与 ModelRouter/PromptPolicy 的协作方式 | 预调用预算检查无法闭环，over-budget 发现可能延迟到 provider 返回后 | P1 |
| GAP-2 | **UsageAggregator 设计缺位**：§12.2 LLM-TODO-013 提到需要成本估算器与 usage 归并器，但在 §6.2 组件清单中无此组件，§6.5 无接口 | 调用后的真实成本回写与 cache 命中计费归并缺少工程落点 | P1 |
| GAP-3 | **Prompt 模板引擎/槽位渲染器缺乏规范**：§6.5.4 IPromptComposer 提到"模板槽位渲染器"依赖，但仅一笔带过，未定义模板语法规范（Mustache/Jinja/自定义）、安全边界（注入防护）和可测试性 | Prompt 装配实现的首要细节缺失，Build 期必须补充 | P1 |

### 1.3 职责重叠

无重叠。PromptPolicy 与 Tool Policy Gate、ContextOrchestrator 的边界在 §1.1、§2.1、§6.15.3 中已显式冻结，不存在模糊地带。

### 1.4 职责漂移

| ID | 漂移风险 | 证据 | 评估 |
|---|---|---|---|
| DRIFT-1 | Runtime 直接编排 Prompt 三段（§6.7.1 时序图步骤 1-6 由 RT 驱动） | 若 Runtime 逐段调用 PromptRegistry→Composer→Policy，则 Runtime 承担了 Prompt 治理编排职责 | **可接受**：Runtime 作为主控者驱动时序是架构既定方案（6.2 确认 Runtime 保持调用主控权），llm 不成为第二主控。但需确保 Runtime 不直接操纵 Prompt 内部状态 |
| DRIFT-2 | Provider Catalog 加载器可能演变为第二配置中心 | §6.15.2 承载了 overlay、merge、truth-source 分类等重规则 | **需监控**：当前设计已声明遵守 ConfigCenter 四层覆盖原则，但 Build 期需防止 Provider Catalog 加载器发展出独立于 infra/config 的覆盖体系 |

---

## 2. 一致性检查（Step 2）

### 2.1 逐项判定表

| 检查项 | 核心问题 | 结果 | 证据 | 备注 |
|---|---|---|---|---|
| 子系统 → 组件映射 | 是否有职责未落地 | **通过** | §6.2 组件清单 10 个组件覆盖 5 层架构；§6.5 定义 5 个公共接口 | TokenEstimator/UsageAggregator 未列入组件清单（见 GAP-1/2），但属于 P1 补充项 |
| 组件 → TODO 映射 | 是否存在设计未实现 | **有条件通过** | §7.1 LLM-D1~D10 覆盖 10 个落地项；§12.2 LLM-TODO-001~014 覆盖 14 个任务 | LLM-D4 同时覆盖 PromptAssetRepository 与 ProviderCatalogRepository，建议拆分为独立 Design ID |
| TODO → Design 追溯 | 是否存在野任务 | **通过** | 每个 LLM-TODO 均可回溯到 §6 的一个或多个组件与 §7.1 的 Design ID | 无野任务 |
| 命名一致性 | 名词/接口/模块命名是否统一 | **有条件通过** | ILLMAdapter / ILLMManager / IPromptRegistry / IPromptComposer / IPromptPolicy 命名与架构文档一致 | **问题**：§6.2 组件清单中的 "CallExecutor" 在 §6.5 无对应公共接口定义，也无 ICallExecutor；§6.7.1 时序图中未出现 CallExecutor 角色。需明确其是 LLMManager 内部实现还是独立组件 |
| Design → Build 映射 | 是否有实现与测试入口 | **通过** | §7.1 每个 Design ID 均包含代码目标、测试目标和验收命令 | 验收命令可直接执行 |

### 2.2 缺失设计

| ID | 缺失项 | 原因 | 建议 |
|---|---|---|---|
| MISS-1 | CallExecutor 的接口定义与测试面 | §6.2 列为独立组件但 §6.5/§6.15 无收敛 | 若为 LLMManager 内部 helper 则降级为实现细节；若为独立组件则需补接口 |
| MISS-2 | ObservabilityBridge 的接口定义 | §6.2 提到但 §6.5 无定义 | 可保持为内部实现，但应在 LLM-D9 明确其耦合面 |
| MISS-3 | Prompt 模板语法与安全边界规范 | §6.5.4 仅提"模板槽位渲染器" | 需补充模板语法选型、注入防护规则和测试基线 |

### 2.3 多余 TODO

无发现。所有 14 个 LLM-TODO 均有设计依据。

### 2.4 不可实现设计

| ID | 设计项 | 阻塞前提 | 当前影响 |
|---|---|---|---|
| BLOCK-1 | streaming（LLM-D10） | StreamHandle 共享对象未冻结 | 已在 §11.1 LLM-BLK-004 识别，回退策略合理 |
| BLOCK-2 | Cloud/LAN adapter 真实调用 | infra secret/endpoint 注入未接线 | 已在 §11.1 LLM-BLK-006 识别，可用 mock 先行 |

---

## 3. 子系统评审（Step 3）

### 3.1 结构问题清单

| ID | 等级 | 类别 | 问题描述 | 证据 | 影响 | 修复建议 | Owner |
|---|---|---|---|---|---|---|---|
| P0-1 | P0 | 架构边界 | **Runtime 编排 Prompt 三段的控制流未冻结为显式协议**：§6.7.1 时序图显示 Runtime 分步调用 PromptRegistry→Composer→Policy→LLMManager，但 Runtime 对 llm 的公共调用面未形成统一 facade，需要 Runtime 了解 4 个独立接口的调用顺序与错误处理 | §6.7.1 步骤 1-6 | Runtime 侧需要硬编码 llm 内部编排逻辑；若后续 Prompt 治理链扩展（如新增 preprocess 步骤），Runtime 必须同步修改 | **建议提供 PromptPipeline facade 或在 LLMManager.generate() 内收编 Prompt 三段为可选内嵌模式**，让 Runtime 可以选择"一步调用"还是"分步编排"。若前者不可行，至少在 llm 公共接口层定义 PromptPipelineRunner helper | llm 设计者 |
| P1-1 | P1 | 分层 | **Provider Catalog 加载器与 infra/ConfigCenter 的配置合并语义存在平行风险**：§6.15.2 定义了独立的 baseline→deployment→snapshot 三层合并规则，而 infra ConfigCenter 已有 defaults→profile→deployment_override→runtime_override 四层 | §6.15.2 vs DASALL_infrastructure子系统详细设计.md ConfigCenter | 两套 overlay 规则若不统一，Build 期可能出现 ConfigCenter 和 Provider Catalog 各自覆盖、互相不感知的局面 | **Provider Catalog 的装载层级应显式映射到 ConfigCenter 四层模型**，避免造出平行配置管道 | llm + infra |
| P1-2 | P1 | 依赖方向 | **PromptComposeRequest 同时携带 context_packet_id 和 model_route**：contracts 中 PromptComposeRequest.model_route 意味着装配阶段已需要 route 信息，但 §6.7.1 时序图中 ModelRouter 在 LLMManager 内部才被调用（步骤 8），晚于 PromptComposer（步骤 3） | contracts/include/prompt/PromptComposeRequest.h: model_route field | model_route 在装配阶段可能尚未确定，导致 PromptComposer 无法使用该字段做模型感知装配 | **明确 model_route 在 compose 阶段是 optional 提示而非必需依赖**；或调整时序让 ModelRouter 在 compose 前运行 | llm 设计者 |
| P2-1 | P2 | 复杂度 | **组件数量偏多**：§6.2 列出 10 个组件 + §6.4.2 列出 11 个 module-local types，对一个当前代码为零的模块而言初始实现面较大 | §6.2、§6.4.2 | Build 阶段接口面积大，可能导致首轮交付周期延长 | 可考虑首轮合并 CallExecutor 到 LLMManager、合并 HealthAggregator 到 AdapterRegistry，降低初始组件数 | llm 实施者 |

### 3.2 SRP 与边界定义评估

| 组件 | SRP 合规 | 评估 |
|---|---|---|
| LLMManager | ✅ | 编排组件，不拥有策略或 provider 知识 |
| ModelRouter | ✅ | 唯一路由 owner，§6.15.1 权责边界清晰 |
| PromptRegistry | ✅ | 选择 owner，不做装配 |
| PromptComposer | ✅ | 装配 owner，不检索 memory |
| PromptPolicy | ✅ | 治理 owner，不授权工具 |
| PromptAssetRepository | ✅ | 资产装载 owner，不做选择 |
| AdapterRegistry | ✅ | 生命周期管理，不做策略 |
| CallExecutor | ⚠️ | 职责与 LLMManager 部分重叠（计时、取消、错误分类均可由 LLMManager 直接承担） |
| ResponseNormalizer | ✅ | 唯一归一化收口 |
| HealthAggregator | ⚠️ | 可合并到 AdapterRegistry 降低初始复杂度 |

### 3.3 依赖方向与循环依赖

无循环依赖风险。依赖方向严格单向：
```
Runtime → llm public interface → Prompt Governance → Invocation Control → Provider Adapters → Observability
```

infra 和 profiles 只作为配置/能力输入，不存在回调。

### 3.4 错误传播与降级路径

**评估良好**：
- §6.9.1 定义了 6 类失败分类，retryable 与 failover 标记清晰
- §6.9.2 恢复策略原则 5 条均可验证
- §6.7.2 Over-Budget 与 Failover 时序完整

**不足**：
- 缺少 PromptAssetRepository 装载失败到 LLMManager 的完整传播路径描述（§6.9.1 只有 PromptAsset 分类，但如何从 Repository 传播到 Manager 未详述）

---

## 4. 组件评审（Step 4）

### 4.1 接口抽象与契约

| 组件 | 接口完整性 | 输入约束 | 输出约束 | 异常契约 | 评估 |
|---|---|---|---|---|---|
| ILLMAdapter | ✅ 4 入口 | LLMAdapterConfig、LLMRequest | LLMResponse、StreamSessionRef、HealthStatus | 未定义（隐式异常或返回值？） | **缺陷**：§6.5.1 未声明 generate() 是通过返回值传错还是通过异常传错；C++ 中必须冻结此约定 |
| ILLMManager | ✅ 3 入口 | LLMSubsystemConfig、LLMGenerateRequest | LLMManagerResult、HealthStatus | 通过 LLMManagerResult.code 传错 | ✅ |
| IPromptRegistry | ✅ 2 入口 | PromptRegistryConfig、PromptQuery | PromptRegistryResult | 未定义 PromptRegistryResult 结构 | **缺陷**：PromptRegistryResult 未在 §6.4.2/§6.4.3 给出定义 |
| IPromptComposer | ✅ 2 入口 | PromptComposerConfig、contracts 对象 | PromptComposeResult | 通过 contracts PromptComposeResult.composition_warnings | ✅ |
| IPromptPolicy | ✅ 2 入口 | PromptPolicyConfig、contracts 对象 + module-local | PromptPolicyDecision | 通过 PromptPolicyDecision.disposition | ✅ |

### 4.2 组件级缺陷列表

| ID | 等级 | 组件 | 问题描述 | 证据 | 修复建议 |
|---|---|---|---|---|---|
| COMP-1 | P1 | ILLMAdapter | **generate() 错误传播方式未冻结**：是返回 error response、抛异常还是返回 std::expected？C++ 中必须在接口层冻结 | §6.5.1 代码示例返回 LLMResponse 但 LLMResponse 无 ErrorInfo 字段（contracts §禁止错误所有权字段） | 建议 generate() 返回 `AdapterCallResult`（已在 §6.4.2 列出但未体现在接口签名中），或改为返回 `std::expected<LLMResponse, AdapterError>` |
| COMP-2 | P1 | IPromptRegistry | **PromptRegistryResult 类型定义缺失**：§6.5.3 接口返回 PromptRegistryResult 但 §6.4 中无此类型定义 | §6.5.3 select() 返回类型 | 在 §6.4.2 或 §6.4.3 补充 PromptRegistryResult 定义 |
| COMP-3 | P2 | ILLMAdapter | **LLMAdapterConfig 类型未定义**：init() 参数类型无结构定义 | §6.5.1 init(const LLMAdapterConfig& config) | 在 module-local types 中补充 |
| COMP-4 | P2 | IPromptPolicy | **PromptPolicyInput 类型未定义**：evaluate() 参数类型无结构定义 | §6.5.5 evaluate(..., const PromptPolicyInput& input) | 在 module-local types 中补充 |
| COMP-5 | P2 | IPromptComposer | **compose() 缺少 model context window 输入**：Composer 需要知道目标模型的上下文窗口才能计算 estimated_tokens 和 over-budget warning，但接口只接收 PromptComposeRequest 和 PromptRelease，无模型元数据 | §6.5.4 compose 签名 | 补充 context window 或 model metadata 输入参数 |

### 4.3 生命周期与状态机

设计中各组件采用 init() → 运行态 → 析构的简单生命周期，无复杂状态机。这对首轮 Build 是合理的。

唯一潜在状态机在 StreamSessionRef（§6.4.2），但已被正确延后到 Phase 2。

### 4.4 内存安全与所有权

| 要点 | 设计覆盖 | 评估 |
|---|---|---|
| 智能指针 | 未显式声明 | **建议**：公共接口层 adapter 持有方式建议 `std::unique_ptr<ILLMAdapter>` |
| RAII | 未显式声明 | **建议**：adapter 资源（连接池、会话）应遵循 RAII |
| 所有权传递 | PromptComposeRequest/Result 通过 const ref | ✅ 安全 |

### 4.5 可测试性缺口

| 组件 | Mock 点 | 依赖注入 | 评估 |
|---|---|---|---|
| LLMManager | ✅ 可注入 ModelRouter、AdapterRegistry | ✅ 通过 init config | 可测 |
| ModelRouter | ✅ 可注入 HealthAggregator、profile view | ✅ | 可测 |
| PromptRegistry | ✅ 可注入 PromptAssetRepository | ✅ | 可测 |
| PromptComposer | ⚠️ 模板渲染器依赖未被抽象为可注入接口 | 未定义 | **需补充渲染器接口以支持 mock** |
| PromptPolicy | ✅ 可注入 policy config | ✅ | 可测 |
| ResponseNormalizer | ✅ 纯函数式 | ✅ | 可测 |
| Cloud/LAN/Local Adapter | ⚠️ 网络层未被抽象为可注入 transport | 未定义 | **需 HTTP client 接口抽象以支持单测 mock** |

---

## 5. 并发模型（Step 5）

### 5.1 并发模型评估

§6.11 并发策略定义了四项资源治理：

| 资源点 | 策略 | 评估 |
|---|---|---|
| active llm calls | bounded semaphore + reject | ✅ 合理 |
| adapter health cache | lock-free read or short L1 lock | ✅ 合理 |
| stream session table | bounded + reject new session | ✅ 延后 |
| metrics/trace emit | fire-and-forget | ✅ 合理 |

### 5.2 并发风险点

| ID | 等级 | 风险描述 | 证据 | 缓解建议 |
|---|---|---|---|---|
| CONC-1 | P1 | **PromptAssetRepository catalog 刷新并发**：§6.6.2 提到 runtime 只刷新 catalog，但未描述 catalog snapshot 的读写并发模型。若刷新同时有 select() 在读旧 catalog，需要 immutable snapshot 或 RCU 语义 | §6.6.2 "运行时应只刷新 Prompt catalog" | 冻结为 immutable snapshot + atomic pointer swap，确保读者拿到的是一致快照 |
| CONC-2 | P1 | **Provider Catalog 刷新并发**：与 CONC-1 同理，§6.15.2 声明 immutable snapshot 但未描述 swap 机制 | §6.15.2 "生成 immutable catalog snapshot" | 同上，需 atomic swap |
| CONC-3 | P2 | **HealthAggregator 更新与 ModelRouter 读取竞态**：ModelRouter 读取 health snapshot 与 adapter health probe 回写之间需要同步 | §6.15.1 "health snapshot" | 通过 copy-on-write 或定时 snapshot 避免热路径上的锁 |
| CONC-4 | P2 | **bounded semaphore 的 inflight 计数实现未指定**：§6.11 声明 bounded semaphore 但未说明是 std::counting_semaphore、条件变量还是 infra 提供的并发原语 | §6.11 资源治理表 | Build 期选定具体实现，建议复用 infra 并发原语 |

### 5.3 必须新增的并发测试场景

| 场景 | 验证目标 |
|---|---|
| catalog reload 期间并发 select() | 读者不阻塞、不读到半成品 |
| semaphore 满时新请求到达 | 新请求被立即拒绝而非阻塞 |
| adapter health probe 与 ModelRouter resolve 并发 | health snapshot 一致性 |
| fallback 路径中 primary adapter 超时与 health 更新竞态 | 不因 health 更新导致 fallback 跳过仍可用的 adapter |

---

## 6. 扩展性（Step 6）

### 6.1 扩展能力评分

| 维度 | 评分 | 理由 |
|---|---|---|
| 插件化能力 | 4/5 | ILLMAdapter + AdapterRegistry 支持新 adapter 注册；但 adapter 目前只能编译期注册，无动态加载 |
| 动态扩展能力 | 3/5 | Prompt 资产和 Provider 资产支持外部化和 deployment override；但 adapter family 新增仍需重编译 |
| 多模型/多 Provider 支持 | 5/5 | Provider Catalog + ModelCatalogEntry + tier traits 设计完整，配置式接入路径清晰 |
| 版本兼容与迁移策略 | 4/5 | module-local types 不影响 shared contracts；Prompt 包 manifest 含 version/scope；但 Provider Catalog schema 迁移策略未显式定义 |

**总体评分：4/5**

### 6.2 扩展性建议

| ID | 建议 | 优先级 |
|---|---|---|
| EXT-1 | Provider Catalog schema 应定义 version 字段和向前兼容规则 | P2 |
| EXT-2 | Prompt 包 manifest schema 应定义 min_loader_version 避免旧加载器误读新格式 | P2 |

---

## 7. TODO 评审（Step 7）

### 7.1 TODO 合格判定

| TODO ID | 原子性 | 代码目标 | 测试目标 | 验收命令 | 判定 | 缺陷说明 |
|---|---|---|---|---|---|---|
| LLM-TODO-001 | ✅ | ✅ llm/include 首版骨架 | ⚠️ 未明确测试文件名 | ⚠️ 未给出 | **不合格** | 缺测试目标文件名和验收命令 |
| LLM-TODO-002 | ⚠️ 粒度过大 | ✅ PromptAssetRepository + ProviderCatalogRepository + 本地资产 | ⚠️ 未明确测试文件名 | ⚠️ 未给出 | **不合格** | 任务过大，应拆为至少 3 个原子任务；缺验收命令 |
| LLM-TODO-003 | ✅ | ✅ PromptRegistry | ⚠️ 未明确测试文件名 | ⚠️ 未给出 | **不合格** | 缺测试目标文件名和验收命令 |
| LLM-TODO-004 | ✅ | ✅ PromptComposer | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 缺测试目标和验收命令 |
| LLM-TODO-005 | ✅ | ✅ PromptPolicy | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 同上 |
| LLM-TODO-006 | ✅ | ✅ ModelRouter | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 同上 |
| LLM-TODO-007 | ⚠️ 粒度偏大 | ✅ LLMManager + 成本回写 + reasoning 剥离 | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 应拆分 LLMManager 骨架和 reasoning 剥离为独立任务 |
| LLM-TODO-008 | ✅ | ✅ adapter skeleton | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 缺测试目标和验收命令 |
| LLM-TODO-009 | ⚠️ 粒度过大 | ✅ 多个 integration tests | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 应拆分为独立 integration test 任务 |
| LLM-TODO-010 | ✅ | ✅ streaming | ✅ | ⚠️ 未给出 | **不合格** | 缺验收命令 |
| LLM-TODO-011 | ✅ | N/A（评审任务） | N/A | N/A | **合格** | 评审任务无需三件套 |
| LLM-TODO-012 | ✅ | N/A（评审任务） | N/A | N/A | **合格** | 同上 |
| LLM-TODO-013 | ✅ | ✅ | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 缺测试目标和验收命令 |
| LLM-TODO-014 | ✅ | ✅ | ⚠️ 未明确 | ⚠️ 未给出 | **不合格** | 同上 |

**三件套完整率：2/14 = 14.3%（远低于 95% 基线）**

**根因分析**：§12.2 的 LLM-TODO 列表是概要级建议，不是可执行 TODO。真正的三件套信息分散在 §7.1 Design→Build 映射表（LLM-D1~D10）中。两套编号体系存在映射模糊。

**改进方向**：LLM-TODO 体系应基于 §7.1 的 LLM-D 映射表重写，使每个 TODO 直接继承对应 Design ID 的代码目标、测试目标和验收命令。

### 7.2 缺失 TODO

| ID | 缺失任务 | 设计依据 | 建议 |
|---|---|---|---|
| MISS-TODO-1 | **Prompt 模板引擎选型与安全规范** | §6.5.4 "模板槽位渲染器" | 新增 TODO：选定模板引擎、定义安全边界、补充注入防护测试 |
| MISS-TODO-2 | **tests/unit/llm CMakeLists.txt 激活与 mock 升级** | §8.1、§3.1 MockLLMAdapter 状态 | 新增 TODO：替换占位 CMakeLists.txt、升级 MockLLMAdapter 为生产接口 mock |
| MISS-TODO-3 | **tests/integration/llm 目录创建与 CMake 注册** | §11.1 LLM-BLK-005 | 新增 TODO：创建目录结构并注册到 tests/CMakeLists.txt |
| MISS-TODO-4 | **TokenEstimator 组件落地** | GAP-1 | 新增 TODO |
| MISS-TODO-5 | **Provider Catalog schema version 与兼容规则** | EXT-1 | 新增 TODO |

### 7.3 建议新增 TODO（按原子粒度）

见本报告附件：docs/todos/llm/DASALL_llm子系统TODO落地实施步骤指引.md

---

## 8. 执行顺序（Step 8）

### 8.1 Phase 划分

| Phase | 目标 | 输入条件 | 输出/退出条件 | 关键任务 | 并行任务 |
|---|---|---|---|---|---|
| **Phase 0** | 阻塞清理与测试基础设施 | llm/CMakeLists.txt 可编译 placeholder | (1) llm/include 目录存在且空骨架可编译 (2) tests/unit/llm 与 tests/integration/llm 目录创建 (3) MockLLMAdapter 升级为生产接口 mock (4) ctest -N 可发现 llm 占位测试 | T-001 接口骨架、T-002 测试基础设施、T-003 Mock 升级 | T-001 ∥ T-002 ∥ T-003 |
| **Phase 1** | Prompt 资产与 Provider 资产 | Phase 0 退出 | (1) Prompt 包解析通过单测 (2) Provider Catalog 解析通过单测 (3) PromptRegistry 选择通过单测 (4) assets/ 基线目录存在 | T-004 Prompt 包规范与解析器、T-005 Provider Catalog 规范与解析器、T-006 PromptRegistry、T-007 模板引擎选型 | T-004 ∥ T-005 → T-006；T-007 独立 |
| **Phase 2** | Prompt 治理核心 | Phase 1 退出 | (1) PromptComposer 装配通过单测 (2) PromptPolicy 治理通过单测 (3) over-budget 路径可验证 | T-008 PromptComposer、T-009 PromptPolicy、T-010 LLMSubsystemConfig | T-008 → T-009；T-010 独立 |
| **Phase 3** | 路由与调用核心 | Phase 2 退出 | (1) ModelRouter 路由通过单测（含双模式选择）(2) LLMManager unary 主路径通过单测 (3) ResponseNormalizer 通过单测 (4) Fallback 路径可验证 | T-011 ModelRouter、T-012 LLMManager + CallExecutor、T-013 ResponseNormalizer | T-011 → T-012；T-013 ∥ T-011 |
| **Phase 4** | Provider 适配与可观测 | Phase 3 退出 | (1) 至少 1 个 adapter skeleton 可编译 (2) health probe 通过单测 (3) observability bridge 接线完整 | T-014 adapter skeleton、T-015 observability bridge | T-014 ∥ T-015 |
| **Phase 5** | 集成与质量门 | Phase 4 退出 | (1) smoke integration 通过 (2) failure injection 通过 (3) prompt source switch 通过 (4) ctest -N 可发现所有 llm integration tests | T-016~T-021 各集成测试 | T-016~T-021 可并行 |
| **Phase 6（可选）** | Streaming | unary 稳定 + StreamHandle 设计完成 | stream 单测和取消测试通过 | T-022 streaming | 独立 |

### 8.2 依赖链与关键路径

```
Phase 0: T-001(接口) ─┬─→ Phase 1: T-004(Prompt资产) ─┬─→ T-006(Registry) ──→ Phase 2: T-008(Composer) ──→ T-009(Policy)
         T-002(测试)  ─┘           T-005(Provider资产)─┘                                                         │
         T-003(Mock)  ─────────────────────────────────────────────────────────────────────────────────────────────┘
                                                                                                                   │
         T-010(Config) ──────────────────────────────────────────────────→ Phase 3: T-011(Router) ──→ T-012(Manager)
                                                                                    T-013(Normalizer) ─┘
                                                                                                       │
                                                                          Phase 4: T-014(Adapters)  ───┘
                                                                                   T-015(Observability)─┘
                                                                                                        │
                                                                          Phase 5: T-016~T-021(Integration)
```

**Critical Path**: T-001 → T-004/T-005 → T-006 → T-008 → T-009 → T-011 → T-012 → T-014 → T-016

**Parallel Lanes**:
- Lane A (Prompt 资产): T-004 → T-006 → T-008 → T-009
- Lane B (Provider 资产): T-005 → (merge at T-006)
- Lane C (配置): T-010 → (merge at T-011)
- Lane D (归一化): T-013 (parallel with T-011)
- Lane E (可观测): T-015 (parallel with T-014)

### 8.3 每个 Phase 退出条件

| Phase | 退出条件 | 验证命令 |
|---|---|---|
| 0 | ctest -N 可发现 ≥2 个 llm 单测；dasall_llm 编译不只依赖 placeholder | `cmake --build build-ci --target dasall_llm && ctest --test-dir build-ci -N \| grep -c llm` |
| 1 | Prompt 包解析和 Provider Catalog 解析相关单测全绿 | `ctest --test-dir build-ci -R "(PromptAsset\|ProviderCatalog\|PromptRegistry)" --output-on-failure` |
| 2 | PromptComposer + PromptPolicy 单测全绿 | `ctest --test-dir build-ci -R "(PromptComposer\|PromptPolicy)" --output-on-failure` |
| 3 | ModelRouter + LLMManager + ResponseNormalizer 单测全绿 | `ctest --test-dir build-ci -R "(ModelRouter\|LLMManager\|ResponseNormalizer)" --output-on-failure` |
| 4 | Adapter + Observability 单测全绿 | `ctest --test-dir build-ci -R "(Adapter\|LLMObservability)" --output-on-failure` |
| 5 | 至少 smoke + failure + prompt switch 集成测试全绿 | `ctest --test-dir build-ci -R "LLM.*IntegrationTest" --output-on-failure` |

---

## 9. 风险评估（Step 9）

### 9.1 架构风险

| ID | 风险 | 触发条件 | 影响 | 监控信号 | 缓解措施 |
|---|---|---|---|---|---|
| RISK-A1 | **Prompt/Context 混层** | PromptComposer 实现者主动拉取 memory 数据 | 违反 ADR-006，llm 与 memory 职责纠缠，测试复杂度爆炸 | Code review 中发现 llm 对 memory 的 #include | 在 llm/CMakeLists.txt 中不链接 memory 目标；Code review checklist 增加 ADR-006 检查项 |
| RISK-A2 | **Runtime 对 Prompt 三段编排的深耦合** | Runtime 硬编码调用 PromptRegistry→Composer→Policy 的顺序 | Prompt 治理链扩展时 Runtime 必须同步修改 | Runtime 对 llm 接口的 #include 数量 > 2 | 提供 PromptPipeline facade（见 P0-1 建议） |
| RISK-A3 | **Provider Catalog 演变为第二配置中心** | Provider Catalog 加载器发展出独立于 infra/ConfigCenter 的覆盖体系 | 双配置管道导致覆盖冲突和审计不一致 | Provider Catalog 开始直接读取环境变量或文件系统路径而不经 ConfigCenter | 冻结 Provider Catalog 装载层级到 ConfigCenter 四层模型 |

### 9.2 并发风险

| ID | 风险 | 触发条件 | 影响 | 监控信号 | 缓解措施 |
|---|---|---|---|---|---|
| RISK-C1 | **Catalog 刷新竞态** | PromptAssetRepository 或 Provider Catalog 刷新时 select/resolve 并发读取 | 读到半成品 catalog，导致路由或 Prompt 选择异常 | 偶发选择结果不一致 | immutable snapshot + atomic pointer swap |
| RISK-C2 | **bounded semaphore 过饱和** | 高并发场景下 active llm calls 达到上限 | 新请求全部被拒绝，降级不可用 | reject count 指标突增 | 动态调整 semaphore 上限、或引入排队机制（需 SSOT 声明） |

### 9.3 工程风险

| ID | 风险 | 触发条件 | 影响 | 监控信号 | 缓解措施 |
|---|---|---|---|---|---|
| RISK-E1 | **首轮 Build 面积过大** | 10 组件 + 11 module-local types + Prompt/Provider 两套资产规范同时推进 | 交付周期延长，Build 质量下降 | Phase 1-2 耗时明显超出预估 | 首轮合并 CallExecutor→LLMManager、HealthAggregator→AdapterRegistry，降低初始组件数 |
| RISK-E2 | **TODO 三件套不完整** | §12.2 的 LLM-TODO 缺少测试目标和验收命令 | 实施者无法自行判定完成状态，验收时间浪费在"什么叫做完了"的讨论上 | TODO 验收反复退回 | 基于 §7.1 LLM-D 映射表重写 TODO 体系 |
| RISK-E3 | **provider secret 注入未接线** | Cloud/LAN adapter 实装阶段发现 infra secret 注入路径不通 | adapter 只能 dummy 运行，无法真实调用 | adapter init 阶段 secret resolve 失败 | Phase 4 前确认 infra secret 注入链路可用；否则先用 mock adapter 完成主链路 |

### 9.4 扩展风险

| ID | 风险 | 触发条件 | 影响 | 监控信号 | 缓解措施 |
|---|---|---|---|---|---|
| RISK-X1 | **Provider Catalog schema 不兼容** | models.yaml schema 在后续迭代中不可向前兼容 | 旧 Provider 包无法被新加载器解析 | Provider 包加载失败率上升 | 定义 schema_version 字段与向前兼容规则 |
| RISK-X2 | **tier traits 抽象不足** | 新 Provider 的多档位模型无法映射到现有 tier_family/latency_tier/cost_tier | adapter 或 ModelRouter 出现厂商专有分支 | ModelRouter 中出现 if(provider == "xxx") 硬编码 | 在 Provider Catalog 规范中预留自定义 tag 维度 |

---

## 10. 最终结论（Step 10）

### 结论

**有条件通过**

### 准入门槛判定

| 门槛 | 状态 | 说明 |
|---|---|---|
| 无 P0 未闭环问题 | ⚠️ **1 个 P0 待处理** | P0-1 Runtime 编排 Prompt 三段的控制流未冻结为显式协议 |
| Design → Component → TODO → Build/Test 可追溯 | ✅ 基本满足 | §7.1 LLM-D 映射完整，但 §12.2 LLM-TODO 与 LLM-D 存在双编号体系映射模糊 |
| TODO 三件套完整率 ≥ 95% | ❌ **14.3%** | §12.2 LLM-TODO 列表仅为概要级建议，三件套信息分散在 §7.1 |
| 存在可执行关键路径与验收命令集 | ✅ | §7.1 每个 LLM-D 均有验收命令；§8.2 分阶段计划清晰 |

### 理由

1. **架构质量高（量化）**：13 项架构职责全覆盖，29 条约束明确（Must 14 + Should 3 + Must-Not 3 + 新增 9），5 层分层合理，10 组件边界清晰，4 条 ADR 边界严格遵守。
2. **Design→Build 映射完整（量化）**：§7.1 提供 10 个 Design ID 与代码/测试/命令三件套，可直接进入 Build。
3. **创新价值高**：Provider Catalog 资产化、模型挡位 tier traits 抽象、双模式可解释路由等设计超越常规 LLM SDK wrapper 层级。
4. **TODO 体系需重构（量化）**：§12.2 的 14 个 LLM-TODO 中仅 2 个（评审类）合格，三件套完整率 14.3%，必须基于 §7.1 重写后方可进入 Build 执行。
5. **P0 待闭环**：Runtime 与 llm 之间的 Prompt 编排控制流协议需冻结，否则 Runtime 侧接入成本不可控。

### P0 问题

| ID | 等级 | 问题 | 修复建议 | 闭环条件 |
|---|---|---|---|---|
| P0-1 | P0 | Runtime 编排 Prompt 三段的控制流未冻结为显式协议 | 提供 PromptPipeline facade 或在 ILLMManager 中集成 Prompt 三段调用模式 | llm 对 Runtime 暴露的公共接口数量 ≤ 3 |

### P1 建议

| ID | 等级 | 问题 | 修复建议 |
|---|---|---|---|
| P1-1 | P1 | Provider Catalog 加载器与 ConfigCenter 合并语义平行风险 | 显式映射到 ConfigCenter 四层模型 |
| P1-2 | P1 | PromptComposeRequest.model_route 时序问题 | 明确为 optional 提示而非必需 |
| COMP-1 | P1 | ILLMAdapter.generate() 错误传播方式未冻结 | 选定 AdapterCallResult 或 std::expected |
| COMP-2 | P1 | PromptRegistryResult 类型定义缺失 | 补充到 §6.4 |
| CONC-1 | P1 | PromptAssetRepository catalog 刷新并发 | 冻结为 immutable snapshot + atomic swap |
| CONC-2 | P1 | Provider Catalog 刷新并发 | 同上 |
| GAP-1 | P1 | TokenEstimator 组件设计缺失 | 补充组件定位与接口 |
| GAP-2 | P1 | UsageAggregator 设计缺位 | 补充到组件清单 |
| GAP-3 | P1 | Prompt 模板引擎/槽位渲染器安全规范缺失 | 补充选型与注入防护 |

### P2 优化

| ID | 等级 | 问题 | 修复建议 |
|---|---|---|---|
| P2-1 | P2 | 初始组件数量偏多 | 首轮合并 CallExecutor→LLMManager、HealthAggregator→AdapterRegistry |
| COMP-3 | P2 | LLMAdapterConfig 未定义 | 补充 |
| COMP-4 | P2 | PromptPolicyInput 未定义 | 补充 |
| COMP-5 | P2 | IPromptComposer.compose() 缺少模型上下文窗口输入 | 补充 |
| CONC-3 | P2 | HealthAggregator 更新竞态 | copy-on-write |
| EXT-1 | P2 | Provider Catalog schema 迁移规则 | 定义 version + 兼容规则 |
| EXT-2 | P2 | Prompt 包 manifest 的 min_loader_version | 定义兼容性字段 |

---

## 附录：Design → Build 映射表（完整）

| 设计项 | 组件 | Design ID | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| llm 公共接口与 module-local types | ILLMAdapter, ILLMManager, IPrompt*, supporting types | LLM-D1 | llm/include/*.h | tests/unit/llm/InterfaceSurfaceTest.cpp | `cmake --build build-ci --target dasall_llm && ctest --test-dir build-ci -R LLMInterfaceSurfaceTest --output-on-failure` |
| LLMSubsystemConfig 与 profile 投影 | LLMSubsystemConfig | LLM-D2 | llm/include/LLMSubsystemConfig.h; llm/src/LLMSubsystemConfig.cpp | tests/unit/llm/LLMSubsystemConfigProjectionTest.cpp | `ctest --test-dir build-ci -R LLMSubsystemConfigProjectionTest --output-on-failure` |
| ModelRouter 与 ResolvedModelRoute | ModelRouter | LLM-D3 | llm/include/route/ResolvedModelRoute.h; llm/src/route/ModelRouter.cpp | tests/unit/llm/ModelRouter{Policy,Fallback,ReasoningModeSelection}Test.cpp | `ctest --test-dir build-ci -R "ModelRouter" --output-on-failure` |
| Prompt 资产 + Provider Catalog + Registry | PromptAssetRepository, ProviderCatalogRepository, PromptRegistry | LLM-D4 | llm/src/prompt/PromptAssetRepository.cpp; llm/src/provider/ProviderCatalogRepository.cpp; llm/src/prompt/PromptRegistry.cpp | tests/unit/llm/Prompt{AssetPackageParse,SourceOverlay,RegistrySelection,RegistryTrustSource}Test; Provider{CatalogParse,CatalogOverlay,ModelMetadataParse}Test | `ctest --test-dir build-ci -R "(Prompt\|ProviderCatalog\|ProviderModelMetadata)" --output-on-failure` |
| PromptComposer 与 over-budget | PromptComposer | LLM-D5 | llm/src/prompt/PromptComposer.cpp | tests/unit/llm/PromptComposer{SlotMapping,OverBudget}Test.cpp | `ctest --test-dir build-ci -R "PromptComposer" --output-on-failure` |
| PromptPolicy 发送前治理 | PromptPolicy | LLM-D6 | llm/include/prompt/PromptPolicyDecision.h; llm/src/prompt/PromptPolicy.cpp | tests/unit/llm/PromptPolicy{Allowlist,ToolVisibility}Test.cpp | `ctest --test-dir build-ci -R "PromptPolicy" --output-on-failure` |
| LLMManager + AdapterRegistry + CallExecutor + unary | LLMManager, AdapterRegistry, CallExecutor, ResponseNormalizer | LLM-D7 | llm/src/LLMManager.cpp; llm/src/route/AdapterRegistry.cpp; llm/src/execution/*.cpp | tests/unit/llm/LLMManager{SuccessPath,Fallback,FailureMapping}Test; ResponseNormalizerReasoningContentStripTest | `ctest --test-dir build-ci -R "(LLMManager\|ResponseNormalizer)" --output-on-failure` |
| Cloud/LAN/Local adapter skeleton | OpenAICompatibleAdapter, OllamaAdapter, LocalLLMAdapter | LLM-D8 | llm/src/adapters/*.cpp | tests/unit/llm/Adapter{HealthProbe,ProtocolMapping}Test.cpp | `ctest --test-dir build-ci -R "Adapter" --output-on-failure` |
| Observability + integration | ObservabilityBridge, integration tests | LLM-D9 | llm/src/observability/*; tests/integration/llm/*.cpp | smoke + failure + profile + prompt switch + dual mode | `ctest --test-dir build-ci -R "LLM.*IntegrationTest" --output-on-failure` |
| Streaming 占位 | StreamSessionRef | LLM-D10 | llm/include/stream/StreamSessionRef.h; llm/src/stream/StreamSessionRegistry.cpp | tests/unit/llm/StreamSessionLifecycleTest.cpp | `ctest --test-dir build-ci -R StreamSessionLifecycleTest --output-on-failure` |

---

## 附录：跨步骤必查维度覆盖矩阵

| 维度 | 覆盖步骤 | 具体位置 | 完整性 |
|---|---|---|---|
| 模块接口与契约 | Step 4 (§4.1-4.2) | ILLMAdapter/ILLMManager/IPrompt* 5 个接口 | ✅ 已覆盖，P1 缺陷已记录 |
| 数据模型与序列化 | Step 4 (§4.1) + Step 6 (§6.1) | contracts 对象 + module-local types + Prompt/Provider yaml | ✅ 已覆盖 |
| 生命周期与状态机 | Step 4 (§4.3) | init→运行→析构，streaming 延后 | ✅ 已覆盖 |
| 错误处理与异常策略 | Step 3 (§3.4) + Step 4 (§4.2 COMP-1) | 6 类失败分类 + 恢复策略 | ✅ 已覆盖，COMP-1 待闭环 |
| 日志与审计 | Step 9 (§9.1) | 17 日志字段 + 6 审计事件 | ✅ 已覆盖 |
| 配置管理 | Step 3 (§3.1 P1-1) | ConfigCenter 四层 + Provider Catalog 三层 | ✅ 已覆盖，P1-1 待闭环 |
| 安全 | Step 4 (§4.2) + Step 7 (MISS-TODO-1) | secret ref 分层 + Prompt 模板注入防护 | ⚠️ 模板注入防护需补充 |
| 性能与可观测性 | Step 5 (§5.1) + Step 9 (§9.1) | semaphore + 12 指标 + 6 span | ✅ 已覆盖 |
| 可测试性 | Step 4 (§4.5) + Step 7 (§7.1) | Mock 点 + DI + 三件套 | ✅ 已覆盖，adapter transport mock 待补 |
| 可部署性与平台依赖 | Step 6 (§6.1) | Linux 目标，CMake 静态库 | ✅ 已覆盖 |
| 依赖管理与第三方风险 | Step 6 (§6.2) | 当前无第三方 LLM SDK 依赖，HTTP client 选型待定 | ⚠️ HTTP client 库选型需在 Phase 4 前冻结 |
| 构建与 CI/CD | Step 8 (§8.3) | 验收命令集完整，build-ci 目标 | ✅ 已覆盖 |

---

*评审完成。评审结论：有条件通过。必须在进入 Build 前闭环 P0-1 问题，并基于本报告重构 TODO 体系。*
