# ACC-FIX-001 installed async receipt positive gate 收口

来源任务：ACC-FIX-001
完成日期：2026-05-22

## 1. 任务边界

1. 本轮只收口 Access installed async receipt 正向 gate，不把 installed HTTP gateway、multi-instance receipt authority、streaming lifecycle、qemu / release-runner 证据混入本轮。
2. authoritative 问题定义固定为：本机 real installed package 是否已经能产出真实 `submit -> receipt -> status -> replay -> cancel` 证据，并保留 HMAC ownership token 与 replay cache 语义。
3. 本轮明确禁止使用 qemu / kvm；证据仅来自本机 installed daemon / CLI 与 package smoke artifact。

## 2. 设计回链

1. `docs/architecture/DASALL_access子系统详细设计.md` 6.19.2 已冻结 receipt ownership 规则：`actor_ref` 与 `ownership_token` 必须双因子匹配，`ownership_token = HMAC-SHA256(server_secret, receipt_id || actor_ref || request_id)`。
2. 同一详设的 `ResultReplayCache` 章节已冻结 async query / replay 的 bounded replay cache 责任，要求 replay hit 继续复用现有 receipt/request 维度索引，而不是额外扩 public schema。
3. `ACC-TODO-033` 已完成 build-tree ownership/query/cancel 单元门，`ACC-TODO-046` 已完成正式 HMAC token 与 secret missing fail-closed；`ACC-FIX-001` 只补 local installed positive evidence，不重复扩写 ownership ABI。
4. 外部授权基线采用 OWASP Authorization Cheat Sheet：Deny by Default、Validate the Permissions on Every Request、Create Unit and Integration Test Cases for Authorization Logic。该基线直接对应 receipt owner mismatch 必须 fail-closed、且 package smoke 需把正向与负向路径一起固定。

## 3. 实现摘要

1. `access/include/AccessGatewayFactory.h` / `access/src/AccessGatewayFactory.cpp`
   - 新增 daemon internal `AsyncReceiptObserver` seam。
   - receipt attach 后允许组合根旁路观察完整 `AsyncTaskReceipt`，但不扩 public CLI/UDS schema。
2. `apps/daemon/src/main.cpp`
   - 新增 `DASALL_DAEMON_ASYNC_RECEIPT_PROOF_DIR` env-gated proof mode。
   - proof mode 下为 async 请求注入本地 `AsyncTaskRegistry`、proof-only dispatch backend、cancel backend 与 receipt observer。
   - 默认 daemon 行为不变；只有 package smoke 拉起临时 proof daemon 时才启用该路径。
3. `scripts/packaging/pkg_smoke_install.sh`
   - 在 installed package smoke 中生成临时 daemon config，拉起 proof-mode daemon，并固定 `access-installed-async-receipt-proof.json` artifact。
   - 固定验证：`accepted_async` submit、owner-matched `status=active`、same-request replay、`status_owner_mismatch`、`cancel_owner_mismatch`、owner-matched `cancelled` 与 `status_after_cancel=cancelled`。
   - `cancel_owner_mismatch` 采用 root CLI 真实 peer identity 触发 `local://uid/0` mismatch，不再伪造 payload actor。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 落点 |
|---|---|
| installed package 必须返回真实 `accepted_async` receipt | `pkg_smoke_install.sh --explicit-start-check` 生成 `access-installed-async-receipt-proof.json`，`submit.disposition=accepted_async`、`receipt_ref=receipt:acc-fix-001-installed-async-proof` |
| receipt ownership 继续按 HMAC + actor 双因子校验 | artifact 固定 `status_owner_mismatch.error.error_ref=status_owner_mismatch`、`cancel_owner_mismatch.error.error_ref=cancel_owner_mismatch`，两者 `exit_code=4` |
| replay 继续复用现有 receipt / replay cache 语义 | artifact 固定 `replay.disposition=accepted_async`，且 `replay.receipt_ref` 与初次 submit 相同 |
| cancel 正向路径必须仍可把 receipt 终态更新为 `cancelled` | artifact 固定 `cancel.result.response_text=cancelled` 与 `status_after_cancel.result.response_text=cancelled` |
| build-tree async causality 不得回退 | `DaemonReceiptFlowIntegrationTest`、`AccessAsyncReceiptQueryCancelIntegrationTest`、`FullIntAsyncRecoveryCausalityTest` 回归通过 |

## 5. D Gate

1. 只在 env-gated proof mode 下引入 proof registry；默认 installed daemon 主链与 public surface 不变。
2. 不新增 CLI / daemon public schema；ownership token 与 actor 仍通过现有 receipt/query/cancel 语义闭合。
3. 不把 proof-mode local secret 外推为 deployment secret wiring authoritative 结论；本轮 authoritative 结论仅限 installed package positive receipt gate 已建立。
4. 不把 multi-instance receipt authority、HTTP gateway、release-runner 或 qemu 证据写成已完成。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall-daemon","dasall_access_integration_tests","dasall_fullint_011_async_recovery_causality"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["DaemonReceiptFlowIntegrationTest","AccessAsyncReceiptQueryCancelIntegrationTest","FullIntAsyncRecoveryCausalityTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
   - fallback：`ctest --test-dir build/vscode-linux-ninja -R '^(DaemonReceiptFlowIntegrationTest|AccessAsyncReceiptQueryCancelIntegrationTest|FullIntAsyncRecoveryCausalityTest)$' --output-on-failure`
   - 结果：3/3 通过。
3. `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-rt-fix-006-pkg-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - 结果：通过；artifact 目录新增 `access-installed-async-receipt-proof.json`。
   - 关键字段：
     - `submit.disposition=accepted_async`
     - `status_active.result.response_text=active`
     - `replay.receipt_ref=receipt:acc-fix-001-installed-async-proof`
     - `status_owner_mismatch.exit_code=4`
     - `cancel_owner_mismatch.exit_code=4`
     - `cancel.result.response_text=cancelled`
     - `status_after_cancel.result.response_text=cancelled`

## 7. 完成判定

1. `ACC-FIX-001` 已完成。
2. BC-04 不再只有 missing receipt reject；本机 installed package 已具备真实 async receipt 正向 artifact。
3. 本结论不外推为 multi-instance authority、HTTP gateway、qemu / release-runner 或 release hardening 已闭合；这些仍留在 `ACC-FIX-002/003/005` 或更高层环境验证。