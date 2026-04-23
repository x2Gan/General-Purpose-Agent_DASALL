# Cross-Module Data Projection Matrix (Single Source of Truth)

状态：Active
Owner：架构组 + Profiles/Runtime 负责人
关联任务：2026-04-15 全局设计评审收敛项

## 1. 目的

本文件冻结 DASALL 当前最关键的跨模块数据投影规则，解决“上游结构化对象、下游共享 contracts、观测/审计事实”之间语义漂移的问题。首批只覆盖三条必须先闭合的数据线：

1. Knowledge 证据投影
2. ToolResult -> Observation / ObservationDigest
3. Access sidecar -> runtime / infra 审计事实

## 2. 统一规则

1. 每条数据线都必须明确四件事：source of truth、唯一 projection owner、下游可见对象、lossy/lossless 边界。
2. shared contracts 只承载稳定最小视图；module-local 结构化对象继续归源模块所有，不因下游暂时需要而反向推进 contracts。
3. Runtime 负责跨 knowledge/tools/access 与 memory/cognition/infra 的主链转运；若子系统文档显式声明某个 projector 为唯一出口，则 runtime 只消费其产出，不重做语义解释。
4. 任何 consumer 都不得从 lossy projection 逆推出上游结构化字段，更不得把下游补写结果反向视为 source of truth。
5. 若后续需要跨模块共享更丰富的结构化对象，必须采用“新增可选 projection object / supporting contract”的增量方式，不得重解释现有 string 向量或 digest 字段。

## 3. 顶层矩阵

| 数据线 | 上游事实源 | 唯一投影责任 | 下游可见对象 | v1 共享形态 | 禁止事项 |
|---|---|---|---|---|---|
| Knowledge 证据 | Knowledge 的 `KnowledgeRetrieveResult` 中 `EvidenceBundle/EvidenceSlice` | `Knowledge` 负责结构化证据组装，`runtime` 负责跨模块转运到 memory | `MemoryContextRequest.external_evidence`、`ContextPacket.retrieval_evidence`、观测/审计 evidence refs | `std::vector<std::string>` 文本切片 | 不得把 `retrieval_evidence` 反解为结构化证据对象 |
| Tool 执行结果 | `ToolResult` | `tools::ResultProjector` | `Observation`、`ObservationDigest`、audit facts、`ToolInvocationEnvelope` | `Observation` / `ObservationDigest` shared contracts | 不得把 raw payload 直接回灌 prompt、`ContextPacket` 或长期 memory |
| Access sidecar | `SubjectIdentity`、`AccessDecisionProof`、`RuntimeDispatchRequest` | `access::RequestNormalizer` 负责 runtime bridge sidecar，`AccessObservabilityBridge` 负责 audit projection | `AgentRequest`、`RuntimeDispatchRequest`、audit/metrics/tracing facts | 共享面只有 `AgentRequest` / `AgentResult` | 不得把主体属性、认证秘密、完整 proof 塞进 shared contracts |

## 4. Knowledge 证据投影

### 4.1 详细矩阵

| 阶段 | Owner | 对象 / 字段 | 形态 | 投影规则 | 下游消费者 |
|---|---|---|---|---|---|
| 检索完成 | knowledge | `KnowledgeRetrieveResult.context_projection`、`EvidenceBundle`、`EvidenceSlice` | 结构化、lossless、module-local | 作为当前请求内结构化证据 source of truth，保留 `evidence_ref`、`source_type`、`confidence`、freshness 等字段 | runtime |
| runtime -> memory | runtime | `MemoryContextRequest.external_evidence` | `std::vector<std::string>`、lossy | 按 rerank 后顺序，把面向 memory/context 的文本切片投影为字符串；只保留可直接用于上下文装配的摘要文本 | memory `CandidateCollector` |
| memory -> contracts | memory | `ContextPacket.retrieval_evidence` | `std::vector<std::string>`、lossy | 从 `vector_hits + external_evidence` 生成面向 llm 的检索证据文本槽位；该字段只表达“可展示证据摘要”，不是结构化证据事实源 | runtime / llm / cognition |
| 观测与审计 | knowledge + runtime | evidence refs / source ids / freshness facts | 结构化事实、module-local | 结构化引用继续留在知识侧结果或 runtime invoke-scoped 事实集中，供 observability / audit / recovery 使用 | infra、runtime |

### 4.2 v1 冻结规则

1. `external_evidence` 与 `retrieval_evidence` 都是文本投影视图，不承诺保留 `EvidenceSlice` 的全部结构化字段。
2. `EvidenceBundle` 才是知识证据的结构化事实源；如需保留 `evidence_ref`、`source_type`、`confidence`，必须沿 runtime invoke-scoped sidecar 或观测事实传播，不能指望从字符串恢复。
3. `external_evidence` 的单条元素必须是经过裁剪和脱敏的文本切片，不得直接塞入数据库行、原始 JSON、二进制句柄或未经治理的长文。
4. 当后续确需跨模块共享结构化证据时，新增独立 projection object；在那之前，任何模块不得私自把 `std::vector<std::string>` 扩写为自定义结构体 ABI。

## 5. ToolResult -> Observation / ObservationDigest

### 5.1 详细矩阵

