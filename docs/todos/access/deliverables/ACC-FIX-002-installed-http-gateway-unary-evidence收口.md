# ACC-FIX-002 installed HTTP gateway unary evidence 收口

来源任务：ACC-FIX-002
完成日期：2026-05-22

## 1. 任务边界

1. 本轮只收口 Access installed HTTP gateway unary evidence，不把 streaming lifecycle、multi-instance authority、qemu / release-runner、TLS / authz release hardening 混入本轮完成判定。
2. authoritative 问题定义固定为：本机 real installed package 是否已经实际携带 gateway binary，并能在 installed binary 上完成 `GET /health/ready` 与 `POST /v1/submit` 正向请求，同时保留缺 backend 时的 fail-closed / no-listener 负向证据。
3. 本轮明确禁止使用 qemu / kvm；证据仅来自本机 rebuilt `.deb`、installed package smoke 与 `access-installed-gateway-http-proof.json`。

## 2. 设计回链

1. `docs/architecture/DASALL_access子系统详细设计.md` 6.20.2 已冻结 health probe 规则：gateway 暴露 `/health/live`、`/health/ready`、`/health/startup`，且 health listener 不经过 Admission pipeline。
2. 同一详设 6.20.3 已冻结 HTTP 安全头与 CORS 默认关闭语义；本轮 installed HTTP smoke 继续沿用 gateway main 的既有 unary/health surface，而不扩 public route。
3. `docs/ssot/GatewayBinaryProductionPathV1.md` 已冻结 gateway binary production composition rule：`runtime_dispatch_backend` 缺失时必须 fail-closed，`gateway->init()` 失败时应输出 `production submit pipeline unavailable` 等价语义，且 `/health/ready` 不得虚报 ready。
4. `docs/todos/access/DASALL_access子系统专项TODO.md` 现有安装态口径与 `Gate-INT-10` 边界要求 build-tree `GatewayBinaryUnarySmokeTest` / `GatewayBinaryMissingBackendRegressionTest` 不得被误写为 installed evidence；`ACC-FIX-002` 只补本机 installed owner，不回写为 qemu / release 结论。

## 3. 外部参考

1. OWASP REST Security Cheat Sheet：REST endpoint 应在每个 endpoint 做 access control、本地验证输入与 content type、限制 HTTP method，并使用语义正确的 HTTP status code；这支持我们继续把 `/v1/submit` 作为显式 unary POST surface、把 health endpoint 与业务入口分账，并保持 fail-closed 错误语义。
   - https://cheatsheetseries.owasp.org/cheatsheets/REST_Security_Cheat_Sheet.html
2. Microsoft Health Endpoint Monitoring pattern：健康探针不应只看 200，还应检查响应内容；业务 endpoint 与 health endpoint 可以分路径暴露，且 monitoring 应验证 response code 与 body detail。
   - https://learn.microsoft.com/en-us/azure/architecture/patterns/health-endpoint-monitoring

## 4. 实现摘要

1. `apps/gateway/CMakeLists.txt`
   - 为 `dasall_gateway` 增加 `OUTPUT_NAME dasall-gateway` 与 install rule，产出正式 installed binary `/usr/sbin/dasall-gateway`。
2. `debian/dasall-daemon.install`
   - 将 `usr/sbin/dasall-gateway` 纳入 `dasall-daemon` 包内容，使 rebuilt `.deb` 真正携带 gateway binary。
3. `apps/gateway/src/main.cpp`
   - 新增 env-gated `DASALL_GATEWAY_FORCE_MISSING_RUNTIME_DISPATCH_BACKEND` seam。
   - 仅当 package smoke 显式打开该 env 时，gateway main 才故意不注入 `runtime_dispatch_backend`，从而在真实 installed binary 上复现 `production submit pipeline unavailable` fail-closed；默认 public HTTP surface 不变。
4. `scripts/packaging/pkg_smoke_install.sh`
   - 新增 installed gateway smoke：拉起 `/usr/sbin/dasall-gateway --profile-id desktop_full --port <ephemeral>`，校验 `/health/ready` 返回 `READY runtime_readiness=default-ready`，并对 `POST /v1/submit` 固定 `status=200`、非空 `result_id`、非空 `payload`。
   - 同轮新增 missing-backend negative：在 env-gated seam 下启动同一 installed binary，固定 `stage=access-gateway-init`、`detail=production submit pipeline unavailable`，并断言 ready listener 不暴露。
   - artifact 固定为 `access-installed-gateway-http-proof.json`。

## 5. Design -> Build 映射

| Design 目标 | Build / Validation 落点 |
|---|---|
| installed package 必须实际携带 gateway binary | rebuilt `dasall-daemon_0.1.0-1_amd64.deb` 包含 `/usr/sbin/dasall-gateway` |
| installed gateway health 必须返回真实 ready/detail，而不是 stub-ready | `access-installed-gateway-http-proof.json.ready_body = READY runtime_readiness=default-ready` |
| installed HTTP unary 必须在 real installed binary 上返回成功 envelope | artifact 固定 `submit.status=200`、非空 `submit.result_id`、非空 `submit.payload` |
| 缺 backend 时 gateway main 必须 fail-closed 且不暴露 listener | artifact 固定 `negative_log` 含 `stage=access-gateway-init` 与 `detail=production submit pipeline unavailable`，且 `negative_listener_exposed=false` |
| build-tree gateway unary / missing-backend 回归不得退化 | `dasall_gate_int_10` 通过；`dasall_access_gateway_submit_composition_test` 与 `dasall_access_gateway_binary_unary_smoke_integration_test` 通过 |

## 6. D Gate

1. 新增的 missing-backend seam 仅为 env-gated package smoke 使用；默认 installed gateway 行为与 public route 不变。
2. 不新增 gateway config schema；installed proof 继续使用现有 `--profile-id` / `--port` 输入面与 install layout。
3. 不把本轮结论外推为 auth/policy deny release matrix、multi-instance authority、streaming readiness、TLS hardening 或 qemu / release-runner。

## 7. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_gate_int_10"])`
   - 结果：通过；`Gate-INT-10` discoverability 与 acceptance 继续保持绿色。
2. `RunCtest_CMakeTools(tests=["HttpGatewaySubmitIntegrationTest","GatewayBinaryUnarySmokeTest","GatewayBinaryMissingBackendRegressionTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
   - fallback：`Build_CMakeTools(buildTargets=["dasall_access_gateway_submit_composition_test","dasall_access_gateway_binary_unary_smoke_integration_test"])`
   - 结果：通过；custom target 实际执行了 submit composition、happy-path smoke 与 missing-backend regression。
3. package rebuild
   - 结果：rebuilt `.deb` 已包含 `/usr/sbin/dasall-gateway`。
4. `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-rt-fix-006-pkg-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - 结果：通过；artifact 目录新增 `access-installed-gateway-http-proof.json`。
   - 关键字段：
     - `gateway_binary_path=/usr/sbin/dasall-gateway`
     - `effective_profile_id=desktop_full`
     - `ready_body=READY runtime_readiness=default-ready`
     - `submit.status=200`
     - `negative_listener_exposed=false`
     - `negative_log` 含 `detail=production submit pipeline unavailable`

## 8. 完成判定

1. `ACC-FIX-002` 已完成。
2. `BC-02` 不再只有 build-tree app-binary 证据；本机 installed package 已具备真实 HTTP gateway unary positive artifact 与 missing-backend readiness negative artifact。
3. 本结论不外推为 policy deny / authz release matrix、streaming、multi-instance、qemu / release-runner 已闭合；这些继续保留给 `ACC-FIX-003/004/005` 或更高层 release 验证。