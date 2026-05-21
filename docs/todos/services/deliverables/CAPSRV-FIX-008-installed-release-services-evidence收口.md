# CAPSRV-FIX-008 installed / release services 证据收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-008` / `CAPSRV-FIX-008`。
2. 本轮目标：在用户明确禁止使用 qemu / kvm 的约束下，为 Capability Services 固定两类 authoritative local evidence owner：一是 installed package smoke 产出的 `services-installed-proof.json`，二是 release-runner local soak 产出的 `services-soak-summary.json`。
3. 完成判定：`pkg_smoke_install.sh --explicit-start-check` 能在 artifact 目录落盘 `tools-installed-proof.json` 与 `services-installed-proof.json`；release runner workflow 能固定 `services-soak` 目录并归档；本地 direct-binary soak 可稳定复验 `remote_timeout` / `subscription_overflow`；本轮不把 qemu / machine isolation 当作 services closeout 前置。

## 2. 本地证据

1. `scripts/packaging/services_local_installed_proof.sh` 新增为 services own 的 installed proof owner：它消费 `tools-installed-proof.json`，校验 `agent.dataset` / `agent.terminal` 可见性、builtin route、payload marker、confirmation gate、bridge / observability marker，并落盘 `services-installed-proof.json`。
2. `scripts/packaging/pkg_smoke_install.sh` 现已在生成 `tools-installed-proof.json` 后调用 `services_local_installed_proof.sh`；因此 package-smoke artifact 目录成为 services installed evidence 的 authoritative owner，而不是继续依赖人工从 tools proof 侧推断。
3. `scripts/packaging/services_subscription_adapter_soak.sh` 新增为 release-runner local soak owner：它重复执行 `build/vscode-linux-ninja/tests/integration/services/dasall_services_failure_integration_test`，聚焦 `remote_timeout` 与 `subscription_overflow` 两个长期稳定性切片，并落盘 `services-soak-summary.json`。
4. `.github/workflows/release-package-gate.yml` 现已显式创建 `services-soak` 目录、构建 `dasall_services_failure_integration_test`、执行 `services_subscription_adapter_soak.sh`，并把 package-smoke / services-soak artifact 一并归档。
5. `scripts/packaging/README.md` 已把 Capability Services 的 local authoritative owner 固定为 `package-smoke/services-installed-proof.json` 与 `services-soak/services-soak-summary.json`，同时声明 qemu / machine isolation 继续属于 packaging / release hardening。
6. 本轮本机验证已实际生成以下 artifact：
   - `/tmp/dasall-capsrv-fix-008-smoke/tools-installed-proof.json`
   - `/tmp/dasall-capsrv-fix-008-smoke/services-installed-proof.json`
   - `/tmp/dasall-capsrv-fix-008-soak/services-soak-summary.json`
   - `/home/gangan/dasall-cli_0.1.0-1_amd64.deb`
   - `/home/gangan/dasall-daemon_0.1.0-1_amd64.deb`
   - `/home/gangan/dasall-common_0.1.0-1_all.deb`
   - `/home/gangan/dasall_0.1.0-1_all.deb`

## 3. 根因与收口策略

1. `CAPSRV-GAP-008` 的根因不是 services 缺少更多产品代码，而是 installed / release local 证据没有明确 owner。此前已有 tools installed proof 能走通 `IToolManager -> builtin -> services`，但 services 自身没有把这条链路沉淀成单独 artifact。
2. 本轮选择最小根因修复：installed 侧不再重复发明第二条 production 执行路径，而是把现有 `tools-installed-proof.json` 作为 source proof，由 services own 的脚本重新校验并固定为 `services-installed-proof.json`。
3. soak 侧同样不新造 harness，而是复用已有 `CapabilityServicesFailureIntegrationTest` 作为 authoritative local binary，把 `remote_timeout` / `subscription_overflow` 两个切片重复运行并落盘 summary。
4. qemu / machine isolation 继续保留给 packaging / release hardening；它不是本轮 `CAPSRV-FIX-008` 的 closeout owner，也不再作为用户当前任务的前置条件。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | services 必须拥有 installed positive-path artifact owner | `scripts/packaging/services_local_installed_proof.sh` + `scripts/packaging/pkg_smoke_install.sh` |
| D2 | release-runner local soak 必须固定为可归档证据 | `scripts/packaging/services_subscription_adapter_soak.sh` + `.github/workflows/release-package-gate.yml` |
| D3 | packaging contract 必须写清 services local authoritative owner | `scripts/packaging/README.md` |
| D4 | 本轮收口不依赖 qemu / kvm | deliverable、总账、worklog 与验收命令均使用本机 installed smoke / direct-binary soak |

