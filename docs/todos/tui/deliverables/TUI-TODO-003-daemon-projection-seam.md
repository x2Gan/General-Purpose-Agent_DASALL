# TUI-TODO-003 daemon projection seam 设计

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只冻结 daemon-backed TUI 的 projection seam：owner、operation surface、request/response envelope、TUI DTO 字段家族、稳定 reason code，以及 fake/daemon 复用边界。
2. 本任务不实现 `apps/tui` 生产代码，不把 `UdsRequestFrame` / `UdsResponseFrame` 或 CLI `--json` envelope 直接提升为 TUI contract，也不把新的 rich DTO 抬升进 shared contracts。
3. 本任务只解 `BLK-TUI-003`。runtime session registry、`/clear` / `/exit` 的真实 backend close/open 细节仍由 `BLK-TUI-007` 后续收口；`route_catalog` 的详细字段冻结仍后置到 `TUI-TODO-027`。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 7.4、9.5.3、9.5.4、9.6 节已明确：TUI 只能消费面向 UI 的 projection，`ITuiDataSource` 的正式实现经 `TuiIpcController` 访问 daemon projection，且 `TuiIpcController` 必须把 socket 不存在、`permission denied`、timeout、malformed response 归一化为稳定错误码。
2. 同一文档第 9.2 节已给出 `TuiSessionView`、`TuiStatusProjection`、`TuiModelRouteProjection` 的 module-local 草案；第 10/11 节已把 Phase 3/4 的关键产出固定为 projection contract、daemon attach、status projection 与 focused tests。
3. `docs/ssot/CrossModuleDataProjectionMatrix.md` 已冻结：shared contracts 只承载稳定最小视图，module-local 结构化对象继续归 owner 所有；consumer 不得从 lossy projection 反推上游内部对象。
4. `docs/ssot/AccessUnaryProductionPathV1.md` 已冻结：`AgentRequest` 是 access -> runtime 的唯一 shared request，`RuntimeDispatchRequest` 是 access module public handoff；`request_context` 不能成为共享请求事实的唯一承载位。
5. `docs/architecture/DASALL-daemon本地控制面详细设计.md` 已冻结：`UdsRequestFrame`、`UdsResponseFrame` 是 access daemon private/module-local 对象，daemon 壳层通过 `ResultPublisher` 向 CLI 回传稳定控制面结果，但这些对象不是 TUI 的用户面 contract。
6. `docs/architecture/DASALL-cli本地控制面详细设计.md` 已冻结：`DaemonClientResponse` 与 `--json` envelope 是 CLI private projection，不是 `AgentResult` 或 `UdsResponseFrame` 的镜像，更不能被 TUI 直接复用为稳定 projection seam。

## 3. 外部参考

1. Microsoft Azure Architecture Center 的 CQRS 模式明确指出：query/read model 应返回为 presentation layer 优化的 DTO，而不是直接暴露 write/domain model。TUI 作为 task-based UI，适合拥有独立的 read-model / projection seam，而不是直接消费 runtime/access 内部对象。
   - 参考：https://learn.microsoft.com/en-us/azure/architecture/patterns/cqrs
2. Google AIP-193 指出：稳定错误 contract 应提供 machine-readable identifier，例如 `reason`/`domain`/`metadata`，客户端不应依赖解析自由文本 message。TUI IPC 的失败语义应冻结到 reason code，而不是绑定内部异常文案。
   - 参考：https://google.aip.dev/193

## 4. 冻结结论

