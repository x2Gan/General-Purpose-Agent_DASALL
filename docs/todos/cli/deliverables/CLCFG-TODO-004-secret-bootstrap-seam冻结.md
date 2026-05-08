# CLCFG-TODO-004 冻结 bootstrap-only secret import seam 与 `secret://` 投影契约

状态：Done
日期：2026-05-08
来源 TODO：docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md

## 1. 任务边界

1. 本任务只冻结 P1 LLM onboarding 所需的 bootstrap-only secret import seam、`auth_ref` / `secret://` projection 命名和 install-mode file backend root 契约。
2. 本任务不提前实现 `SecretBootstrapWriter`、`LLMSecretPage`、`SecretRefResolver` 或对应 tests，只为 CLCFG-TODO-016、021 和 infra/config 后续实现提供不可再漂移的设计输入。
3. 本任务必须保持 `ISecretManager` 公共接口为 consumer-facing 读/轮换面，不把 bootstrap `create/set` 能力并入现有 ABI。

## 2. 当前冲突与需要收敛的口径

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 在本任务开始前已经确认 P1 不能把明文 secret 写入 `daemon.json` 或 provider baseline 资产，但 bootstrap writer 的 owner、返回结果和失败回滚语义还没有形成单点契约。
2. `docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md` 已冻结 `ISecretManager`、`SecretManagerFacade`、`FileSecretBackend` 与 install-mode file backend root 基线，却还没有显式接受“bootstrap 写入只允许 internal seam”的跨子系统约束。
3. `docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md` 的 `CFG-BLK-003` 仍把 `secret://` projection 当作 secret 接口未冻结的 blocker，导致后续 `SecretRefResolver` 设计无法从 Blocked 恢复到 build-ready。

## 3. 冻结结论

### 3.1 owner 与能力边界

1. bootstrap 写入面固定为 `infra/secret` internal `SecretBootstrapWriter` 或等价 `ISecretProvisioningTransaction`，不允许挂到 `apps/cli` 页面层，也不允许扩张 `ISecretManager` 公共 ABI。
2. `LLMSecretPage` 只负责收集 provider ref、masked secret input、可选 auth profile 名称，并把 `SecureBuffer` 交给 internal seam；页面层不得自己拼接、写入或缓存 secret 文件。
3. 初始导入成功后的后续消费固定继续复用 `ISecretManager`、`SecretManagerFacade` 与 `FileSecretBackend` 读链，不重新发明第二条 provider secret 访问通道。

### 3.2 输入/输出与 projection 规则

1. bootstrap seam 的最小返回结果固定为 `SecretProvisioningResult{auth_ref, backend_root, provisioning_state}`。
2. `auth_ref` 命名固定为 `secret://llm/providers/<provider_ref>`；provider baseline 资产与 `daemon.json` 只允许保存该 redacted ref，不允许保存 secret 明文或第二套本地路径。
3. install-mode file backend root 固定映射为 `/var/lib/dasall/secrets`；build-tree 或 dev-mode 如需不同目录，只能通过部署层 `infra.secret.file.root_dir` 投影表达，不能再发明第二个安装态默认值。

### 3.3 失败与 summary 语义

1. 导入流程若在提交前失败，不得留下半成品 `auth_ref`；若已创建 candidate record，bootstrap seam 必须显式 revoke/remove，再把页面状态保持为 `missing`。
2. summary、日志、审计只显示 redacted ref 与状态，例如 `secret://llm/providers/deepseek-prod (configured)`，不得回显 secret 明文。
3. `configured` 与 `runtime_verified` 必须分离；bootstrap 成功只代表 ref 已可供后续消费，不代表 provider runtime 已完成真实可用性验证。

## 4. 对后续 Build 的直接约束

1. CLCFG-TODO-016 若实现 `SecretBootstrapWriter`，只能把它落到 `infra/src/secret/` internal surface，不得把 `create/set` 扩到 `infra/include/secret/ISecretManager.h`。
2. CLCFG-TODO-016 的 `LLMSecretPage` 只能通过 masked prompt、stdin 或 owner-only import-file 收集输入，并把结果映射为 `SecretProvisioningResult` 与 redacted summary。
3. infra/config 后续的 `SecretRefResolver` 必须直接消费 `secret://llm/providers/<provider_ref>` 投影规则，不再把 `CFG-BLK-003` 当作“secret 接口仍未冻结”的借口。
4. focused tests 应围绕 `FileSecretBackend` / `SecretManagerFacade` / `SecretBootstrapWriterIntegrationTest` 的导入、回滚与 redacted summary 事实编写，而不是围绕临时命名或页面层自管文件写入。

## 5. 验证口径

1. 设计验收使用以下命令：

   `rg -n "SecretBootstrapWriter|secret://|auth_ref|bootstrap-only|CFG-BLK-003|FileSecretBackend|SecretManagerFacade" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-004-secret-bootstrap-seam冻结.md`

2. 通过标准：
   - secret owner、config owner 与 config-center 对 bootstrap seam 的 owner 边界形成唯一口径。
   - `auth_ref=secret://llm/providers/<provider_ref>` 与 install-mode root=`/var/lib/dasall/secrets` 不再出现第二套默认值。
   - `CFG-BLK-003` 与 `CLCFG-BLK-002` 都不再依赖口头说明“以后会有写入 API”，后续实现可直接消费冻结后的投影契约。