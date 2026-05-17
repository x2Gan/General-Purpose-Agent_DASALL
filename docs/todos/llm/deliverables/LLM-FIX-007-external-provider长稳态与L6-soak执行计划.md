# LLM-FIX-007 external provider 长稳态与 L6 soak 执行计划

日期：2026-05-17
任务：LLM-FIX-007
状态：Done（执行计划已冻结，证据尚未执行）

## 1. 本地基线

1. [docs/todos/llm/deliverables/LLM-FIX-004-L5-release-runner-provider长稳态证据收口.md](LLM-FIX-004-L5-release-runner-provider%E9%95%BF%E7%A8%B3%E6%80%81%E8%AF%81%E6%8D%AE%E6%94%B6%E5%8F%A3.md) 已把当前 llm 的 release evidence 固定为“历史 authoritative qemu L5 + 当前 local rerun L4 + fixed release-runner contract + provider failure-handling basis”，并明确没有 L6 soak 结论。
2. [docs/todos/integration/deliverables/FULLINT-TODO-019-release-runner当前候选版rerun与artifact-archive执行准备.md](../../integration/deliverables/FULLINT-TODO-019-release-runner%E5%BD%93%E5%89%8D%E5%80%99%E9%80%89%E7%89%88rerun%E4%B8%8Eartifact-archive%E6%89%A7%E8%A1%8C%E5%87%86%E5%A4%87.md) 已固定 qemu / artifact archive contract；因此 007 不再负责 current release candidate rerun，只负责 external provider 长稳态与 L6 soak 的执行矩阵。
3. [tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp](../../../../tests/unit/llm/LLMManagerTimeoutPolicyTest.cpp)、[tests/unit/llm/LLMManagerRetryBudgetTest.cpp](../../../../tests/unit/llm/LLMManagerRetryBudgetTest.cpp)、[tests/integration/llm/LLMFallbackIntegrationTest.cpp](../../../../tests/integration/llm/LLMFallbackIntegrationTest.cpp)、[tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp](../../../../tests/integration/llm/DeepSeekDualModeSelectionIntegrationTest.cpp)、[tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp](../../../../tests/integration/llm/LLMProductionObservabilityIntegrationTest.cpp) 与 [tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp](../../../../tests/unit/llm/LLMObservabilityFieldCompletenessTest.cpp) 已提供 timeout、retry budget、fallback、dual-mode routing 与 observability field 的 focused / integration basis。
4. [tests/unit/apps/cli/LlmSecretPageTest.cpp](../../../../tests/unit/apps/cli/LlmSecretPageTest.cpp)、[tests/integration/apps/cli/ConfigApplyWorkflowTest.cpp](../../../../tests/integration/apps/cli/ConfigApplyWorkflowTest.cpp)、[debian/tests/pkg-smoke-local-control-plane](../../../../debian/tests/pkg-smoke-local-control-plane) 与 [scripts/packaging/pkg_smoke_install.sh](../../../../scripts/packaging/pkg_smoke_install.sh) 已证明 provider secret 既能通过 `DASALL_DEEPSEEK_API_KEY_FILE` 注入，也能通过 `dasall config apply --from-file --no-input` 做 bootstrap / rotate，并且 summary 只暴露 redacted ref。

## 2. Design 结论

1. `LLM-FIX-007` 的完成标准不是“external provider L6 已执行完成”，而是“已把 provider jitter、network loss、secret rotate、retry budget exhaustion 和 observability trend 拆成可执行验收项，并与 `FULLINT-TODO-019` 彻底解耦”。
2. 007 不新增 llm 产品代码；它只冻结执行矩阵、命令模板、artifact 口径与通过/失败判定，避免后续把 soak 继续写成模糊 residual phrase。
3. 007 的所有 slice 都必须同时满足三条边界：
   - 不记录 secret 明文，不把 host-side key file 路径当作可归档秘密内容。
   - 不把 focused tests 或 plan 文档本身外推成 L6 已完成。
   - 不把 `FULLINT-TODO-019` 的 current release candidate rerun 与 L6 soak 混写成同一 owner。

