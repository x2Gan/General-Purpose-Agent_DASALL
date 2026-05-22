# INF-FIX-006 infra release / soak gate 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-006`。
2. 本轮目标：为 infrastructure owner 建立 real local installed release / soak gate，在不使用 qemu / kvm 的前提下，把 diagnostics、health/readiness、secret unavailable、plugin safe unload 与 observability sink failure 收口为本机 authoritative artifact。
3. 用户附加约束：按 `project-implementation-cycle` 串行推进；如存在前置 blocker 先解阻；逐文件落盘；完成后按仓库规范提交推送；若验收口径写成 qemu，则改成真实本机 installed/local 口径再执行。

## 2. 本地证据

1. `.github/workflows/release-package-gate.yml` 变更前已经固定 package smoke、services soak、knowledge proof / failure / soak 等 local artifact，但没有 infra owner 自己的 local release / soak harness。
2. `scripts/packaging/pkg_smoke_install.sh` 已经能在 installed package 上建立 rootful lifecycle baseline，并验证 `diag health --json` 在默认配置下保持 `diag_disabled`；这正是 infra 正向 gate 的必要前置。
3. `tests/integration/infra/CMakeLists.txt`、`tests/integration/infra/health/CMakeLists.txt`、`tests/integration/infra/metrics/CMakeLists.txt`、`tests/integration/infra/secret/CMakeLists.txt` 与 `tests/unit/infra/plugin/CMakeLists.txt` 已经提供了 diagnostics、health、metrics failure、secret failure 与 plugin safe unload 的 focused binaries，缺的是 owner 级编排与 artifact contract，不是测试面本身不存在。
4. `scripts/packaging/README.md` 已明确 local authoritative evidence 不直接外推为 qemu PASS；因此本轮最小闭环是补 infra local gate，并把 qemu/autopkgtest 继续留在 packaging / release 环境复核。

## 3. 设计结论

### 3.1 根因收口

1. `INF-FIX-006` 的根因不是 infra 没有 diagnostics/health/secret/plugin/metrics 测试，而是 release-runner local evidence 缺少 infrastructure owner 自己的 authoritative harness 和 summary。
2. 若继续把 infra 章节的验收命令绑到 qemu/autopkgtest，就会把 packaging / release 的 machine-isolated 复核误写成 infra owner 当前轮次 blocker，违背本轮约束与已有 README 分层。
3. 因此最小有效修复是：新增 local gate 脚本复用 package smoke 的 installed baseline，在真实本机上先打开 `diag_enabled`，再重复执行 `readiness` / `diag health` 与 focused binaries，并把结果固定成 release-runner 可归档 artifact。

### 3.2 local gate 边界

1. 新增 `scripts/packaging/infra_release_soak_gate.sh` 后，infra owner 的 authoritative local artifact 固定为 `infra-release-soak-summary.json`、installed iteration JSON 与 focused binary logs。
2. workflow 现在在 qemu gate 前固定 infra local gate，但不把结果外推成 qemu PASS，也不宣称 machine isolation 已由 infra owner 闭合。
3. `qemu_required_for_this_gate` 在 summary 中显式固定为 `false`，用于避免后续把 packaging hardening 回流成 infra gap。

### 3.3 实现细节约束

1. 脚本使用 POSIX `sh`，helper 变量不能污染调用方标签；本轮验证中暴露过 artifact 文件名漂移，最终通过把 helper 参数名改成独立变量收口，确保 summary 与实际落盘文件名一致。
2. 本轮不改动 SecretManager / `ISecretManager` live composition owner；对应 app-level wiring 继续由 `INF-FIX-007` / `INF-FIX-008` 持有。
3. 本轮同样不宣称 qemu/autopkgtest、长时 soak chaos 或 release-ready package 结论已经闭合。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | infra owner 需要独立 local release / soak harness，而不是借用 qemu gate 充当当前轮次验收 | `scripts/packaging/infra_release_soak_gate.sh` |
| D2 | release-runner 必须在 qemu 前固定 infra local artifact 与日志路径 | `.github/workflows/release-package-gate.yml` |
| D3 | packaging contract 必须把 infra local gate 写入 gate 表、功能矩阵、artifact 列表与已落盘文件 | `scripts/packaging/README.md` |
| D4 | infrastructure 总账必须把 `INF-GAP-006` / `INF-FIX-006` 从 qemu blocker 改为 local gate 已闭合 | `docs/todos/DASALL_子系统查漏补缺专项记录.md` |
| D5 | 验证中发现的 POSIX `sh` 全局变量串改必须在同一脚本内修复，保证 artifact 名称与 summary 一致 | `scripts/packaging/infra_release_soak_gate.sh` |

