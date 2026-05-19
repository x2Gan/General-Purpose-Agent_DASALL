# LLM-GAP-004 local installed soak blocker closeout

日期：2026-05-19  
任务：LLM-GAP-004  
状态：Done（LLM owner 证据边界已收口；不宣称 L6 positive evidence）

## 1. 执行边界

本轮按任务要求禁用 qemu / kvm 证据采集，不再把 `validate_gate_int_10_installed_package_qemu.sh -- qemu ...` 作为 LLM-GAP-004 的当前验收入口。当前验收口径改为：

1. build-tree focused baseline 证明 timeout / retry / fallback / observability / secret bootstrap 语义未回退。
2. 本机 installed host 证明 daemon、readiness、CLI run 与 async receipt 的真实现状。
3. 若本机 installed host 无法产出 `llm.origin=deepseek-prod/`，只记录 blocker，不把 failure-handling focused tests 外推为 L6 soak success。

本轮不把用户提供的 raw key 明文写入仓库、命令记录或 artifact。后续若要更新 provider key，必须通过 owner-only 本机安全通道执行 `dasall config apply --from-file` 或等价 secret import；文档只允许记录 `secret://llm/providers/deepseek-prod` 这类 redacted ref。

## 2. 本轮本机安装态证据

Artifact：`/tmp/dasall-llm-gap004-local-installed-1779204346`

结果摘要：

1. `systemctl is-active dasall-daemon` 返回 `active`。
2. 5 次 `sudo -n dasall run ... --json --timeout-ms 60000` 均返回 exit `0`，但 disposition 为 `accepted_async`。
3. 5 次 run 均未产出 `llm.origin`；`summary.txt` 记录 `completed_count: 0`、`llm_origin_count: 0`、`task_not_completed_count: 5`。
4. async JSON 只含 `receipt_ref=receipt-for-ticket-1`，没有 `ownership_token`，`request_id` 为 null；因此 `dasall status --receipt ...` 无法形成 owner-authoritative follow-up。

Artifact：`/tmp/dasall-llm-gap004-sync-shape-1779204782`

结果摘要：

1. JSON positional run 可到达 terminal response，但结果为 `disposition=completed`、`exit_code=5`、`error.reason=task_not_completed`，没有 `llm.origin`。
2. plain string / `--prompt` 形态进入 `accepted_async`，同样没有 ownership token 和 `llm.origin`。
3. 近端 daemon journal 未暴露可采信的 provider route / origin 正向字段。

Secret 元数据检查：

1. `/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret` 存在，权限为 `0640`，owner/group 为 `root:dasall`，大小为 `228` bytes。
2. 该文件不是 raw `sk-...` key，而是安装态 secret record / wrapped page；不能直接作为 curl bearer token 使用。

## 3. Focused baseline

本轮先复用了 `LLM-FIX-007` 的 SOAK-00 focused baseline，当前 build-tree failure-handling 基线保持绿色：

```text
LLMManagerTimeoutPolicyTest
LLMManagerRetryBudgetTest
ModelRouterStabilityTest
LLMObservabilityFieldCompletenessTest
LLMProductionObservabilityIntegrationTest
DeepSeekDualModeSelectionIntegrationTest
LLMFallbackIntegrationTest
LlmSecretPageTest
ConfigApplyWorkflowTest
```

通过结论只证明 LLM 内部 timeout / retry / fallback / routing / observability / secret bootstrap regression 没有回退；不证明 installed external provider L6 soak 已通过。

## 4. Closeout 结论

`LLM-GAP-004` 不再作为“缺少执行计划”或“LLM owner 未定义长稳态验收项”的开放缺口跟踪。当前 LLM owner 已完成：

1. external provider soak slice 矩阵已由 `LLM-FIX-007` 固化。
2. qemu/kvm 验收口径已在本轮执行中降级为禁止使用，不作为当前 closeout 依据。
3. 本机 installed host 的真实 blocker 已被记录为 artifact，而不是被 focused tests 掩盖。

当前不得宣称的事项：

1. 不得宣称 L6 external provider soak 已通过。
2. 不得宣称本机 installed `dasall run` 当前可稳定产出 `llm.origin=deepseek-prod/`。
3. 不得把 accepted_async receipt 视为已完成的 LLM generation evidence，因为当前响应缺少 ownership token / request id follow-up path。

## 5. 解阻条件

后续若要把 LLM external provider 长稳态从 blocker 转为 positive evidence，需要同时满足：

1. 通过本机安全通道导入可用 provider key，不在仓库、命令记录或文档中出现 raw key。
2. `sudo -n dasall run '{"prompt":"llm-gap004-positive-proof"}' --json --timeout-ms 120000` 返回 exit `0`、`disposition=completed`，并包含 `llm.origin=deepseek-prod/`。
3. async 路径返回可用 ownership token，或 `request_id` status 查询链可追踪到 completed / failed terminal state。
4. 至少一组连续本机 installed runs 保留 JSON、exit code、daemon journal snapshot 与 summary，且无 silent hang。
5. 若需要 release-runner / machine-isolation 证据，应另开 packaging / release 任务；本轮 LLM-GAP-004 禁止 qemu/kvm，不以 qemu/kvm 作为收敛依据。
