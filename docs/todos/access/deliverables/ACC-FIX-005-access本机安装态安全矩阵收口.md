# ACC-FIX-005 access 本机安装态安全矩阵收口

来源任务：ACC-FIX-005
完成日期：2026-05-27

## 1. 任务边界

1. 本轮只收口 Access owner 的安全验收矩阵归属，不新增 gateway/daemon public surface，也不扩展 qemu / kvm / autopkgtest 测试床。
2. authoritative 问题定义固定为：Access 当前 security hardening 应由哪组证据持有。结论是本机 installed proof + focused release-preflight tests，而不是把 qemu / machine isolation 写成 Access owner 当前验收前置。
3. 本轮不重写 `pkg_smoke_install.sh` 业务流程；只把已存在的 `access-installed-async-receipt-proof.json`、`access-installed-gateway-http-proof.json`、`diag_disabled` 与 focused socket/policy seams 统一收敛成一份 Local installed security matrix。

## 2. 设计回链

1. `scripts/packaging/README.md` 已冻结 installed-package 功能矩阵，并明确“本机实际 installed-package smoke 是 authoritative local evidence；qemu / machine isolation 继续用于 release-runner 环境，不作为所有子系统能力闭合的强制前置”。
2. Access 当前已具备两类互补安全证据：
   - installed package smoke：`pkg_smoke_install.sh --explicit-start-check` 生成 `access-installed-async-receipt-proof.json` 与 `access-installed-gateway-http-proof.json`，固定 owner mismatch、missing-backend、negative listener surface 与 `diag_disabled`。
   - focused release-preflight tests：`DaemonSocketModeIntegrationTest` 与 `AccessPolicyBackendUnavailableIntegrationTest` 分别锁定 socket mode 与 policy fail-closed。
3. 之前真正未收口的不是实现，而是 owner 文档仍把 Access 安全矩阵写成“需要 L5/qemu 才能完成”。本轮把这层误导从顶层总账、Access TODO 与 packaging README 中移除。

## 3. 实现摘要

1. 新增 `tests/integration/access/AccessInstalledSecurityMatrixWordingGuardIntegrationTest.cpp`。
   - 扫描顶层总账，要求 `ACC-FIX-005` 与 `ACC-GAP-005` 明确改写为 `Local installed security matrix`。
   - 扫描 Access TODO，要求 installed async/gateway proof、`status_owner_mismatch`、`cancel_owner_mismatch`、`diag_disabled`、`DaemonSocketModeIntegrationTest` 与 `AccessPolicyBackendUnavailableIntegrationTest` 同时出现，并写明“不以 qemu / kvm 作为 Access owner 当前验收前置”。
   - 扫描 `scripts/packaging/README.md`，要求 installed-package 功能矩阵存在明确的 Access installed security row。
2. 更新 `tests/integration/access/CMakeLists.txt`。
   - 注册 `dasall_access_installed_security_matrix_wording_guard_integration_test` 与 `AccessInstalledSecurityMatrixWordingGuardIntegrationTest`，使该 owner 口径进入 focused integration discoverability。
3. 更新 `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/access/DASALL_access子系统专项TODO.md` 与 `scripts/packaging/README.md`。
   - 将 `ACC-GAP-005` / `ACC-FIX-005` 改写为 Local installed security matrix 已收口。
   - 明确 Access owner 当前安全矩阵由 installed proof + focused release-preflight tests 共同组成，不以 qemu / kvm 作为当前验收前置。
   - 将更高层 machine isolation 明确回交给 packaging / release 环境。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 落点 |
|---|---|
| Access owner 当前安全验收以本机 installed proof 为 authoritative local evidence | `AccessInstalledSecurityMatrixWordingGuardIntegrationTest` 扫描总账 / Access TODO / packaging README 的 Local installed security matrix 文案 |
| owner mismatch、missing-backend、diag disabled 必须继续可见 | `pkg_smoke_install.sh --explicit-start-check` 生成的 `access-installed-async-receipt-proof.json`、`access-installed-gateway-http-proof.json` 与 installed `diag_disabled` 文案被写入矩阵 |
| socket mode 与 policy fail-closed 不得丢失 | `DaemonSocketModeIntegrationTest`、`AccessPolicyBackendUnavailableIntegrationTest` 保持 focused release-preflight security seams |
| qemu / machine isolation 不再作为 Access owner 当前 blocker | 顶层总账、Access TODO 与 packaging README 统一改写为 packaging / release 环境 owner |

## 5. D Gate

1. 不新增 daemon/gateway/cli public surface，不改变 installed package 真实运行语义。
2. 不宣称更高层 machine isolation、release runner 或 soak 已执行；这些继续属于 packaging / release 环境复核。
3. 本轮只收口 Access owner 当前验收口径，不把 local installed evidence 外推为更高层 release-ready。
4. 不把 qemu / kvm 混入本轮完成判定。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_access_installed_security_matrix_wording_guard_integration_test"])`
   - 结果：通过。
2. 首次执行 `./build/vscode-linux-ninja/tests/integration/access/dasall_access_installed_security_matrix_wording_guard_integration_test`
   - 结果：失败；直接暴露顶层总账缺少 `Local installed security matrix` 明文，证实缺口位于 owner 文档口径。
3. 修正总账 / Access TODO / packaging README 后，再次执行 `./build/vscode-linux-ninja/tests/integration/access/dasall_access_installed_security_matrix_wording_guard_integration_test`
   - 结果：通过。
4. `Build_CMakeTools(buildTargets=["dasall_access_installed_security_matrix_wording_guard_integration_test","dasall_access_daemon_socket_mode_integration_test","dasall_access_observability_main_chain_integration_test"])`
   - 结果：通过。
5. `RunCtest_CMakeTools(tests=["AccessInstalledSecurityMatrixWordingGuardIntegrationTest","DaemonSocketModeIntegrationTest","AccessPolicyBackendUnavailableIntegrationTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
6. fallback：
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_installed_security_matrix_wording_guard_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_socket_mode_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_observability_main_chain_integration_test`
   - 结果：3/3 通过。
7. 当前安装态最小命令集：
   - `sudo -n dasall-cli readiness --json`
   - `sudo -n dasall-cli status receipt:missing token local://uid/0 --json`
   - `sudo -n dasall-cli cancel receipt:missing token local://uid/0 --json`
   - `sudo -n dasall-cli diag health --json`
   - 结果：分别返回 completed/exit 0、`status_missing`/exit 5、`cancel_missing`/exit 5、`diag_disabled`/exit 4。
8. 本机既有 installed artifact 复核：
   - `/tmp/rtsup-fix-005-installed-smoke/access-installed-async-receipt-proof.json` 保留 `status_owner_mismatch` / `cancel_owner_mismatch`。
   - `/tmp/rtsup-fix-005-installed-smoke/access-installed-gateway-http-proof.json` 保留 `negative_listener_exposed=false` 与 `detail=production submit pipeline unavailable`。

## 7. 完成判定

1. `ACC-FIX-005` 已完成。
2. Access owner 当前 security hardening 已由 Local installed security matrix 收口，不再把 qemu / kvm 写成当前验收前置。
3. `pkg_smoke_install.sh --explicit-start-check`、installed proof artifacts 与 focused release-preflight tests 现在组成统一矩阵：owner mismatch、missing-backend、diag disabled、socket mode 与 policy fail-closed 均有明确 owner。
4. machine isolation / release-runner rerun 继续归 packaging / release 环境；当前结论不外推。