## 3. Slice 矩阵

| Slice ID | 场景 | 现有依据 | 执行环境 | 必留 artifact | 通过判定 |
|---|---|---|---|---|---|
| SOAK-00 | focused baseline | timeout / retry / fallback / observability / secret bootstrap tests 已存在 | build-tree | ctest output | 后续所有 soak slice 之前，相关 focused tests 必须先绿 |
| SOAK-01 | provider jitter | installed `dasall run` 已具 L4 正向路径；release runner contract 已固定 | installed host 或 dedicated runner VM | 连续 run JSON、时间戳、exit code 汇总 | 连续正向调用无 silent hang；completed / `llm.origin` 比例与失败原因可统计 |
| SOAK-02 | network loss | timeout / fallback / ProviderTimeout 映射已有 focused basis | qemu runner / testbed | gate log、autopkgtest output-dir、setup script | 网络阻断后 fail-closed，命令在预算内返回非零；不出现无限重试或 silent success |
| SOAK-03 | secret rotate | `config apply` / `DASALL_DEEPSEEK_API_KEY_FILE` 双路径已存在 | installed host + runner host | 两轮 apply JSON、post-rotate run JSON、secret record | rotate 前后都只暴露 redacted ref；rotate 后正向 run 继续通过 |
| SOAK-04 | retry budget exhaustion | `LLMManagerRetryBudgetTest`、`LLMFallbackIntegrationTest` 已覆盖同路重试与 blocked threshold | build-tree + qemu negative slice | ctest output、negative gate log | build-tree retry budget 语义不回退；live negative slice 不出现无界重试 |
| SOAK-05 | observability trend | observability bridge / field completeness 已有 focused basis | build-tree + installed host | observability test output、soak run logs、journal snapshot | 关键字段在 focused tests 继续齐全，且 soak 期间有可比对的 route / fallback / provider trace 记录 |

## 4. 共用前提

1. build-tree 入口统一使用 `build-ci`；若该目录缺最新 target，先执行 `cmake -S . -B build-ci`。
2. runner / installed host 必须以 owner-only 权限提供 DeepSeek key file；所有文档、日志与 artifact 只允许记录 redacted ref 或 `deepseek_key_file=provided`。
3. `FULLINT-TODO-019` 与 007 可以共享同一 self-hosted runner 资产，但 007 的 negative/soak slice 不回写为 current release candidate rerun 通过。
4. `SOAK-02` 和 `SOAK-04` 的 negative slice 统一通过 `DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT` 或等价 guest-side 注入脚本实施，避免直接改仓库脚本。

## 5. 可执行验收项

### 5.1 SOAK-00 focused baseline

```sh
cmake -S . -B build-ci && \
cmake --build build-ci --target \
  dasall_llm_manager_timeout_policy_unit_test \
  dasall_llm_manager_retry_budget_unit_test \
  dasall_model_router_stability_unit_test \
  dasall_llm_observability_field_completeness_unit_test \
  dasall_llm_production_observability_integration_test \
  dasall_deepseek_dual_mode_selection_integration_test \
  dasall_llm_fallback_integration_test \
  dasall-llm_secret_page_unit_test \
  dasall_cli_config_apply_integration_test -j4 && \
ctest --test-dir build-ci --output-on-failure -R '^(LLMManagerTimeoutPolicyTest|LLMManagerRetryBudgetTest|ModelRouterStabilityTest|LLMObservabilityFieldCompletenessTest|LLMProductionObservabilityIntegrationTest|DeepSeekDualModeSelectionIntegrationTest|LLMFallbackIntegrationTest|LlmSecretPageTest|ConfigApplyWorkflowTest)$'
```

通过判定：全部测试通过；任何单项红灯都必须先回到 focused 层修复，再执行长稳态 slice。

### 5.2 SOAK-01 provider jitter