1. TUI daemon-backed 路径复用现有本地 daemon IPC 通道、`request_id` / `trace_id` / `session_id` 关联事实，但 TUI 的稳定 contract 不是 transport carrier 本身，而是 access/daemon owner 的 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`。
2. `UdsRequestFrame` / `UdsResponseFrame` 继续留在 access/daemon private 域；CLI `DaemonClientResponse` 与 `--json` envelope 继续留在 CLI private 域。`TuiIpcController` 只绑定 `TuiIpc*Envelope` 与 TUI projection DTO，不能直接绑定 raw carrier 或 CLI 私有对象。
3. `TuiIpcRequestEnvelope.operation` 在 v1 冻结为五个逻辑操作：`open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session`。后续如需扩展，只能做加法型 operation，不能重解释现有五个操作的语义。
4. access/daemon 是 daemon-backed projection 的唯一 owner：负责把 admission、dispatch、receipt、status、route、health、session facts 归一化为 TUI-safe projection。`apps/tui` 只拥有 module-local `TuiProjectionTypes` 与 ViewModel，不拥有 runtime/access 内部对象解释权。
5. `TuiSessionView`、`TuiTurnReceipt`、`TuiStatusProjection`、`TuiModelRouteProjection`、`TuiEventProjection` 继续保持 module-local，落位在 `apps/tui/src/data/TuiProjectionTypes.h`。这些类型可被 fake/daemon source 复用，但不会因此上升为 shared contracts。
6. `TuiRouteCatalogView` 与 `TuiToolSummaryView` 作为 supporting projection object 保留在同一 module-local 家族中：前者的详细字段冻结后置到 `TUI-TODO-027`，后者在本任务内只冻结摘要边界，不冻结实现细节。
7. `TuiIpcResponseEnvelope` 的失败 contract 在 v1 必须同时提供稳定 `reason_domain` + `reason_code`，并允许附带 `retryable`、`error_ref`、`metadata` 和用户可见 message。message 可以改写，reason code 不得随意漂移。
8. `permission_denied`、`daemon_unavailable`、`timeout`、`malformed_response` 等失败必须是 TUI 可直接分支的稳定 reason code，而不是“查看 stderr 文案后猜测”。`permission_denied` 与 `daemon_unavailable` 必须继续区分，不得合并成泛化的 startup failure。
9. `open_session` / `close_session` 在本任务内被冻结为 TUI daemon seam 的逻辑 operation 名称和 envelope 位置，但不宣称 runtime durable session registry 已 ready。`BLK-TUI-007` 未解前，只允许 fail-closed 或 limited bootstrap semantics，不允许伪造“session 已正式 close/open 成功”的事实。

## 5. `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope` 冻结面

### 5.1 公共 envelope 字段

| Envelope | 冻结字段 | 说明 | 明确禁止 |
|---|---|---|---|
| `TuiIpcRequestEnvelope` | `schema_version`、`operation`、`request_id`、`trace_id`、可选 `session_id`、`deadline_ms`、`payload` | TUI 到 daemon projection seam 的逻辑请求；可由 access/daemon 再包进自身 transport carrier | 直接把 `AgentRequest`、`RuntimeDispatchRequest`、CLI flags 对象或 runtime internal object 作为 TUI 请求体 |
| `TuiIpcResponseEnvelope` | `schema_version`、`operation`、`request_id`、`trace_id`、可选 `session_id`、`outcome`、可选 `payload`、可选 `reason_domain`、可选 `reason_code`、可选 `retryable`、可选 `error_ref`、可选 `metadata` | daemon/access 回给 TUI 的逻辑响应；TUI 只消费该 envelope 暴露的稳定字段 | 把 raw `UdsResponseFrame`、raw `AgentResult`、stack trace、private proof、secret refs 原样透传给 TUI |

### 5.2 `operation` 语义矩阵

| operation | 请求最小事实 | 成功 payload | owner 责任 | 当前后置项 |
|---|---|---|---|---|
| `open_session` | `request_id`、`trace_id`、startup/profile hint（可选） | `TuiSessionView` | 返回当前前台 session 的 bootstrap projection、readiness 和启动模式摘要 | durable session registry / query 仍受 `BLK-TUI-007` 约束 |
| `submit_turn` | `session_id`、`user_input`、`NextTurnPreference`、`request_id`、`trace_id` | `TuiTurnReceipt` | 负责 admission、dispatch、receipt/status 归一化；不得把 raw `AgentResult` 暴露给 TUI | `NextTurnPreference` 真链路承载仍由 `TUI-TODO-004` 冻结 |
| `poll_events` | `session_id`、可选 poll cursor、`request_id`、`trace_id` | `std::vector<TuiEventProjection>` 或等价 batch payload | 返回局部刷新所需的 status/tool/receipt/banner 摘要变化 | 不承诺公共 streaming/replay contract；bounded feed 仍后置 |
| `route_catalog` | 可选 `session_id`、profile/selector context hint | `TuiRouteCatalogView` | 返回 selector 所需的 route/readiness/disabled reason 投影 | 详细字段冻结后置到 `TUI-TODO-027` |
| `close_session` | `session_id`、close reason（如 `/exit`、`/clear`） | 成功 ack 或最小 close result | 负责归一化 close success / close unavailable / ownership mismatch | 真正的 session lifecycle 仍受 `BLK-TUI-007` 约束 |

## 6. TUI projection 字段家族

| Projection | 最小字段家族 | owner / consumer | 明确禁止 | fake/daemon 复用边界 |
|---|---|---|---|---|
| `TuiSessionView` | `session_id`、`profile_id`、`daemon_readiness`、`startup_mode`、`started_at` | owner=`access/daemon session seam`；consumer=`TuiApp` header、`/session`、退出确认 | runtime checkpoint/body dump、`SubjectIdentity`、`AccessDecisionProof`、peer uid/gid/pid、raw socket ACL 细节 | fake source 只能生成 UI 可见字段，不能伪造 access proof 或 OS 级主体事实 |
| `TuiTurnReceipt` | `request_id`、`trace_id`、`session_id`、`disposition`、`receipt_ref`、`submitted_at`、`summary_text`、`reason_code` | owner=`access receipt/status projection`；consumer=submit 结果、poll completion、banner | raw `AgentResult`、未归一化 exception、stack trace、secret refs、原始 tool payload | fake source 可复用相同字段；daemon-only correlation 字段允许为空，但字段名不得分叉 |
| `TuiStatusProjection` | `stage`、`current_tool`、`pending_interaction`、`budget_summary`、`recovery_summary`、`health_summary`、`safe_mode_summary` | owner=`runtime 经 access 投影`；consumer=status panel、badge、启动降级说明 | `RecoveryManager` 内部对象、raw policy state、未脱敏预算或恢复内部参数 | fake/status 场景可复用同结构；不得增加 only-fake 字段 |
| `TuiModelRouteProjection` | `current_provider_id`、`current_model_id`、`current_depth_tier`、`disabled_reasons`、`next_preference` | owner=`profile/llm route projection`；consumer=selector header、当前 route 摘要 | provider secret、完整 profile 文件、私有 provider adapter response | fake route catalog 可复用；真实 route 字段冻结后续只做加法扩展 |
| `TuiEventProjection` | `event_cursor`、`event_kind`、`session_id`、`timestamp`、可选 `status_delta`、可选 `turn_receipt`、可选 `tool_summary`、可选 `banner_reason` | owner=`access/runtime event projection`；consumer=`poll_events()`、reducer、局部刷新 | 公共 streaming/replay contract、raw event bus payload、完整 transcript dump | fake source 可产出 deterministic event batch，但 event kind 与 delta 语义必须与 daemon path 同名同义 |
| `TuiToolSummaryView` | `tool_name`、`risk_summary`、`observation_summary`、`latency_ms`、`badges` | owner=`tools/runtime 经 access 摘要`；consumer=transcript/status 局部摘要 | 原始工具输出、secret、路径敏感明文、binary payload | fake source 只可提供摘要文本和 badge，不可伪造 raw output |
| `TuiRouteCatalogView` | 当前 route 摘要、候选项列表、disabled reason 集合、selection affordance 所需最小字段 | owner=`profile/llm/access route catalog projection`；consumer=`TuiModelSelector` | provider secret、完整 allowlist 文档、完整 profile 配置文件 | 当前仅冻结为独立 projection object；字段细节后置到 `TUI-TODO-027` |

补充规则：

1. 所有 projection 字段优先保持 user-visible summary、reason code、timestamp、correlation id 级别；如需更丰富结构化对象，只能新增 supporting object，不得回写为 raw internal dump。
2. `request_id` / `trace_id` / `session_id` 属于 observability/correlation facts，可进入 TUI DTO，但只能作为对账与辅助展示字段，不得让 UI 逻辑依赖未冻结的 daemon 私有 ID 生成策略。
3. UI 组件只能绑定 `TuiProjectionTypes`；任何需要 access/daemon transport detail 的逻辑都必须停在 `TuiIpcController` 以内。

## 7. 稳定 reason code 矩阵

| `reason_domain` | `reason_code` | 适用操作 | 语义 | 备注 |
|---|---|---|---|---|
| `transport` | `socket_missing` | `open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session` | daemon socket 不存在或无法建立连接 | 启动降级与 daemon unavailable 可据此分支 |
| `transport` | `permission_denied` | 同上 | 命中 socket 权限不足或本地 trusted 判定失败 | 必须与 `daemon_unavailable` 区分；优先级高于“猜测 daemon 未启动” |
| `transport` | `timeout` | 同上 | 本地 IPC 超时 | message 可变；reason code 稳定 |
| `transport` | `peer_closed` | 同上 | 对端提前关闭连接 | 不得折叠进泛化 `timeout` |
| `protocol` | `schema_mismatch` | 同上 | envelope schema_version 不兼容 | 由 `TuiIpcController` 映射为稳定错误 |
| `protocol` | `malformed_response` | 同上 | daemon 响应缺字段、类型错误或不可解析 | 不得把 parse failure 伪装成业务拒绝 |
| `daemon` | `daemon_not_ready` | `open_session`、`submit_turn`、`route_catalog` | daemon 已 reachable，但 readiness 未通过 | 对应 `not_ready` / degraded 语义 |
| `daemon` | `daemon_unavailable` | `open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session` | daemon 未运行或健康探针失败 | 与 `socket_missing` 可在实现层合并，但 TUI 用户面仍需稳定表达“daemon 不可用” |
| `daemon` | `profile_missing` | `open_session`、`route_catalog` | 启动 profile 或 route projection 前置条件缺失 | 供 TUI-TODO-024 启动错误分支使用 |
| `session` | `session_not_found` | `poll_events`、`close_session`、后续 query | session_id 无效、过期或不属于当前 owner | 不能回退成 silent reopen |
| `session` | `close_unavailable` | `close_session` | 真实 close seam 未 ready 或 close 失败但可判定 | 与 `/clear` / `/exit` 的用户可见语义配合 |
| `request` | `validation_failed` | `submit_turn` | 输入、payload 或 selector 组合不合法 | 不能把 parser/local validation 错误偷偷送给 daemon |
| `request` | `request_rejected` | `submit_turn`、`open_session` | access policy 或 daemon owner 明确拒绝本次请求 | 与 `permission_denied` 区别：前者是业务/admission，后者是 transport/权限层 |
| `route` | `route_catalog_unavailable` | `route_catalog` | route catalog 暂不可用，但不代表 daemon 全局不可用 | 详细 route 字段仍由 `TUI-TODO-027` 冻结 |

补充规则：

1. TUI 分支逻辑必须绑定 `reason_domain` + `reason_code`；message 只负责展示，不负责判定。
2. `metadata` 只承载附加上下文，如 `socket_path`、`profile_id`、`receipt_ref`、`selector_mode` 等可公开动态值；不得承载 secret、full profile、raw tool output。
3. 若同一错误已向客户端暴露过某个 `metadata` key，后续实现只允许加法扩展，不允许删改现有 key 的语义。

## 8. fake / daemon source 复用边界

1. fake source 与 daemon source 共享 `TuiProjectionTypes.h`，但不共享 transport carrier、socket policy、`TuiIpc*Envelope` 或 daemon 私有 serialization 细节。
2. fake source 可以生成 deterministic `TuiSessionView` / `TuiTurnReceipt` / `TuiStatusProjection` / `TuiEventProjection`，用于 reducer、renderer、snapshot 与 UX 验证；但不得伪造 access proof、peer identity、raw `error_ref` 语义来冒充真实 daemon 行为。
3. daemon source 必须把 daemon/access 投影先收敛到 `TuiIpcResponseEnvelope` 再映射为 `TuiProjectionTypes`；不得让 `TuiApp`、`TuiReducer`、`TuiStatusPanel` 直接感知 socket、carrier 或 `UdsResponseFrame`。
4. 后续若 bounded event feed 落地，也只能作为 daemon-backed source 的新 carrier 或新 operation 追加；UI 和 fake source 继续只绑定 `TuiProjectionTypes` / stable reason code。

## 9. 对后续任务的直接约束

| 后续任务 | 锁定的代码目标 | 锁定的测试目标 | 锁定的验收命令 |
|---|---|---|---|
| `TUI-TODO-021` | `apps/tui/src/ipc/TuiIpcController.h` 需要落盘 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`，并把 `operation` 固定为 `open_session` / `submit_turn` / `poll_events` / `route_catalog` / `close_session` | `TuiDaemonProjectionMappingTest` | `rg -n "open_session|submit_turn|poll_events|route_catalog|close_session|permission_denied|timeout|malformed_response" docs/todos/tui/deliverables/TUI-TODO-021-daemon-projection-mapping.md` |
| `TUI-TODO-022` | `apps/tui/src/ipc/TuiIpcController.cpp` 必须把 `socket_missing`、`permission_denied`、`timeout`、`schema_mismatch`、`malformed_response` 等映射成稳定 reason code，而不是解析 message | `TuiIpcControllerTest`、`TuiIpcPermissionDeniedTest` | `ctest --preset vscode-linux-ninja -R "TuiIpc" --output-on-failure` |
| `TUI-TODO-023` | `apps/tui/src/data/DaemonTuiDataSource.cpp` 只能依赖 `TuiIpc*Envelope` 与 `TuiProjectionTypes`，不得直接读 CLI projection 或 raw daemon carrier | `DaemonTuiDataSourceContractTest` | `ctest --preset vscode-linux-ninja -R "TuiDaemonDataSource" --output-on-failure` |
| `TUI-TODO-024` | `apps/tui/src/app/TuiApp.cpp`、`apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp` 必须继续把 `permission_denied` 与 `daemon_unavailable` 分开处理，并允许 `profile_missing` 单独成因 | `TuiAppStartupFailureTest` | `ctest --preset vscode-linux-ninja -R "TuiAppStartupFailure" --output-on-failure` |
| `TUI-TODO-025` | `apps/tui/src/view/TuiStatusPanel.cpp` 与 `apps/tui/src/model/TuiReducer.cpp` 只能消费 `TuiStatusProjection` / `TuiToolSummaryView` / `TuiEventProjection`，不得透视 runtime/access 内部对象 | `TuiStatusProjectionContractTest`、`TuiStatusPanelIntegrationTest` | `ctest --preset vscode-linux-ninja -R "Tui(StatusProjection|StatusPanelIntegration)" --output-on-failure` |
| `TUI-TODO-027` | `docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md` 只允许在本文件冻结的 `route_catalog` owner/seam 上继续细化 `TuiRouteCatalogView` 字段 | `TuiRouteCatalogProjectionTest` | `rg -n "current_provider_id|current_model_id|disabled_reasons|allowlist|verification_state|health" docs/todos/tui/deliverables/TUI-TODO-027-route-catalog-projection.md` |

## 10. D Gate 结果

1. `BLK-TUI-003` 已被收敛为单一结论：daemon-backed TUI 不复用 CLI projection，也不绑定 raw daemon carrier；它通过 access/daemon owner 的 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope` 获取 TUI-safe read model。
2. operation surface、DTO 字段家族、reason code taxonomy 和 fake/daemon 复用边界已冻结到可执行粒度，足以作为 `TUI-TODO-021`、`022`、`023`、`024`、`025` 的统一前置设计基线。
3. `BLK-TUI-007` 没有被误写为已完成：本文件只冻结 seam 和投影，不提前宣称 session registry、real close/open 行为已经 ready。

结论：TUI-TODO-003 D Gate = PASS。本任务为文档决策任务，无独立 B 阶段；完成条件是 deliverable、专项 TODO、详设、总账与 worklog 口径同步回写。