## 5. Build 三件套

1. 代码目标：
   - 新增 `infra_release_soak_gate.sh`，在 real local installed daemon 上执行 `diag_enabled` config apply、等待 `READY`、重复运行 `readiness --json` / `diag health --json`，并顺序执行 diagnostics/health/secret/plugin/metrics focused binaries。
   - 更新 release workflow，新增 `infra-soak` artifact 目录与 `infra-soak.log`，在 qemu gate 前构建并执行 infra local gate。
   - 更新 packaging README 与 infrastructure 总账，明确 local owner artifact 已闭合，qemu/autopkgtest 继续归 packaging / release 复核。
2. 测试目标：
   - `dasall_infra_diagnostics_smoke_integration_test`
   - `dasall_infra_diagnostics_integration_test`
   - `dasall_health_wiring_integration_test`
   - `dasall_infra_health_cadence_integration_test`
   - `dasall_secret_failure_injection_integration_test`
   - `dasall_plugin_lifecycle_state_unit_test`
   - `dasall_metrics_failure_injection_integration_test`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_infra_diagnostics_smoke_integration_test","dasall_infra_diagnostics_integration_test","dasall_health_wiring_integration_test","dasall_infra_health_cadence_integration_test","dasall_secret_failure_injection_integration_test","dasall_plugin_lifecycle_state_unit_test","dasall_metrics_failure_injection_integration_test"])`
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-inf-fix-006-package-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-inf-fix-006-package-smoke bash scripts/packaging/infra_release_soak_gate.sh --artifact-dir /tmp/dasall-inf-fix-006-soak --build-dir build/vscode-linux-ninja`

## 6. Rollout Checklist

1. package smoke 已先于 infra gate 成功执行，installed baseline 与 rootful lifecycle 前提成立。
2. infra gate 会在 `diag_enabled: true` 下固定 10 轮 `readiness` / `diag health` iteration，并生成 `iteration-01..10` artifact。
3. diagnostics、health wiring、health cadence、secret failure injection、plugin safe unload 与 metrics failure injection focused binaries 已被 workflow 和脚本共同固定。
4. workflow 现在会归档 `infra-soak.log` 与 `infra-soak/infra-release-soak-summary.json`，不再只有 services / knowledge local artifact。
5. 验证中发现的 artifact 文件名漂移已修复；summary 中引用的 `iteration-XX-*.json` 与实际落盘名称一致。
6. 本轮未使用 qemu / kvm，也未把 local installed 结果外推为 machine-isolated package-ready 结论。

## 7. 风险与回退

1. 若后续把 infra 章节验收命令重新绑回 qemu/autopkgtest，会再次把 packaging / release 边界误回流成 infra owner blocker。
2. 若后续在 `infra_release_soak_gate.sh` 中继续复用通用变量名，POSIX `sh` 的全局变量语义会再次污染 artifact 标签，导致 summary 与文件系统漂移。
3. 若 future workflow 去掉 infra local step，release-runner 将重新缺失 diagnostics/health/readiness 正向与 failure slice 的 owner-level artifact。

## 8. D Gate

1. infra owner local release / soak gate 已从“缺 harness”推进到“脚本、workflow、README、总账、deliverable 同口径落盘”。
2. 实际验证已通过：focused build 成功，`pkg_smoke_install.sh --explicit-start-check` 成功，`infra_release_soak_gate.sh` 成功生成 `infra-release-soak-summary.json`、10 轮 iteration JSON 与 7 份 focused binary log。
3. qemu/autopkgtest machine isolation、SecretManager live composition 与更高层 soak/chaos 继续留在后续任务，不被本轮误收口。

结论：D Gate = PASS；`INF-FIX-006` 已以 infra owner local release / soak gate、release-runner artifact contract 与非 qemu 收敛口径收口。
