# SecretConsumerMatrix (Single Source of Truth)

关联任务：INF-FIX-009  
最近更新时间：2026-05-22  
适用阶段：infrastructure secret consumer boundary / local installed package evidence

## 1. 目标

本文件冻结 DASALL 当前 secret consumer matrix，统一以下口径：

1. 哪些 secret path 属于 runtime consumer read path。
2. 哪些 path 只是 bootstrap-only write seam。
3. 哪些配置键或资产是 package-installed truth source。
4. 哪些证据只能说明 local installed / focused / code-level ready，而不能外推为 package-ready 或 production-ready。

本文件不是新的 secret 设计文档，而是对现有 owner 与证据边界的单点收口，避免继续出现以下偏差：

1. 把 `SecretBootstrapWriter` 写入面混成 `ISecretManager` 公共 ABI。
2. 把 LLM provider `auth_ref` 资产、Access ownership HMAC、OTA / Plugin trust anchor、Config bootstrap import 混写成同一种 secret ready 结论。
3. 把 build-tree 或局部 fixture 的 secret 读链外推为 installed-package proof。

## 2. 全局 ownership 规则

### 2.1 live read owner

1. `ISecretManager` 是 infra 提供的 consumer-facing live read seam，只承载 `get_secret/materialize/release/rotate/revoke/inspect`。
2. runtime / apps 只允许消费该 seam 或其受控投影，不允许自行复制第二条 secret 读取通道。
3. consumer 文档只能说明“如何读取 / 投影 / fail-closed”，不能把 bootstrap write path 反向定义成 `ISecretManager` 新职责。

### 2.2 bootstrap write owner

1. bootstrap-only secret import 只属于 `infra/secret` internal seam，即 `SecretBootstrapWriter` 或其等价 internal transaction。
2. `dasall config apply`、CLI secret page 和 package install smoke 可以驱动 bootstrap write seam，但都不是 `ISecretManager` owner。
3. bootstrap write 成功后的对外稳定结果只有 redacted ref 与 file-backed record，不是“provider runtime verified”。

### 2.3 package evidence owner

1. 本轮 package evidence 只采信本机 installed-package authoritative smoke 与安装后的只读资产探针。
2. qemu / autopkgtest 仍属于 packaging / release 环境，不在本文件的完成判定范围内。
3. 若某 consumer 在本轮没有新的 local installed proof，必须明确标注“禁止外推”，不能因为已有 design / unit / integration owner 就写成 package-ready。

## 3. Consumer Matrix

| Consumer 类 | 具体 consumer | semantic owner | 读取路径 | 写入路径 | profile flag / activation | package asset / installed asset | 当前可采信证据 | 不可外推 |
|---|---|---|---|---|---|---|---|---|
| Access ownership HMAC | daemon / gateway accepted-async ownership token | daemon / gateway app composition owner；Access 只消费 receipt secret | `RuntimeDependencySet::secret_manager` -> `AccessOwnershipSecretWiring` -> `ownership_secret_manager` -> `AccessGatewayFactory` / `AsyncTaskRegistry` | 无；Access 不负责 bootstrap write | 无 profile flag；只消费 bootstrap field `ownership_token_hmac_secret_ref` | 默认安装包 `daemon.json` 不自带 ownership secret；该 secret 只在部署 bootstrap config 中声明 | `AsyncTaskRegistryMissingSecretFailClosedTest`、`DaemonAccessSecretCompositionTest`、`GatewayAccessSecretCompositionTest` | 不代表所有安装态默认已启用 ownership HMAC；未声明 `ownership_token_hmac_secret_ref` 的安装仍可能合法禁用 |
| LLM provider `auth_ref` | provider manifest / runtime transport secret ref | LLM provider asset + transport 读链；真正 materialize 由 secret backend 完成 | `llm/assets/providers/*/manifest.yaml` -> `ProviderCatalogRepository` -> `LLMSubsystemConfig` -> adapter / transport -> `ISecretManager` / backend | 无；LLM 不负责 secret 初始写入 | `auth_ref` 本身不是 profile key；启停由 provider overlay / route activation 决定 | `/usr/share/dasall/llm/providers/deepseek/manifest.yaml` 必须保留 `auth_ref: secret://llm/providers/deepseek-prod`；installed record 位于 `/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret` | provider asset parse / onboarding tests；local installed `pkg_smoke_install.sh` DeepSeek run；`secret-consumer-package-proof.json` | 不代表所有 provider family、所有 endpoint 或 L6 稳态都已验证 |
| OTA trust anchor | package signature verification anchor | OTA `PackageVerifier` | `PackageVerifier` -> `ITrustAnchorProvider.load_active_anchor("ota.package.verify", algorithm)` | 无；OTA 不负责 anchor 持久化、轮换或撤销 | `infra.ota.package.verify_required`、`infra.ota.package.signature_algorithm` | 本轮无新增 installed package asset proof；当前主要是 code/design owner | `PackageVerifierTest`、`OTAPackageVerifierInterfaceTest`、OTA 详细设计 | 不代表 local installed package 已完成真实 trust anchor verify proof |
| Plugin trust anchor | plugin signature verification anchor | plugin signature verifier | `IPluginSignatureVerifier` -> trust anchor purpose `plugin.package.verify` | 无；plugin 不负责 anchor 持久化、轮换或撤销 | `infra.plugin.trust.min_level`、plugin trust policy | 本轮无新增 installed package asset proof；当前主要是 code/design owner | plugin interface / design owner；plugin detailed design 6.6.1 | 不代表 installed plugin package verify 已在本轮本机 smoke 中运行 |
| Config bootstrap writer | initial secret onboarding / import | CLI config workflow + `infra/secret` internal bootstrap seam | 运行期后续消费仍回到 `FileSecretBackend` / `SecretManagerFacade` / `ISecretManager` 标准读链 | `dasall config apply` / wizard -> `SecretBootstrapWriter::import_secret()` -> `/var/lib/dasall/secrets` record + redacted `auth_ref` | 无 profile flag；由 desired state `secrets.refs[*]` 驱动 | `/usr/bin/dasall`、`/etc/default/dasall-daemon`、`/etc/dasall/daemon.json`、`/var/lib/dasall/secrets/...`、安装后的 provider manifest/doc assets | `SecretBootstrapWriterIntegrationTest`、CLI config integration tests、`debian/tests/pkg-smoke-local-control-plane`、local installed `pkg_smoke_install.sh` when import path or preserved record is present | bootstrap import 只证明导入与 redacted projection，不代表 provider runtime / remote endpoint / production key management ready |

