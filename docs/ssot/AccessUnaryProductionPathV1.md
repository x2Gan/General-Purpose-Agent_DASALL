# AccessUnaryProductionPathV1 (Single Source of Truth)

关联任务：INT-TODO-025  
关联阻塞：INT-BLK-06  
关联 Access 整改：ACC-TODO-040、ACC-TODO-041、ACC-TODO-042、ACC-TODO-049；ACC-BLK-008

## 1. 目标与范围

本文件冻结 Access v1 unary production path 的最小系统合同，统一以下四类口径：

1. `AgentRequest` 从 `RequestNormalizer` 到 `RuntimeDispatchRequest` / `RuntimeBridge` 的 handoff 规则。
2. `AccessGateway` 的 production pipeline 依赖完整性与 readiness 合同。
3. production profile 与 test profile 的 mock pipeline 使用边界。
4. Access v1 release evidence 允许采信的测试拓扑与拒绝采信的伪证据。

本文件不冻结 async query/cancel 的所有权与安全治理细节，也不覆盖 stream attach/reconnect/replay cursor；这些内容继续由 INT-TODO-028 与 deferred streaming blocker 收口。

## 2. 冻结的 unary production path

### 2.1 主链拓扑

Access v1 unary production path 固定为：

`entry adapter -> AccessGateway -> subject resolver -> authenticator -> policy gate -> admission -> validator -> RequestNormalizer -> RuntimeBridge -> runtime seam -> ResultPublisher`

只有 `contracts::AgentRequest` / `contracts::AgentResult` 允许进入 shared 主链；`SubjectIdentity`、`AccessDecisionProof`、publish context、adapter facts、ownership facts 继续保持 module-local sidecar。

### 2.2 `RuntimeDispatchRequest` handoff 规则

1. `RuntimeDispatchRequest` 是 Access module public handoff，必须由 `RequestNormalizer` 作为唯一 owner 生成。
2. `RuntimeDispatchRequest` 必须显式承载 `contracts::AgentRequest`，或与之等价的公开 bridge surface（例如 `dispatch(AgentRequest, RuntimeInvokeContext)`）；无论采用哪种实现，`AgentRequest` 都必须成为 public handoff 的正式输入，而不是留在 `RequestNormalizationOutput` 中无人消费。
3. `request_context` 只允许承载追踪、publish hint、治理投影等 sidecar 元数据，不能作为 `AgentRequest` 共享字段的唯一运输通道；`request_id` / `session_id` / `trace_id` / `user_input` / `request_channel` 等 shared request 事实必须能从 public handoff 直接验证。
4. `RuntimeBridge` 可以继续保留 `normalizer_ready` 这类 guard，但该 guard 只能作为补充校验，不能替代 `AgentRequest` public handoff 本身。

### 2.3 当前基线与禁止继续外推的现象

截至 2026-05-06，仓库内已知现象包括：

1. `access/src/RuntimeBridge.cpp` 当前仍依赖 `request_context["normalizer_ready"]` 做 guard，这说明 `AgentRequest` handoff 还未真正进入 public seam。
2. `access/include/AccessTypes.h` 的现有 `RuntimeDispatchRequest` 仍未显式携带 `AgentRequest`。
3. 上述现象只能作为 027 的修复基线，不能继续被解释为 Access production path ready。

## 3. `AccessGateway` readiness 与 production pipeline 合同

### 3.1 production pipeline 完整性

production profile 下，`AccessGateway::init()` 必须对以下 P0 依赖做 fail-closed 校验：

1. subject resolver
2. authenticator
3. policy gate
4. admission controller
5. request validator
6. RequestNormalizer
7. RuntimeBridge
8. ResultPublisher

若 profile 开启 async / replay / observability / config governance，相应的 async registry、replay cache、observability bridge、config view 也必须被显式注入并通过可用性校验；缺失时只能返回 init 失败或 readiness=false，不能降格为“空实现照常 Ready”。

### 3.2 readiness 规则

1. `AccessGatewayState == Ready` 的前提是 production pipeline 依赖完整且初始化成功，而不是单纯执行过 `init()`。
2. gateway / daemon 的 readiness 必须聚合同一套条件：`AccessGateway` 已 Ready、至少一个 adapter 已注册、`RuntimeBridge` 可达、policy/config health 通过。
3. `submit_pipeline_not_configured` 与 `gateway_not_ready_or_shutting_down` 只允许作为 fail-closed 负路径证据存在，不能成为 production profile 的常态行为。
4. `/health/live` 或 daemon ping 只证明进程 liveness，不证明 unary production path 已 ready。

