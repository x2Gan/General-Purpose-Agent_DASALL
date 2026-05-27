# RTSUP-FIX-005 installed / release-preflight composition gate closeout

来源任务：RTSUP-FIX-005
完成日期：2026-05-27
关联缺口：RTSUP-GAP-005
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/ssot/SystemIntegrationGateMatrix.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`、`docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`

## 1. 任务边界

1. 本轮只收口 runtime_support shared helper 的 local installed package smoke 与 build-tree release-preflight 分层证据，不再把 qemu / `autopkgtest` 当作当前 closeout 的 authoritative 边界。
2. qemu / `autopkgtest` handoff 继续归 packaging / release owner；本轮只要求把这条 owner 边界正式写回专项 TODO、总记录与 SSOT，避免继续把环境问题记成 runtime_support blocker。
3. 本轮包含一处必要的 gate 稳定性修复：`GatewayBinaryUnarySmokeTest` 的 `/health/ready` 等待窗口延长到 30 秒，以匹配真实 gateway startup / runtime composition 成本；该修复服务于 release-preflight / `Gate-INT-10`，不改变产品代码 owner。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| build-tree gateway binary smoke | `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp` 的 `wait_for_ready()` 现以 30 秒 deadline 等待 `/health/ready`；`./build-ci/tests/integration/access/dasall_access_gateway_binary_unary_smoke_integration_test_bin` 已重新通过 | `Gate-INT-10` 不再被真实 startup 成本误判为红灯，release-preflight 与 installed smoke 的前置 build-tree gate 已恢复稳定 |
| release-preflight gate | `cmake --build build-ci --target dasall_gate_int_10 -j2` 已通过 | daemon / gateway shared helper 的 app-binary / release-preflight 层证据仍保持绿色，且与 installed smoke 分层存在 |
| fresh rebuilt packages | `shell: copilot-rt-fix-006-rebuild-deb` 已重新生成并复制 `dasall-common_0.1.0-1_all.deb`、`dasall-cli_0.1.0-1_amd64.deb`、`dasall-daemon_0.1.0-1_amd64.deb` 与 `dasall_0.1.0-1_all.deb` 到 `/home/gangan/` | local installed smoke 的输入来自 fresh rebuilt packages，而不是历史残留包 |
| runtime installed proof | `/tmp/rtsup-fix-005-installed-smoke/runtime-installed-proof.json` 记录 `tool_status=Completed`、`tool_runtime_path=runtime_path:tool_positive`、`recovery_positive_status=Completed`、`recovery_positive_checkpoint_persisted=true`、`recovery_negative_binding_rejected=true`，并保留 `knowledge-installed-assets-ready` 等 external evidence | runtime_support shared helper 在 local installed 层已同时覆盖 tool positive、waiting / recovery positive、binding reject negative 与 knowledge installed marker |
| runtime summary proof | `/tmp/rtsup-fix-005-installed-smoke/runtime-proof.json` 记录 `direct_llm_disposition=completed`、`direct_llm_llm_origin_present=true`、`tool_positive_runtime_path=runtime_path:tool_positive`、`recovery_positive_runtime_path=runtime_path:recovery_positive` | installed runtime summary artifact 与 runtime-installed-proof 保持同轮一致，可分层说明 direct / tool / recovery owner |
| gateway HTTP proof | `/tmp/rtsup-fix-005-installed-smoke/access-installed-gateway-http-proof.json` 记录 `ready_body="READY runtime_readiness=default-ready"`、`submit.status=200`、`negative_listener_exposed=false` | gateway unary 在 fresh installed package 下已重新证明 positive ready/submit 与 missing-backend fail-closed，不再依赖 qemu |
| async receipt proof | `/tmp/rtsup-fix-005-installed-smoke/access-installed-async-receipt-proof.json` 记录 `accepted_async` submit/replay、`status_owner_mismatch` / `cancel_owner_mismatch` exit 4，以及 owner-matched `cancelled` terminal status | daemon async receipt owner、mismatch fail-closed 与 cancel terminal state 已在 fresh installed package 下重新闭合 |

## 3. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| release-preflight / app-binary 必须与 installed-package 分层记账 | `cmake --build build-ci --target dasall_packaging_preflight_tests dasall_gate_int_10 -j2` |
| gateway binary smoke 必须覆盖真实 main path 的 readiness，而不是短窗口假阴性 | `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp`、`dasall_access_gateway_binary_unary_smoke_integration_test_bin` |
| runtime_support installed evidence 必须来自 fresh rebuilt package smoke artifact family | `scripts/packaging/pkg_smoke_install.sh --explicit-start-check`、`runtime-installed-proof.json`、`runtime-proof.json` |
| gateway unary / missing-backend fail-closed 必须在 installed package 层复验 | `access-installed-gateway-http-proof.json` |
| async receipt ownership / mismatch fail-closed 必须在 installed package 层复验 | `access-installed-async-receipt-proof.json` |
| qemu / `autopkgtest` 只作为更高层 packaging / release handoff，不再阻塞 runtime_support closeout | `docs/todos/runtime/DASALL_runtime_support组件专项TODO.md`、`docs/ssot/BusinessChainIntegrationMatrix.md` |

## 4. 验证

1. `./build-ci/tests/integration/access/dasall_access_gateway_binary_unary_smoke_integration_test_bin`
	- 结果：通过。
2. `cmake --build build-ci --target dasall_gate_int_10 -j2`
	- 结果：通过。
3. `shell: copilot-rt-fix-006-rebuild-deb`
	- 结果：通过，fresh rebuilt `.deb` 已复制到 `/home/gangan/`。
4. `cd /home/gangan/DASALL && artifact_dir=/tmp/rtsup-fix-005-installed-smoke && DASALL_PACKAGE_SMOKE_ARTIFACT_DIR="$artifact_dir" bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
	- 结果：通过；artifact 目录包含 `runtime-installed-proof.json`、`runtime-proof.json`、`access-installed-gateway-http-proof.json`、`access-installed-async-receipt-proof.json`、`memory-proof.json`、`services-installed-proof.json`、`tools-installed-proof.json` 等文件。

## 5. 完成判定

满足以下条件时，`RTSUP-FIX-005` 可在系统总记录与 runtime_support 专项 TODO 中标记为 Done：

1. build-tree `dasall_gate_int_10` / `dasall_packaging_preflight_tests` 继续保持绿色。
2. fresh rebuilt `.deb` 在 local installed smoke 下能重新生成 runtime/access authoritative artifact family。
3. 结论明确停留在 L3 release-preflight + L4 local installed，不把 local installed 结果外推为 qemu / release-runner ready。
4. qemu / `autopkgtest` handoff 被明确写回 packaging / release owner，而不是继续作为 runtime_support blocker。

本轮结论：`RTSUP-FIX-005` 可标记为 Done。runtime_support shared helper 的自有证据已完成从 build-tree app-binary / release-preflight 到 local installed package smoke 的分层闭合；更高层 qemu / `autopkgtest` 复核继续保留在 packaging / release 环境，不再阻塞本专项 closeout。