## 4. package evidence contract

### 4.1 required local installed artifact

本轮 local installed package smoke 对 secret consumer 只要求以下 redacted artifact：

1. `secret-consumer-package-proof.json`
2. `run-first.json` / `run-second.json` 中出现 `llm.origin=deepseek-prod/`
3. 已存在的 runtime / memory proof artifact 继续作为相邻证据，但不替代 secret consumer artifact

### 4.2 `secret-consumer-package-proof.json` 最小字段

1. matrix 文档安装态存在：`/usr/share/dasall/docs/ssot/SecretConsumerMatrix.md`
2. provider manifest 安装态存在且 `auth_ref` 仍为 redacted `secret://llm/providers/deepseek-prod`
3. local installed secret record 路径、owner/group/mode
4. 本轮 secret provisioning mode：`config_apply_import` 或 `preserved_secret_record_copy`
5. 若本轮实际执行了 config import，则记录 `written_secret_refs` 是否命中 `secret://llm/providers/deepseek-prod`

### 4.3 explicit non-extrapolation rules

1. 该 artifact 不能说明 Access ownership HMAC 已在所有安装态默认启用；它只证明 matrix 文档、LLM provider asset、bootstrap/import record 与 local DeepSeek smoke 的一致性。
2. 该 artifact 不能说明 OTA trust anchor 或 Plugin trust anchor 已具 installed-package verify 证据；这两者在本轮仍停留于 code/design owner。
3. 该 artifact 不能说明 provider key rotation、remote KMS、multi-provider rollout 或 qemu/autopkgtest package gate 已闭合。

## 5. Guard 规则

1. `ISecretManager` boundary contract 必须继续显式拒绝 backend leak，同时额外拒绝 create/set/bootstrap/provision/import 这类 bootstrap write 方法进入公共 ABI。
2. `SecretBootstrapWriter` 只允许停留在 `infra/src/secret/` internal surface；若未来需要更复杂的 onboarding transaction，也只能在 internal seam 扩展，而不是在 `infra/include/secret/ISecretManager.h` 扩表。
3. consumer-specific matrix 若新增新行，必须同步补本文件、相关 deliverable 与至少一条 focused proof 或 non-extrapolation 说明。

## 6. Design -> Build 映射

| 设计决策 | Build / 验证落点 |
|---|---|
| 统一 secret consumer owner / path / package evidence 语义 | 本文件 §3 与 `INF-FIX-009` deliverable |
| bootstrap write 不得倒灌 `ISecretManager` | `SecretManagerInterfaceBoundaryContractTest` |
| local installed package 必须输出 redacted secret proof artifact | `scripts/packaging/pkg_smoke_install.sh` |
| infrastructure secret gap 必须以 matrix + proof 回写总账 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 7. 完成判定

当且仅当以下条件成立时，才允许将 `INF-FIX-009` 视为完成：

1. 本文件已落盘并明确 Access、LLM、OTA、Plugin、Config bootstrap 的 owner / read / write / package asset / non-extrapolation。
2. `ISecretManager` boundary guard 已显式阻止 bootstrap create/set/provision/import 进入公共 ABI。
3. local installed package smoke 已能产出 redacted `secret-consumer-package-proof.json`。
4. TODO / deliverable / worklog 已同步说明哪些证据是 local installed，哪些仍只是 code/design owner。