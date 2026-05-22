# INF-FIX-009 secret consumer matrix 与 package 证据

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-009`。
2. 本轮目标：建立 single-source secret consumer matrix，明确 Access ownership HMAC、LLM provider `auth_ref`、OTA/Plugin trust anchor、Config bootstrap writer 四类 consumer 的 owner、读写路径、profile flag、package asset 与不可外推边界；同时补齐本机 installed-package 的 redacted secret proof，避免继续把 focused secret 读链外推为 production/package ready。
3. 用户附加约束：按 `project-implementation-cycle` 串行推进；逐文件落盘；若验收口径包含 qemu / kvm 必须替换为 build-tree 或本机 installed-package 口径；完成后需提交并推送；本轮不得把 qemu/autopkgtest 证据混入完成判定。

## 2. 本地证据

1. `INF-FIX-007` 已冻结 `secret/SecretManagerLiveComposition.h/.cpp`，`RuntimeDependencySet::secret_manager` 现在能在 daemon / gateway shared runtime composition 中保留 live `ISecretManager` seam。
2. `INF-FIX-008` 已在 daemon / gateway app composition root 注入 `ownership_secret_manager`，`AsyncTaskRegistryMissingSecretFailClosedTest` 也已证明 Access ownership HMAC 在 secret 缺失时保持 fail-closed。
3. `infra/include/secret/ISecretManager.h` 当前只暴露 `get_secret/materialize/release/rotate/revoke/inspect` 六个 consumer-facing 方法，没有 bootstrap create/set；该边界已由 `tests/contract/smoke/SecretManagerInterfaceBoundaryContractTest.cpp` 守住“不得吸收 backend/health protocol”，但还没有显式把 bootstrap create/set/provision 排除写成 guard。
4. `infra/src/secret/SecretBootstrapWriter.cpp` 与 `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 已冻结 bootstrap-only secret import seam：writer 属于 `infra/secret` internal seam，install-mode root 固定 `/var/lib/dasall/secrets`，成功导入仅返回 redacted `auth_ref=secret://llm/providers/<provider_ref>`。
5. `llm/assets/providers/deepseek/manifest.yaml` 已把 provider secret 固定为 `auth_ref: secret://llm/providers/deepseek-prod`；`llm/src/LLMSubsystemConfig.cpp` 与 `llm/src/transport/CurlCommandLLMTransport.cpp` 当前都只接受 `secret://` / `profile://` reference，而不是明文 key。
6. `infra/src/ota/PackageVerifier.cpp` 与 `infra/include/plugin/IPluginSignatureVerifier.h` 已把 OTA / Plugin trust anchor purpose 冻结为 `ota.package.verify` / `plugin.package.verify`，并明确两者只读 trust anchor，不负责持久化、轮换或 bootstrap 写入。
7. `scripts/packaging/pkg_smoke_install.sh` 已具备本机 installed-package authoritative smoke：可以 fresh reinstall、restore/import `secret://llm/providers/deepseek-prod`、显式启动 daemon、运行 `dasall run`、并在 `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR` 下输出 runtime/memory/tools proof artifact；当前缺口是没有单独的 redacted secret consumer/package proof artifact，也没有把 secret consumer matrix 对应的 installed asset/projection 口径收口成单点证据。

## 3. 外部参考

1. OWASP Secrets Management Cheat Sheet 强调 secret consumption 应通过 centralized / standardized access、least privilege 与审计链路完成，consumer 不应私自扩写写入接口，也不应把 secret 复制进静态资产或配置文件。
2. HashiCorp Vault `operator init` / bootstrap 实践表明：bootstrap secret initialization 是受控的单独初始化面，不应混入普通查询 API；初始化状态与运行时读取状态也不应混写为同一个 ready 结论。

## 4. 设计结论

### 4.1 需要被统一矩阵化的 secret consumer 边界

1. Access ownership HMAC 是 app owner 驱动的 deployment secret consumer：由 daemon / gateway owner 注入 `ownership_secret_manager`，Access 只消费 `ownership_token_hmac_secret_ref` 对应 secret，缺 secret 时必须 fail-closed。
2. LLM provider `auth_ref` 是 asset + transport 联合 consumer：provider manifest / overlay 只保存 redacted `secret://` 或 `profile://` 引用，真正 materialize 只发生在 transport/secret backend 读链，不发生在 provider asset 或 adapter config bootstrap。
3. OTA / Plugin trust anchor 只属于 verify-time read path：两者都通过固定 `anchor_purpose` 读取 trust anchor，不拥有 bootstrap、轮换、撤销或持久化职责。
4. Config bootstrap writer 不是 `ISecretManager` 的对外 consumer-facing API 扩展，而是 `infra/secret` 内部 bootstrap-only write seam；它只负责一次性导入和 redacted ref 投影，不能被回写成 `ISecretManager.create/set`。

