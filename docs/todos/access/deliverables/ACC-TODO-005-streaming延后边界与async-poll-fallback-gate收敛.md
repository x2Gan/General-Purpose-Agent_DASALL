# ACC-TODO-005 streaming 延后边界与 async/poll fallback Gate 收敛

日期：2026-04-23  
任务：ACC-TODO-005  
状态：D Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 已经把 `StreamGateway` 定义为 access core 内部组件，并明确 shared streaming lifecycle 未冻结时不能把完整 stream 语义当作首版硬门禁；但在本轮前，缺少一个可直接引用的统一 Gate matrix，去约束 attach/reconnect/replay cursor、WS/MQTT route 与 async/poll fallback 的默认行为。
2. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 已把 `StreamGateway / WS / MQTT` 标成 `L1 / Blocked`，但在本轮前仍主要停留在“阻塞存在”的描述，没有把 Access 侧必须采取的 `feature flag default-off + async receipt/poll fallback` 收成唯一口径。
3. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 已在 ACC-TODO-004 中冻结 gateway 首版为 HTTP-only unary + accepted async receipt + 独立 health listener；因此 005 的正确方向不是给 gateway 增补流式 ready，而是把未冻结的 stream/WS/MQTT 明确排除出 v1 ready 面。
4. [docs/architecture/DASALL_cli本地控制面详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_cli本地控制面详细设计.md) 已明确“async receipt 是 v1 唯一认可的断线恢复路径”，并要求如果 run 返回 receipt，就通过 status/query 获取结果而不是假设 CLI 会保持长连接。这为 Access 侧把流式失败统一回落到 receipt/query/poll 提供了现成上游约束。

## 2. 外部参考

1. RFC 9110 `202 Accepted` 明确指出：请求可以“已接受但处理尚未完成”，而 HTTP 本身“不提供异步操作完成后的二次状态回送能力”；因此响应表示应描述当前状态，并指向状态监视器。这与 DASALL access 在 shared stream lifecycle 未冻结时优先使用 `AcceptedAsync + receipt + poll/status monitor` 的策略一致。
   - 参考：<https://www.rfc-editor.org/rfc/rfc9110.html#name-202-accepted>

## 3. 设计结论

1. Access 侧正式冻结 `ACC-GATE-11`：在 runtime / llm / contracts 尚未共同冻结 `attach/reconnect/replay cursor` shared contract 前，`StreamGateway`、WS/MQTT route、upgrade、subscription 与任何 stream ready 表述一律不得进入 v1 Build-ready 结论。
2. streaming feature flag 必须 default-off；stream / WS / MQTT 在 v1 只能以占位、disabled/not ready 或设计卡片形式存在，不能被写成“可并行实现”的普通 Build 任务。
3. Access v1 唯一默认可交付的断线恢复路径固定为 `AcceptedAsync -> AsyncTaskReceipt -> access.task.query / poll`；该路径优先于任何未冻结的 reconnect、cursor replay、subscription keepalive 或 broker replay 承诺。
4. 只有在请求语义可以安全退化为 unary accepted async 时，才允许把 `stream_requested=true` 的请求回落到 receipt/query/poll；如果请求本质上依赖长连接订阅或持续推送，必须显式返回 disabled/not ready，而不是伪造 `StreamAttached` 成功态。
5. `StreamGateway` 继续保持 access module-local internal 级接口，不把 `attach_stream()`、`ReconnectToken`、`StreamCursor` 等对象提前写入 `access/include` 或 contracts。

## 4. 边界 / 职责

| 对象 | 边界与职责 | 不允许事项 |
|---|---|---|
| `ACC-GATE-11` | 作为 Access 侧唯一流式准入门，约束 stream/WS/MQTT 在 v1 只能处于延后 Gate | 把“未冻结”解释成“可以先实现再收敛” |
| `StreamGateway` | 仅在 gate lifted 后负责 attach、heartbeat、慢消费者隔离、有限 replay 与 poll fallback | 在 gate 未解除时创建 stream session、发放稳定 reconnect token 或宣称完整 replay |
| WS/MQTT adapters | 保留 entry-specific decode/encode 设计占位，等待后续 lifecycle 收敛 | 提前绑定 listener、upgrade route、topic attach 或 ready 结论 |
| `AsyncTaskRegistry` / `ResultReplayCache` | 提供 accepted async receipt、query、bounded replay ref，是 v1 默认恢复路径 | 假装自己已经等价于完整 streaming lifecycle |

## 5. 延后 Gate 矩阵