### 3.3 当前基线与禁止继续外推的现象

截至 2026-05-06，仓库内已知现象包括：

1. `access/src/AccessGateway.cpp` 当前在未验证依赖完整性的情况下把状态直接切到 `Ready`。
2. `apps/gateway/src/main.cpp` 当前默认构造空 `AccessGateway`，并将 health readiness 直接写成 `true`。
3. 上述行为只能作为 027 的修复入口，不能再被视为 production ingress 已具备 readiness 证据。

## 4. production profile 与 test profile 边界

1. production profile 的 daemon / gateway 组合根必须装配真实 dependency bundle，不允许隐藏的 mock pipeline fallback。
2. test profile 可以使用显式 mock pipeline、mock runtime bridge、fixture adapter registry 或 fake config view，但必须在测试/fixture 代码中明确可见，并通过测试命名体现其 mock 属性。
3. 默认构造的 `AccessGateway`、空 `submit_pipeline_`、仅用于单测的 fake publisher 或 ping-only health 结果，只能作为 test profile 行为，不得被 app 二进制、deliverable、review 结论或系统 Gate 采信为 production 证据。
4. 任何基于 mock pipeline 的通过结果，都不能覆盖 `ACC-GATE-05`、`ACC-GATE-08`、`ACC-GATE-14` 与系统 `Gate-INT-08` 对 production path 的真实要求。

## 5. 测试拓扑与证据分层

AccessUnaryProductionPathV1 的正式证据分层固定如下：

1. contract：`AgentRequestContractTest`、`AgentResultContractTest`、`IdentityMetadataContractTest`，用于证明 Access 没有把 sidecar 污染回 shared contracts。
2. unit：`RuntimeBridgeAgentRequestHandoffTest`、`RequestNormalizerRuntimeBridgeCompatibilityTest`、`AccessGatewayProductionPipelineTest`、`AccessGatewayDependencyValidationTest`，用于证明 handoff 与 fail-closed 依赖校验成立。
3. integration：`DaemonAccessSubmitCompositionTest`、`GatewayAccessSubmitCompositionTest`、`AccessHealthReadinessIntegrationTest`、`CliDaemonSubmitIntegrationTest`，用于证明 daemon/gateway production 组合根、health readiness 与 unary submit 主链成立。
4. review / deliverable：只有在上述 unit + integration 证据稳定后，`INT-TODO-030` 与 `ACC-TODO-049/050` 才能把 Access v1 写成 ingress production ready；mock pipeline、ping/liveness、局部 envelope 字段或 app 空壳 smoke 都不构成交付证据。

## 6. Design -> Build 映射

1. `INT-TODO-027`：落实 `AgentRequest` handoff、`AccessGateway` production pipeline 与 daemon/gateway readiness。
2. `INT-TODO-028`：在 production path 固定后，补齐 ownership、policy、observability 安全治理闭环。
3. `INT-TODO-030`：把上述实现与 focused tests 收敛为 Access v1 production Gate 与系统交付证据。

对应 Access 专项任务映射：

1. `ACC-TODO-040` 负责把 `AgentRequest` 真正推进到 `RuntimeDispatchRequest` / `RuntimeBridge` public handoff。
2. `ACC-TODO-041` 负责把 `AccessGateway` 从函数注入 facade 收敛为 production pipeline + dependency validation。
3. `ACC-TODO-042` 负责将 daemon/gateway 组合根切换为 production dependency bundle，并让 readiness 聚合真实依赖。
4. `ACC-TODO-049` 负责把上述行为提升为 Access v1 release gate，而不是继续停留在局部单测或 ping smoke。

## 7. 完成判定

当且仅当以下结论同时成立时，才允许将 Access v1 unary production ingress 视为 Build-Ready：

1. `AgentRequest` 已进入 RuntimeBridge public handoff，不再只靠 `request_context` 侧带。
2. 空 pipeline 或依赖缺失的 `AccessGateway` 无法进入 `Ready`。
3. daemon/gateway production 组合根与 test profile mock path 已被明确分离。
4. readiness 只反映真实依赖，不再由 ping / liveness 或手工 `set_ready(true)` 冒充。
5. focused Access Gate 与系统 Gate 已有可重复的 unit / integration / contract 证据。