```sh
export DASALL_DEEPSEEK_API_KEY_FILE=/secure/keys/deepseek-primary.key
bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check

soak_dir=/tmp/dasall-llm-fix-007/provider-jitter
rm -rf "$soak_dir" && mkdir -p "$soak_dir"

for iteration in $(seq 1 30); do
  sudo -n dasall run '{"prompt":"请回复 stable-ack。"}' --json --timeout-ms 120000 \
    > "$soak_dir/run-${iteration}.json"
done

rg -n '"disposition":"completed"|llm.origin=deepseek-prod/' "$soak_dir"
find "$soak_dir" -maxdepth 1 -type f | sort
```

通过判定：30 轮命令都在超时预算内返回；正向输出持续包含 completed / `llm.origin=deepseek-prod/`；失败时必须留下对应 JSON 和轮次，不允许 silent drop。

### 5.3 SOAK-02 network loss

```sh
cat >/tmp/dasall-llm-fix-007-network-loss.sh <<'EOF'
#!/bin/sh
set -eu
printf '127.0.0.1 api.deepseek.com\n' >> /etc/hosts
EOF
chmod 755 /tmp/dasall-llm-fix-007-network-loss.sh

export DASALL_DEEPSEEK_API_KEY_FILE=/secure/keys/deepseek-primary.key
export DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT=/tmp/dasall-llm-fix-007-network-loss.sh
export DASALL_AUTOPKGTEST_OUTPUT_DIR=/tmp/dasall-llm-fix-007/network-loss-autopkgtest

gate_log=/tmp/dasall-llm-fix-007/network-loss-gate.log
set +e
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu /var/lib/libvirt/images/dasall-test.qcow2 >"$gate_log" 2>&1
gate_status=$?
set -e

test "$gate_status" -ne 0
rg -n 'timeout|ProviderTimeout|transport|retry' "$gate_log" "$DASALL_AUTOPKGTEST_OUTPUT_DIR"
```

通过判定：命令非零退出且在预算内返回；artifact 中存在 fail-closed 的 timeout / transport / retry 证据；不允许卡死到手工 kill。

### 5.4 SOAK-03 secret rotate

```sh
install -m 600 /secure/keys/deepseek-primary.key /tmp/dasall-llm-rotate-primary.key
install -m 600 /secure/keys/deepseek-secondary.key /tmp/dasall-llm-rotate-secondary.key

cat >/tmp/dasall-llm-rotate-primary.yaml <<'EOF'
profile_id: desktop_full
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: file:/tmp/dasall-llm-rotate-primary.key
      auth_profile_name: primary
EOF

cat >/tmp/dasall-llm-rotate-secondary.yaml <<'EOF'
profile_id: desktop_full
secrets:
  refs:
    - ref: secret://llm/providers/deepseek-prod
      source: file:/tmp/dasall-llm-rotate-secondary.key
      auth_profile_name: secondary
EOF

sudo -n dasall config apply --from-file /tmp/dasall-llm-rotate-primary.yaml --no-input --json > /tmp/dasall-llm-rotate-primary.json
sudo -n dasall config apply --from-file /tmp/dasall-llm-rotate-secondary.yaml --no-input --json > /tmp/dasall-llm-rotate-secondary.json
sudo -n dasall run '{"prompt":"请回复 rotate-check。"}' --json --timeout-ms 120000 > /tmp/dasall-llm-rotate-run.json

rg -n '"written_secret_refs":\["secret://llm/providers/deepseek-prod"\]' /tmp/dasall-llm-rotate-primary.json /tmp/dasall-llm-rotate-secondary.json
rg -n 'llm.origin=deepseek-prod/' /tmp/dasall-llm-rotate-run.json
```

通过判定：两轮 apply 都只回写 redacted secret ref；secondary rotate 后正向 run 继续成功；artifact 中不得记录 secret 明文。

### 5.5 SOAK-04 retry budget exhaustion