## 5. Build 三件套

1. 代码目标：新增 services installed proof / release local soak 两个 owner 脚本，并把 package-smoke 与 release runner workflow 接到这两个 owner 上。
2. 测试目标：package-smoke artifact 必须包含 `services-installed-proof.json`；release local soak artifact 必须包含 `services-soak-summary.json`；二者都要能从现有本机命令稳定复验。
3. 验收命令：
   - `dpkg-buildpackage -us -uc -b`
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-capsrv-fix-008-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
   - `cmake --build build/vscode-linux-ninja --target dasall_services_failure_integration_test`
   - `bash scripts/packaging/services_subscription_adapter_soak.sh --artifact-dir /tmp/dasall-capsrv-fix-008-soak --build-dir build/vscode-linux-ninja --iterations 5`

## 6. Build 原子清单

| B 项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 新增 services installed proof owner | source tools proof 缺字段时 fail-closed；成功时落盘 `services-installed-proof.json` | `sh -n scripts/packaging/services_local_installed_proof.sh`；`bash scripts/packaging/services_local_installed_proof.sh --help` | 若 source proof schema 变化，应回到 tools proof owner 同步调整，而不是在 services 侧做宽松兼容 |
| B2 | 在 package-smoke 中接入 services proof owner | installed smoke 完成后 artifact 目录出现 `services-installed-proof.json` | `sh -n scripts/packaging/pkg_smoke_install.sh`；`DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-capsrv-fix-008-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | 若 package-smoke 未设置 artifact dir，则 services proof 不会落盘；这属于调用方 contract，不做隐式兜底 |
| B3 | 新增 release local soak owner 并接入 workflow | 直接 binary soak 与 workflow step 均能产出 `services-soak-summary.json` | `sh -n scripts/packaging/services_subscription_adapter_soak.sh`；`cmake --build build/vscode-linux-ninja --target dasall_services_failure_integration_test`；`bash scripts/packaging/services_subscription_adapter_soak.sh --artifact-dir /tmp/dasall-capsrv-fix-008-soak --build-dir build/vscode-linux-ninja --iterations 5` | soak 只覆盖 `remote_timeout` / `subscription_overflow`，不把它外推为任意外部 endpoint 全量稳定性 |
| B4 | 回写 packaging contract、总账、deliverable 与 worklog | 文档中的 owner、artifact 名称、验收命令与实测结果保持一致 | `rg -n "services-installed-proof|services-soak-summary|CAPSRV-FIX-008" scripts/packaging/README.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/services/deliverables/CAPSRV-FIX-008-installed-release-services-evidence收口.md docs/worklog/DASALL_开发执行记录.md` | 若文档与实现不一致，以本轮已验证 artifact 名称和命令为准回写 |

## 7. 验证

1. 语法与静态检查。
   - `sh -n scripts/packaging/services_local_installed_proof.sh`：通过。
   - `sh -n scripts/packaging/services_subscription_adapter_soak.sh`：通过。
   - `sh -n scripts/packaging/pkg_smoke_install.sh`：通过。
   - `.github/workflows/release-package-gate.yml` 编辑后无 YAML 错误。
2. package build。
   - `dpkg-buildpackage -us -uc -b`：通过，本机已生成 `dasall-cli_0.1.0-1_amd64.deb`、`dasall-daemon_0.1.0-1_amd64.deb`、`dasall-common_0.1.0-1_all.deb`、`dasall_0.1.0-1_all.deb`、`.changes` 与 `.buildinfo`。
3. release local soak。
   - `cmake --build build/vscode-linux-ninja --target dasall_services_failure_integration_test`：通过。
   - `bash scripts/packaging/services_subscription_adapter_soak.sh --artifact-dir /tmp/dasall-capsrv-fix-008-soak --build-dir build/vscode-linux-ninja --iterations 5`：通过。
   - 生成 `iteration-01.log` ~ `iteration-05.log`、`services-soak-summary.json`、`status.txt`；summary 中 `covered_slices=["remote_timeout","subscription_overflow"]`、`all_passed=true`、`release_runner_local_artifact_ready=true`。
4. installed package smoke。
   - `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-capsrv-fix-008-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`：通过，日志出现 `[pkg-smoke-install] install smoke passed`。
   - artifact 目录包含 `tools-installed-proof.json`、`services-installed-proof.json`、`source-tools-installed-proof.json`、`status.txt` 以及 package-smoke 既有 Knowledge / Memory / run 证据文件。
5. installed services proof 关键字段。
   - `effective_profile_id="desktop_full"`
   - `visible_tools=["agent.dataset","agent.terminal"]`
   - `data_route_kind="builtin"`
   - `terminal_route_kind="builtin"`
   - `service_payload_evidence_present=true`
   - `service_confirmation_gate_present=true`
   - `service_projection_evidence_present=true`
   - `service_route_citation_present=true`
   - `service_tool_call_citation_present=true`
   - `service_bridge_evidence_present=true`
   - `service_observability_evidence_present=true`
   - `tool_to_services_adapter_backend_path_present=true`
   - `terminal_failure_reason_code="policy.confirmation_required"`
6. installed tools proof 关键字段。
   - `ok=true`
   - `route_kind="builtin"`
   - `terminal_route_kind="builtin"`
   - `agent_dataset_visible=true`
   - `agent_terminal_visible=true`
   - `tool_invocation_succeeded=true`
   - `terminal_invocation_succeeded=true`
   - `terminal_confirmation_denied=true`
   - `projection_present=true`
   - `terminal_projection_present=true`
   - `production_bridge_evidence_present=true`
   - `production_observability_evidence_present=true`

## 8. 结果

1. `CAPSRV-GAP-008` 已在当前树按用户约束收口：Capability Services 现在拥有明确的 installed artifact owner 与 release-runner local soak owner，不再只停留在 build-tree focused tests。
2. package-smoke 侧现在可以独立证明 `IToolManager -> builtin -> services -> adapter/backend` 正向链路；release local 侧现在可以固定 `remote_timeout` / `subscription_overflow` 的重复 soak 结果并归档到 workflow artifact。
3. 本轮关闭的是 services local evidence gap，而不是 qemu / machine isolation gate；更高层 release hardening 仍由 packaging / release workflow 持续负责。

## 9. 风险与边界

1. `services-installed-proof.json` 明确依赖 `tools-installed-proof.json` 作为 source proof；这符合当前支持路径“Tools 为上游 ingress owner”。若未来新增非 Tools 的 production ingress，应单独开任务建立新的 services owner，而不是在本轮偷扩入口语义。
2. `services-soak-summary.json` 当前只覆盖 `remote_timeout` / `subscription_overflow` 两个 failure slice；它不替代真实外部 endpoint 的发现、鉴权和长期环境稳定性验证。
3. 本轮禁止使用 qemu / kvm，因此不宣称 machine-isolated installed-package gate 已通过，也不把 local host 结果外推成 packaging release-ready。

## 10. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 installed proof owner、release local soak owner、packaging contract 与验收命令。
3. Build 三件套已由本机 package build、installed smoke 与 direct-binary soak 完成验证，且未使用 qemu / kvm。
4. 结论：`CAPSRV-FIX-008` 可以标记为 Done。