| 阶段 | Owner | 对象 / 字段 | 形态 | 投影规则 | 下游消费者 |
|---|---|---|---|---|---|
| 工具执行完成 | tools | `ToolResult` | shared contract、执行面事实源 | 保留 success、payload、error、side_effects、evidence_refs、latency 等原始执行结果 | `ResultProjector`、runtime |
| 结果投影 | tools | `ResultProjector` | 唯一 projector | 把 `ToolResult` 规则化投影为 `Observation`、`ObservationDigest` 与 audit facts；不得依赖 LLM 做摘要 | runtime |
| runtime handoff | tools | `ToolInvocationEnvelope` | module-local | 统一承载 `ToolResult`、`Observation`、`ObservationDigest`、compensation hints、route facts | runtime |
| 上下文 / 推理消费 | runtime + memory | `Observation`、`ObservationDigest` | shared contracts | `ObservationDigest` 进入下一轮上下文/写回主链；`Observation` 可供 runtime/cognition 做更高保真分析；raw `ToolResult` 仅保留在执行/恢复/审计路径 | memory、cognition、llm |
| 观测与恢复 | runtime + infra | side effects / route facts / evidence refs | module-local facts | 进入 audit、metrics、recovery 输入；不得把完整 raw payload 嵌入 digest 或 contracts | infra、runtime |

### 5.2 v1 冻结规则

1. `ToolResult` 是执行面事实源，`Observation` / `ObservationDigest` 是推理面和写回面的唯一合法投影视图。
2. 下一轮 `ContextPacket` 只消费 `ObservationDigest`；若需要更高保真分析，只允许 runtime 在本轮 invoke 内消费 `Observation`，不能直接把 raw `ToolResult.payload` 回灌给 llm/memory。
3. `side_effects`、`evidence_refs`、`route_kind`、`latency_ms` 必须通过 audit facts / compensation hints / recovery inputs 传播，不得塞进 digest 五字段之外的共享面。
4. 若 `ResultProjector` 失败，必须退化为最小 failure digest，而不是泄漏 raw payload。

## 6. Access sidecar -> runtime / audit

### 6.1 详细矩阵

| 阶段 | Owner | 对象 / 字段 | 形态 | 投影规则 | 下游消费者 |
|---|---|---|---|---|---|
| 入口准入 | access | `InboundPacket`、`SubjectIdentity`、`AccessDecisionProof` | module-local、lossless | 在 access core 内完整保留主体、通道、授权证明与入口事实 | `RequestNormalizer` |
| runtime bridge 归一化 | access | `RuntimeDispatchRequest` | module-local | `RequestNormalizer` 是唯一 owner：只把共享最小请求投影为 `AgentRequest`，其余主体/授权/发布上下文继续保留在 sidecar | `RuntimeBridge`、runtime |
| runtime invoke handoff | access | `RuntimeInvokeContext` | bridge-local、module-local | `RuntimeBridge` 是唯一 owner：把 `RuntimeDispatchRequest` 压缩为 invoke 期最小事实后，再对接 runtime public seam 或 bridge-local adapter；不要求 runtime 暴露新的 access sidecar public type | runtime adapter / runtime |
| shared 主链 | contracts | `AgentRequest` / `AgentResult` | shared contracts | 共享面只承载 runtime 主链所需稳定字段；不承载 access 私有 proof、secret、peer 原始句柄 | runtime、上层入口 |
| 观测 / 审计投影 | access | audit / metrics / tracing facts | module-local -> observability | `AccessObservabilityBridge` / `ResultPublisher` 只抽取最小必要字段：`request_id`、`trace_id`、`entry_type`、`actor_ref`、`auth_method`、`operation`、`target_ref`、`decision`、`policy_decision_ref`、`reason_code`、`publish_mode`、`outcome` | infra、审计、运维 |
| 敏感字段保留 | access | headers、credential refs、peer 原始地址、认证秘密 | module-local private | 只允许以脱敏 ref 或 hash 形式进入 observability；不得进入 shared contracts 或普通结果回包 | access 内部 |

### 6.2 v1 冻结规则

1. `AgentRequest` 是 access -> runtime 的唯一 shared request；`SubjectIdentity` 与 `AccessDecisionProof` 不进入 contracts。
2. `RuntimeDispatchRequest` 是 access module public handoff，`RuntimeInvokeContext` 是 bridge-local invoke shape；前者由 `RequestNormalizer` 生成，后者由 `RuntimeBridge` 生成，任何其他模块都不得重复拥有 sidecar 投影权。
3. runtime 可以消费 `RuntimeDispatchRequest` 或其下游 invoke context 中的 sidecar 事实做确认、审计关联和高风险动作门禁，但不得把整份 proof 或主体属性复制进 `AgentResult`。
4. access 到 infra 的 audit 投影只保留可追责的最小事实集；认证秘密、headers 原文、peer 原始句柄必须留在 access 私有域。
5. 如后续需要共享更稳定的 access 主体引用，只能新增单独的 shared ref 对象；在那之前不得把 `SubjectIdentity` 偷渡进 contracts。

## 7. 变更规则

1. 新增跨模块数据线，或修改本文件中任一 projection shape / owner / consumer，必须同步更新本文件、对应子系统详细设计和相关 tests/TODO。
2. 若某消费者文档与本文件冲突，以本文件为准；消费者文档只描述本地投影视图和实现约束，不重新定义上游语义。