### 4.2 package 证据边界

1. 本轮 package evidence 只采用本机 installed-package authoritative smoke，不使用 qemu / kvm。
2. package evidence 需要至少证明三件事：
   - 安装后的文档 / provider 资产中仍然只保留 redacted secret ref，而不是明文 secret。
   - installed `config apply` / preserved secret restore 最终落到 `/var/lib/dasall/secrets/...` 的 file-backed record，且权限为 owner/group 受控。
   - artifact 里能区分“bootstrap write path”“runtime consumer read path”“package asset presence”，避免把任一局部证据外推成四类 consumer 全部 installed-ready。
3. OTA / Plugin trust anchor 在本轮没有新的 installed-package active verify artifact，因此 matrix 必须把它们标为“有 code/design owner，但无新增 local installed package proof；禁止外推为 package-ready”。

### 4.3 guard 结论

1. `ISecretManager` boundary contract 必须显式拒绝 create/set/bootstrap/provision/import 之类写入方法，防止后续把 Config bootstrap seam 倒灌进 consumer-facing ABI。
2. 该 guard 应收敛在现有 `SecretManagerInterfaceBoundaryContractTest`，而不是另起一套平行 secret ABI test，避免重复定义接口边界。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | secret consumer owner / path / package asset / non-extrapolation 需要单点 SSOT | `docs/ssot/SecretConsumerMatrix.md` |
| D2 | `ISecretManager` 不得吸收 bootstrap create/set/provision | `tests/contract/smoke/SecretManagerInterfaceBoundaryContractTest.cpp` |
| D3 | installed-package 需要 redacted secret consumer proof artifact 与 asset probe | `scripts/packaging/pkg_smoke_install.sh` |
| D4 | infrastructure 任务、交付物与工作日志必须把 009 的 matrix/proof 一起回写 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md`、本交付物 |

## 6. Build 三件套

1. 代码目标：
   - 新增 secret consumer SSOT，固定四类 consumer 的 owner、读写路径、profile flag、package asset 与不可外推规则。
   - 扩展 secret boundary contract，显式禁止 `ISecretManager` 暴露 create/set/bootstrap/provision/import 写入方法。
   - 扩展本机 installed package smoke，产出 redacted secret consumer/package proof artifact，并校验安装包中的矩阵文档与 provider auth_ref 资产。
2. 测试目标：
   - `SecretManagerInterfaceBoundaryContractTest`
   - `AsyncTaskRegistryMissingSecretFailClosedTest`
   - 本机 installed package smoke：`scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_contract_secret_manager_interface_boundary_test","dasall_access_async_task_registry_missing_secret_fail_closed_unit_test"])`
   - `RunCtest_CMakeTools(tests=["SecretManagerInterfaceBoundaryContractTest","AsyncTaskRegistryMissingSecretFailClosedTest"])`；若继续命中仓库已知泛化“生成失败”，按 fallback 直接执行 `build/vscode-linux-ninja/tests/contract/dasall_contract_secret_manager_interface_boundary_test` 与 `build/vscode-linux-ninja/tests/unit/access/dasall_access_async_task_registry_missing_secret_fail_closed_unit_test`
   - `run_task(workspaceFolder="/home/gangan/DASALL", id="shell: copilot-rt-fix-006-rebuild-deb")`
   - `run_task(workspaceFolder="/home/gangan/DASALL", id="shell: copilot-rt-fix-006-package-smoke")`

## 7. 实施结果