| 场景 | 允许行为 | 禁止行为 | 必须回退 |
|---|---|---|---|
| `stream_requested=true`，但 feature flag 关闭或 shared lifecycle 未冻结 | 若业务语义可退化为 unary accepted async，则返回 `AcceptedAsync` + receipt | 伪造 `StreamAttached`、偷建 session、发放未冻结 reconnect token | `AsyncTaskReceipt` + `access.task.query` / poll |
| 请求依赖纯订阅/持续推送语义，无法退化为 unary accepted async | 显式返回 disabled/not ready | 把 subscription 假装成异步任务或 silent downgrade | not ready / disabled |
| reconnect token / replay cursor 未形成 shared contract | 只允许读取已有 `ResultReplayCache` / query 结果 | 承诺精确 frame replay、跨连接恢复完整订阅态 | poll/query 结果或 not ready |
| WS/MQTT upgrade、broker/topic attach | 保留设计占位，默认 disabled/not ready | 在 v1 ready 面暴露 listener、upgrade、topic route | disabled/not ready |
| 慢消费者、heartbeat 失败、channel unavailable | 断开连接或拒绝 attach，并在 receipt 可用时给出 poll 指引 | 持有无界队列、阻塞 runtime/publisher 主链 | detach + async receipt/query/poll |

## 6. 数据 / 接口说明

1. `StreamGateway` 相关对象仍保持 module-local：`StreamSubscriptionRequest`、`StreamSession`、`StreamFrame`、`StreamCursor`、`SlowConsumerState`、`ReconnectToken` 不进入 contracts，也不进入 `access/include`。
2. 允许稳定进入 v1 的对象仍只有 unary/async 主链：`RuntimeDispatchRequest`、`PublishEnvelope`、`AsyncTaskReceipt`、`AsyncTaskRegistry`、`ResultReplayCache`。
3. `build_poll_fallback()` 的设计意图不是把 stream 伪装成 ready，而是在 attach/reconnect/replay 无法被安全证明时，统一发放 query/poll 指引。
4. 对 HTTP/gateway 而言，004 已冻结首版只承诺 HTTP unary + accepted async receipt；005 不改变该结论，只是进一步明确 WS/MQTT/streaming 继续是 disabled/not ready。

## 7. 流程 / 时序

1. 客户端发起 unary 请求且 runtime 接受异步执行：Access 返回 `AcceptedAsync` + `AsyncTaskReceipt`。
2. 客户端后续通过 `access.task.query` / poll path 查询状态，必要时结合 `ResultReplayCache` 读取 bounded result/ref。
3. 如果客户端请求 stream attach，但 `ACC-GATE-11` 仍未解除：
   - 可退化请求：直接返回 receipt + poll instruction；
   - 不可退化请求：立即返回 disabled/not ready。
4. 如果出现 reconnect、cursor replay、WS upgrade、MQTT topic attach 等未冻结语义：Access 不创建“半吊子 stream session”，而是统一保持 not ready 或回到 receipt/query/poll。

## 8. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.14.9、10.2、10.3、11、12。
2. TODO / blocker / gate / 风险回写在 [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md)。
3. 本任务交付物落于 [docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md)。
4. 证据日志落于 [docs/worklog/DASALL_开发执行记录.md](/home/gangan/DASALL/docs/worklog/DASALL_开发执行记录.md)。

## 9. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `ACC-GATE-11` 延后门 | `tests/integration/access/AccessProfileCompatibilityTest.cpp`、`tests/integration/access/AccessObservabilityIntegrationTest.cpp` 中对 not-ready / deny / fallback 的断言 |
| `AsyncTaskReceipt -> query/poll` 默认恢复路径 | `access/src/AsyncTaskRegistry.cpp`、`apps/gateway/src/TaskQueryHandler.cpp` |
| `StreamGateway` internal-only 占位接口 | `access/src/StreamGateway.cpp` 占位实现与 `AccessStreamReconnectIntegrationTest.cpp` 设计占位 |
| WS/MQTT disabled/not ready 规则 | 后续 `WebSocketProtocolAdapterTest.cpp`、`MqttProtocolAdapterTest.cpp` 必须先验证 Gate，再讨论 ready |

## 10. Build 三件套

1. 代码目标：只更新 Access 详设 / TODO / worklog / deliverable，冻结流式延后 Gate 与 async/poll fallback 口径，不接入任何新的 streaming listener、route 或 ABI。
2. 测试目标：通过 architecture / TODO / deliverable 一致性检索，确认 `StreamGateway`、feature flag、`ACC-GATE-11`、async receipt、poll fallback、WS/MQTT disabled/not ready 已形成唯一口径。
3. 验收命令：
   - `rg -n "StreamGateway|feature flag|async receipt|poll fallback|default-off|disabled/not ready|ACC-GATE-11" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-005-streaming延后边界与async-poll-fallback-gate收敛.md`

## 11. 剩余阻塞

1. ACC-TODO-005 已完成 Access 侧的 blocker recovery，但不等于 shared streaming lifecycle 已被上游冻结。
2. 后续若 runtime / llm / contracts 没有共同冻结 `attach/reconnect/replay cursor` shared contract，则 `StreamGateway`、WS/MQTT、stream-ready 相关 Build 任务仍不得被写成 ready。
3. 如果未来有人尝试把 feature flag 从 default-off 改成默认开启、把 receipt/query/poll 改成“临时过渡方案”而不回写设计，必须回退到本轮冻结的延后 Gate 结论。