# UnaryResponseContract (Single Source of Truth)

状态：Frozen
Owner：runtime / cognition
关联任务：INT-TODO-002
关联阻塞：INT-BLK-02
关联 Gate：Gate-INT-03

## 1. 目的

本文件冻结 default single-agent unary 主链的最终响应合同，统一 `ResponseBuilder`、`AgentOrchestrator`、`RuntimeUnaryIntegrationTest` 与 `CognitionRuntimeIntegrationTest` 对 `response_text`、`status`、observation projection、llm fallback 的解释。

## 2. 范围与术语

适用范围：

1. `runtime -> cognition::IResponseBuilder::build() -> runtime final publish` 的 unary 终态输出链路。
2. 仅覆盖 `AgentResult.status`、`task_completed`、`response_text`、`structured_payload`、`goal_id`、`checkpoint_ref` 的合同边界。
3. 不覆盖 cancel / timeout / multi-agent 专项语义；这些仍由各自专项 Gate 管理。

术语约定：

1. `observation projection`：在存在 `latest_observation.payload` 但 llm bridge 不可用时，由 ResponseBuilder 直接把用户安全的 observation payload 投影为最终 `response_text`。
2. `llm fallback`：llm bridge 调用失败或返回空 payload 后，如果 template fallback 已启用，则转入模板降级而不是继续宣称 `Completed`。
3. `fixture path`：允许使用 fail-closed stub / null adapter 验证 terminal path 形状的 subsystem-local 路径。
4. `true integration path`：绑定真实 public interface 的跨模块路径；其成功合同由 `RuntimeUnaryIntegrationTest` 与 `CognitionRuntimeIntegrationTest` 锁定。

## 3. 核心合同

1. `ResponseBuilder` 负责生成终态 `AgentResult draft`；`AgentOrchestrator` 保留最终提交权，但只允许补齐 `request_id`、`trace_id`、`goal_id`、`checkpoint_ref`、`created_at` 等审计锚点，不得重写成功路径的 `response_text` / `status` 语义。
2. `Completed` 只允许出现在 `llm_bridge` 或 `observation_projection` 成功路径；凡是模板降级、llm fallback、部分信息可用的路径，都必须使用 `PartiallyCompleted`。
3. `Completed` 必须同时满足：`task_completed=true`、`response_text` 非空、`error_info` 为空，且 true integration path 下 `goal_id` 与 `checkpoint_ref` 在 publish 前已经由 runtime 补齐。
4. `PartiallyCompleted` 必须同时满足：`task_completed=false`、`response_text` 非空、`structured_payload.fallback_used=true`；它代表“用户拿到了可读结果，但系统不能宣称完整成功”。
5. `Failed` 只用于 builder invalid input、mode unavailable、runtime terminal error 等失败路径；失败结果不得携带成功式 observation projection 文本来伪装 `Completed`。
6. 在当前 Gate-INT-03 基线下，true integration success path 冻结为 observation projection 成功路径；未来若把 llm live path 升格为默认必需，必须同步更新本 SSOT、runtime/cognition 详设与 Gate 断言。

## 4. Response Mode Matrix

| 终态模式 | 进入条件 | 最终 `status` | `response_text` 规则 | `structured_payload` / 标签规则 | Gate 含义 |
|---|---|---|---|---|---|
| `llm_bridge` | 存在 `latest_observation.payload`，且 llm bridge 可用并返回非空 payload | `Completed` | 使用 llm 返回文本，经过 redaction / clamp 后作为最终 `response_text` | `response_mode=llm_bridge`；可带 `llm_route:*`；`fallback_used` 允许记录桥接链路内部回退，但不改变 `Completed` | 可作为未来 default-ready 的成功模式，但当前 true integration baseline 不以它为必需 |
| `observation_projection` | 存在 `latest_observation.payload`，但 llm bridge 不可用或未接线 | `Completed` | 必须保留 `runtime unary integration completed:` 前缀，并携带 redacted / clamped 的 observation payload 投影 | `response_mode=observation_projection`；`fallback_used=false` | 当前 `RuntimeUnaryIntegrationTest` 与 `CognitionRuntimeIntegrationTest` 的成功基线 |
| `template_fallback` | `prefer_template`、`TemplatePreferred`，或 llm fallback 发生且 template fallback 已启用，且存在 summary seed | `PartiallyCompleted` | summary seed 优先级固定为：`terminal_decision.response_outline.summary` -> `latest_observation.payload` -> `context_packet.current_goal_summary` -> `goal_contract.goal_description` | `response_mode=template_fallback`；`fallback_used=true`；必须带 `response_fallback_used` 标签 | 属于可观测降级路径，不满足 Gate-INT-03 的 `Completed` 成功定义 |
| `unavailable` / build error | 无 observation payload 且 template fallback 禁用，或输入校验/策略解析失败 | `Failed`（由 runtime 终态失败路径收敛） | 不得生成 success-like completion 文本 | `structured_payload` 可为空；必须通过 `error_info` / diagnostics 给出失败来源 | 只证明 fail-closed，不证明 unary success contract |

## 5. Fixture 与 True Integration 断言分层

| 路径 / 用例 | 允许断言 | 明确禁止 |
|---|---|---|
| `RuntimeUnaryFixtureIntegrationTest` | terminal path 会调用 `ResponseBuilder`、runtime 会补齐审计锚点、失败路径 fail-closed | 不能把 fixture path 的 success 结果外推为 `Completed` 真集成合同 |
| `RuntimeUnaryIntegrationTest` | `status=Completed`、`task_completed=true`、`goal_id` 与 `checkpoint_ref` 非空、`response_text` 包含 `runtime unary integration completed:` 与投影 payload | 不能接受 `PartiallyCompleted`、模板降级文本或仅有占位 observation 摘要 |
| `CognitionRuntimeIntegrationTest` | `status=Completed`、`task_completed=true`、`goal_id` 与 `checkpoint_ref` 非空、`response_text` 至少保留 completion 前缀 | 不能把 cognition 内部模板降级或 builder draft 未提交结果当作 true integration pass |

## 6. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| `Completed` / `PartiallyCompleted` 的切分必须与 response mode 对齐 | `INT-TODO-012` | `cognition/src/response/ResponseBuilder.cpp`、`runtime/src/AgentOrchestrator.cpp`、`tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h` | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest" --output-on-failure` |
| fixture gate 与 true integration gate 不得共享一套成功断言 | `INT-TODO-018` | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp`、`tests/integration/cognition/CognitionRuntimeIntegrationTest.cpp`、相关 CMake 注册 | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`MainFlowContractE2ETest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_integration_tests && ctest --test-dir build-ci -R "RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest|MainFlowContractE2ETest" --output-on-failure` |

## 7. 验证锚点

```bash
rg -n "response_text|observation projection|llm fallback|Completed|RuntimeUnaryIntegrationTest|CognitionRuntimeIntegrationTest" \
  docs/ssot/UnaryResponseContract.md \
  docs/architecture/DASALL_runtime子系统详细设计.md \
  docs/architecture/DASALL_cognition子系统详细设计.md
```

## 8. 结论

1. `INT-BLK-02` 的设计出口已经固定：response mode 与 `AgentResult.status` 的映射不再由 runtime、cognition、tests 各自猜测。
2. 当前 Gate-INT-03 的成功定义已经冻结为：true integration path 返回 `Completed`，并保留可审计的 `response_text`、`goal_id`、`checkpoint_ref`。