1. 新增 `docs/ssot/SecretConsumerMatrix.md`：把 Access ownership HMAC、LLM provider `auth_ref`、OTA trust anchor、Plugin trust anchor 与 Config bootstrap writer 统一收口到同一张 owner/path/package asset/non-extrapolation 矩阵，不再让四类 secret consumer 继续散落在 deliverable、worklog 和脚本说明中。
2. 扩展 `tests/contract/smoke/SecretManagerInterfaceBoundaryContractTest.cpp`：在既有 backend/health-probe boundary guard 之外，新增 create/set/create_secret/set_secret/provision/provision_secret/import_secret/bootstrap_secret 八类写入方法的 compile-time 否定断言，显式守住 `ISecretManager` 不得吸收 bootstrap-only write seam。
3. 扩展 `scripts/packaging/pkg_smoke_install.sh`：
   - 新增安装态 asset probe，校验 `/usr/share/dasall/docs/ssot/SecretConsumerMatrix.md` 与 `/usr/share/dasall/llm/providers/deepseek/manifest.yaml` 存在。
   - 新增 redacted `secret-consumer-package-proof.json` artifact，固定 matrix doc path、provider manifest `auth_ref`、secret record owner/group/mode、bootstrap provisioning mode 与 explicit non-extrapolation 说明。
   - 当 smoke 通过 `DASALL_DEEPSEEK_API_KEY_FILE` 走 import 路径时，脚本现在会捕获 `dasall config apply --json` 输出并检查 `written_secret_refs`；当走 preserved secret record copy 路径时，则显式把证据标记为 `preserved_secret_record_copy`，避免把“已有 record 复用”误写成“本轮做了 bootstrap import”。

## 8. 验收结果

1. `Build_CMakeTools(buildTargets=["dasall_contract_secret_manager_interface_boundary_test","dasall_access_async_task_registry_missing_secret_fail_closed_unit_test"])`：通过。
2. `RunCtest_CMakeTools(tests=["SecretManagerInterfaceBoundaryContractTest","AsyncTaskRegistryMissingSecretFailClosedTest"])`：仍命中仓库已知泛化“生成失败”。
3. fallback direct binaries：
   - `build/vscode-linux-ninja/tests/contract/dasall_contract_secret_manager_interface_boundary_test`
   - `build/vscode-linux-ninja/tests/unit/access/dasall_access_async_task_registry_missing_secret_fail_closed_unit_test`
   - 结果：2 个 binary 均退出 `0`。
4. `run_task(... id="shell: copilot-rt-fix-006-rebuild-deb")` 后核验：新的 `dasall-common_0.1.0-1_all.deb` 已包含 `usr/share/dasall/docs/ssot/SecretConsumerMatrix.md`。
5. `run_task(... id="shell: copilot-rt-fix-006-package-smoke")`：通过，artifact 目录新增 `secret-consumer-package-proof.json`。
6. `secret-consumer-package-proof.json` 关键结果：
   - `matrix_doc_present=true`
   - `provider_manifest_auth_ref_line="auth_ref: secret://llm/providers/deepseek-prod"`
   - `bootstrap_provisioning_mode="preserved_secret_record_copy"`
   - `secret_record_path=/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret`
   - `secret_record_owner=root`、`secret_record_group=dasall`、`secret_record_mode=640`、`secret_root_mode=750`
   - explicit non-extrapolation 已写入 artifact：Access ownership HMAC 仍需显式 `ownership_token_hmac_secret_ref`，OTA/Plugin trust anchor 本轮无 local installed verify proof，bootstrap import/local DeepSeek smoke 不外推为 qemu / production key-management ready。

## 9. 风险与回退

1. 若直接把 bootstrap create/set 加进 `ISecretManager`，会打破既有 consumer-facing ABI 边界，并把 Config bootstrap 与 runtime read path 混成一条不受控写通道。
2. 若 package artifact 记录 raw path、明文 secret 或未区分 write/read/asset 三类证据，会重新制造“有 artifact 但无边界”的假阳性结论。
3. 若把 OTA / Plugin trust anchor 在本轮写成 installed/package ready，会越过当前本机安装态证据能力，错误外推到未验证的 verify-time 场景。

## 10. Gate 结论

1. D Gate：PASS。secret consumer owner/path/package evidence 的统一口径已经固定到 `SecretConsumerMatrix`，且不再允许 bootstrap 写入倒灌到 `ISecretManager`。
2. B Gate：PASS。boundary guard、local installed asset probe 与 redacted package proof artifact 已全部落盘并通过本机验证。
3. 结论：`INF-FIX-009` 已完成；infra 在 secret 方向上的 local installed boundary / matrix gap 已闭合，后续更高层 qemu/autopkgtest 与 trust anchor verify-time 复核继续留给 packaging / release 或各 consumer 自身任务，不回流为 infra owner blocker。