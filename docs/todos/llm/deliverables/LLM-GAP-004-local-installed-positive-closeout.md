# LLM-GAP-004 local installed positive evidence closeout

日期：2026-05-20  
任务：LLM-GAP-004  
状态：Done（本机 installed positive evidence 已解阻；不宣称 L6 external provider soak）

## 1. 执行边界

本轮承接 2026-05-19 的 blocker ledger：当时本机 installed run 仍落到 `accepted_async` 或 `task_not_completed`，没有产出 `llm.origin`。用户已完成本地安装态 API KEY 的安全部署后，本轮只验证 installed host 正向路径是否恢复，不使用 qemu / kvm，不记录 secret 明文，也不把少量本机 run 外推为 L6 长稳态。

本轮可升级的结论只有一项：同步 `dasall run` 经 production LLM path 返回 terminal completed，并持续带有 provider/model origin。以下事项仍不在本轮声明范围内：

1. external provider L6 soak / SLA。
2. current release candidate runner rerun / artifact archive。
3. qemu / machine-isolation negative slice。
4. async receipt ownership token / status follow-up 链。

## 2. 用户先验验证

用户提供的本机 installed smoke：

```text
sudo dasall run '{"prompt":"告诉我现在几点了"}' \
  --request-id llm-smoke-001 \
  --session llm-session-001 \
  --trace-id trace-llm-smoke-001 \
  --json
```

结果摘要：`disposition=completed`、`exit_code=0`、`task_completed=true`，`response_text` 首行包含 `llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`。这已经证明 2026-05-19 的 invalid-secret / no-origin blocker 不再复现。

## 3. 本轮复验 artifact

Artifact：`/tmp/dasall-llm-gap004-positive-1779243811`

执行摘要：

```text
total_runs: 5
exit0_count: 5
completed_count: 5
task_completed_count: 5
llm_origin_count: 5
agent_dataset_count: 0
accepted_async_count: 0
task_not_completed_count: 0
```

5 次 run 均使用本机 installed `sudo -n dasall run ... --json --timeout-ms 120000`，并分别带唯一 `request_id`、`session` 与 `trace_id`。每次均返回：

1. `disposition=completed`。
2. CLI exit code `0`。
3. `result.task_completed=true`。
4. `response_text` 含 `llm.origin=deepseek-prod/`。
5. 未出现 `agent.dataset` fallback。

artifact 目录保留 `run-01.json` 至 `run-05.json`、对应 stderr / exit code、daemon active 状态、pre/post journal snapshot 与 `summary.txt`。artifact 不包含 raw provider key。

## 4. Focused baseline 复验

本轮补跑 `LLM-FIX-007` 的 SOAK-00 focused baseline 口径，结果如下：

1. `ctest --test-dir build-ci --output-on-failure -R '^(LLMManagerTimeoutPolicyTest|LLMManagerRetryBudgetTest|ModelRouterStabilityTest|LLMObservabilityFieldCompletenessTest|LLMProductionObservabilityIntegrationTest|DeepSeekDualModeSelectionIntegrationTest|LLMFallbackIntegrationTest|LlmSecretPageTest|ConfigApplyWorkflowTest)$'`：4 个单测通过，5 个集成 / CLI 测试在该构建目录中为 `Not Run`，原因为 `build-ci` 缺少对应测试可执行文件；该结果只说明构建目录不完整，不记录为产品测试失败。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(LLMManagerTimeoutPolicyTest|LLMManagerRetryBudgetTest|ModelRouterStabilityTest|LLMObservabilityFieldCompletenessTest|LLMProductionObservabilityIntegrationTest|DeepSeekDualModeSelectionIntegrationTest|LLMFallbackIntegrationTest|LlmSecretPageTest|ConfigApplyWorkflowTest)$'`：`100% tests passed, 0 tests failed out of 9`。

因此，timeout / retry / fallback / routing / observability / secret bootstrap 的 focused baseline 在当前可用构建目录中保持绿色。

## 5. Closeout 结论

`LLM-GAP-004` 的本机 installed positive evidence 已解阻：当前安装态 API KEY 可用，CLI -> daemon -> runtime -> production LLM manager -> DeepSeek-compatible provider 的同步 generation 路径可产出 terminal completed 与 `llm.origin=deepseek-prod/`。

这会替换 2026-05-19 blocker ledger 的当前态判断，但不删除该历史记录。旧 blocker 仍用于解释：在 key 不可用或响应只进入 async / task_not_completed 时，不应把 focused tests 误写成 positive live evidence。

当前仍不得宣称的事项：

1. 不得宣称 L6 external provider soak 已通过。
2. 不得宣称 30 轮 SOAK-01 provider jitter 已完成。
3. 不得宣称 SOAK-02 / SOAK-04 qemu negative slice 已完成。
4. 不得宣称 async ownership/status follow-up 链已修复。
5. 不得把本机 installed evidence 外推为 current release candidate runner artifact archive。

## 6. 后续收敛条件

若要把本结论从“本机 installed positive evidence”继续升级到 L6 soak，需要按 `LLM-FIX-007` 的 slice 矩阵补齐：

1. SOAK-01：执行 30 轮 provider jitter，并统计 completed / `llm.origin` / failure reason。
2. SOAK-02：在隔离环境验证 network loss fail-closed。
3. SOAK-03：完成 secret rotate 前后正向 run 证据。
4. SOAK-04：补 live retry budget exhaustion negative slice。
5. SOAK-05：保留 observability trend artifact，并确认 route / provider / fallback 字段足够追溯。

上述升级仍必须遵守 secret redaction 与 owner 边界：raw key 不进入仓库、命令回显、journal 摘录或 artifact summary；release/machine-isolation 证据继续归 packaging / integration owner。