```sh
ctest --test-dir build-ci --output-on-failure -R '^(LLMManagerRetryBudgetTest|LLMManagerTimeoutPolicyTest|LLMFallbackIntegrationTest|ModelRouterStabilityTest)$'

cat >/tmp/dasall-llm-fix-007-retry-exhaust.sh <<'EOF'
#!/bin/sh
set -eu
printf '127.0.0.1 api.deepseek.com\n' >> /etc/hosts
EOF
chmod 755 /tmp/dasall-llm-fix-007-retry-exhaust.sh

export DASALL_DEEPSEEK_API_KEY_FILE=/secure/keys/deepseek-primary.key
export DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT=/tmp/dasall-llm-fix-007-retry-exhaust.sh
export DASALL_AUTOPKGTEST_OUTPUT_DIR=/tmp/dasall-llm-fix-007/retry-exhaust-autopkgtest

retry_gate_log=/tmp/dasall-llm-fix-007/retry-exhaust-gate.log
set +e
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu /var/lib/libvirt/images/dasall-test.qcow2 >"$retry_gate_log" 2>&1
retry_status=$?
set -e

test "$retry_status" -ne 0
rg -n 'timeout|ProviderTimeout|route blocked|retry' "$retry_gate_log" "$DASALL_AUTOPKGTEST_OUTPUT_DIR"
```

通过判定：build-tree retry budget / timeout / fallback tests 全绿；live negative slice 非零退出但不出现无界重试，日志里能看到 timeout / blocked / retry 证据。

### 5.6 SOAK-05 observability trend

```sh
ctest --test-dir build-ci --output-on-failure -R '^(LLMProductionObservabilityIntegrationTest|LLMObservabilityFieldCompletenessTest|DeepSeekDualModeSelectionIntegrationTest)$'

obs_dir=/tmp/dasall-llm-fix-007/observability
rm -rf "$obs_dir" && mkdir -p "$obs_dir"

sudo -n journalctl -u dasall-daemon --since '10 minutes ago' --no-pager > "$obs_dir/pre.log" || true
for iteration in $(seq 1 10); do
  sudo -n dasall run '{"prompt":"请回复 obs-check。"}' --json --timeout-ms 120000 > "$obs_dir/run-${iteration}.json"
done
sudo -n journalctl -u dasall-daemon --since '10 minutes ago' --no-pager > "$obs_dir/post.log" || true

find "$obs_dir" -maxdepth 1 -type f | sort
```

通过判定：observability focused tests 全绿；现场 soak 至少保留 `pre.log`、`post.log` 与连续 run JSON，可供后续统计 route / fallback / provider trace 趋势；若现场日志字段不足，结论应记录为“trend artifact 不足”，而不是默认为通过。

## 6. Artifact 口径

1. 每个 slice 都至少保留：原始命令、exit code、开始/结束时间、环境约束、输出文件路径。
2. secret 相关 artifact 只能出现 `secret://llm/providers/deepseek-prod`、`auth_profile_name` 或 `deepseek_key_file=provided` 这类 redacted 标识。
3. qemu slice 统一保留：gate log、`DASALL_AUTOPKGTEST_OUTPUT_DIR` 目录、boot/setup script 副本。
4. installed slice 统一保留：连续 `run-*.json`、必要的 `journalctl` snapshot、`config apply` JSON summary。

## 7. 风险与边界

1. 007 不把 `FULLINT-TODO-019` 的 current release candidate rerun 视为已完成；019 仍是当前版本 release-ready 证据 owner。
2. 007 不把 `LLMManagerRetryBudgetTest`、`LLMFallbackIntegrationTest`、`LLMProductionObservabilityIntegrationTest` 这些 focused tests 直接等价为 L6 soak；它们只是 soak 前置 gate。
3. 若 runner / installed host 无法提供 owner-only key file、qemu image、guest-side setup script 或 rootful journal 读取权限，本计划必须如实记为环境阻塞，而不是降格为“手工目测通过”。