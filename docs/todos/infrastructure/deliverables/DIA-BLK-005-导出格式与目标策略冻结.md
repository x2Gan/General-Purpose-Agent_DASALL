# DIA-BLK-005 导出格式与目标策略冻结

日期：2026-04-07  
任务：DIA-BLK-005  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md) 将 `DIA-TODO-020` 标记为 `Blocked`，根因明确为“导出格式、checksum 规则与 remote allowed_targets 白名单未冻结”。
2. [infra/include/diagnostics/DiagnosticsTypes.h](infra/include/diagnostics/DiagnosticsTypes.h) 已固定 `SnapshotExportRequest` / `SnapshotExportResult` 字段边界，以及 `ExportTarget::{LocalFile,RemoteUpload}`、`ExportFormat::{Json,TextArchive}` 两个枚举，这意味着 005 不能靠扩写公共对象来补洞，只能在 diagnostics 私有设计里冻结 v1 语义。
3. [infra/src/diagnostics/DiagnosticsServiceFacade.cpp](infra/src/diagnostics/DiagnosticsServiceFacade.cpp) 的 `export_snapshot()` 仍是 placeholder：只拒绝非本地 target，并返回硬编码 checksum；如果不先冻结 format/checksum/target 规则，020 只会把这些占位值固化成实现偶然性。
4. [tests/integration/infra/InfraDiagnosticsSmokeTest.cpp](tests/integration/infra/InfraDiagnosticsSmokeTest.cpp) 与 [tests/unit/infra/DiagnosticsSnapshotExportTest.cpp](tests/unit/infra/DiagnosticsSnapshotExportTest.cpp) 已经把 v1 最小导出路径锚定在 `LocalFile + ExportFormat::Json`，说明 blocker recovery 应在现有对象边界上细化导出语义，而不是重开对象设计。

## 2. 外部参考

1. JSON Lines 官方文档明确指出：该格式要求 UTF-8 编码、每一行都是合法 JSON value，并以 `\n` 作为行终止符，常用文件扩展名是 `.jsonl`；本轮据此把 diagnostics v1 的本地导出载体收敛为 UTF-8 JSON Lines，而不是模糊的“任意 JSON 文件”。
   - https://jsonlines.org/
2. Python `hashlib` 文档总结了 SHA-256 的通用可用性，并明确提醒 MD5/SHA1 存在已知碰撞弱点；本轮据此把 diagnostics v1 的导出摘要固定为 `sha256:<64 lowercase hex>`，避免为校验字段引入弱摘要歧义。
   - https://docs.python.org/3/library/hashlib.html
3. Azure Architecture Center 的 Gatekeeper pattern 强调应先做 validation/sanitize，再决定是否把请求交给受信后端；本轮据此把 remote `allowed_targets` 收敛为 exact-match allow-list，而不是允许 prefix/wildcard 宽松放行。
   - https://learn.microsoft.com/en-us/azure/architecture/patterns/gatekeeper

## 3. 阻塞修复与设计结论

阻塞分类：

1. `DIA-BLK-005` 属于 context blocker：`SnapshotExportRequest` / `SnapshotExportResult` 已冻结，但 `ExportFormat::Json` 究竟表示什么、checksum 如何编码、remote target 允许多宽，仍缺乏权威边界，直接实现 020 会把设计决策偷偷埋进代码。

最小 blocker-fix：

1. 在 diagnostics 详细设计中新增专门章节，冻结 `ExportFormat::Json`、`ExportFormat::TextArchive`、`checksum` 和 local/remote `target_ref` allow-list 规则。
2. 明确 v1 只有 `LocalFile + Json` 能成功，且 `Json` 在 diagnostics 模块内固定映射为 UTF-8 JSON Lines（`.jsonl`）。
3. 明确 remote 只接受 exact-match `https://` endpoint ref 且默认禁用；未启用前必须返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`，而不是试探性访问网络目标。

设计结论：

1. diagnostics v1 不新增新的公开导出枚举；继续沿用 `ExportTarget` / `ExportFormat`，但把 `ExportFormat::Json` 的模块语义冻结为 UTF-8 JSON Lines 输出。
2. v1 本地导出唯一允许成功的组合是：`request.target == LocalFile` 且 `request.format == Json`。
3. 本地 `request.target_ref` 固定匹配 `local://diagnostics/<artifact_name>.jsonl`，其中 `<artifact_name>` 仅允许 `[a-z0-9._-]{1,128}`；不允许 query、fragment、`..`、重复斜杠或其他 scheme。
4. `ExportFormat::TextArchive` 在 diagnostics v1 保留枚举值但必须返回 `INF_E_DIAG_EXPORT_FAIL`；任何 tar/zip/text bundle 语义都需要新的 design gate。
5. `SnapshotExportResult.checksum` 固定为对“最终写出的确切字节串”计算 `sha256`，并以 `sha256:<64 lowercase hex>` 返回；不接受 `md5`、`sha1`、无算法前缀或非 hex 摘要。
6. `infra.diagnostics.remote.enabled=false` 时，任何 `RemoteUpload` 请求必须在发起网络 IO 前返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`。
7. 若未来显式启用远程导出，`request.target_ref` 仍必须与 `infra.diagnostics.remote.allowed_targets` 中的某个条目 exact match；条目固定为完全限定的 `https://` endpoint ref，不允许 wildcard、prefix match、query、fragment 或内嵌凭据。

### 3.1 target / format / gate 矩阵

| target | format | v1 结果 | 约束 |
|---|---|---|---|
| `LocalFile` | `Json` | Success | `target_ref` 命中 `local://diagnostics/<artifact_name>.jsonl`；序列化为 UTF-8 JSON Lines；checksum=`sha256:<64hex>` |
| `LocalFile` | `TextArchive` | Reject | 返回 `INF_E_DIAG_EXPORT_FAIL` |
| `RemoteUpload` | `Json` | Deny by default | `remote.enabled=false` 时返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`；启用后仍需 exact-match `allowed_targets` |
| `RemoteUpload` | `TextArchive` | Reject | v1 不支持 |

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `ExportFormat::Json -> UTF-8 JSON Lines` | `infra/src/diagnostics/ExportManager.cpp` 只实现 `.jsonl` 本地导出，不再猜测普通 `.json` 或 archive 语义 |
| 冻结 `checksum=sha256:<64hex>` | `DiagnosticsExportTest` 直接断言 checksum 前缀与 64 位 hex 形状，防止占位字符串继续存活 |
| 冻结本地 `target_ref` 规则 | `ExportManager` 对 `local://diagnostics/<artifact_name>.jsonl` 做严格校验，拒绝路径跳转与非法 suffix |
| 冻结 remote default deny + exact-match allow-list | 020 实现中把 `RemoteUpload` 请求先过 `remote.enabled` 与 `allowed_targets` gate，再决定是否进入远程逻辑 |
| 冻结 `TextArchive` 为 v1 unsupported | `DiagnosticsExportTest` / failure path 新增 `TextArchive` 拒绝断言，避免后续偷偷把 archive 做成无规范实现 |

## 5. 对 DIA-TODO-020 的直接交接

1. `DIA-TODO-020` 可以从 `Blocked` 转为 `Not Started`，并按 diagnostics 详细设计 6.5.4 直接实现 `ExportManager` 骨架。
2. 020 的最小完成边界应包括：
   - `LocalFile + Json` 成功输出 UTF-8 JSON Lines，并返回 `sha256:<64 lowercase hex>` checksum；
   - `TextArchive` 在 v1 明确拒绝；
   - `RemoteUpload` 在 `remote.enabled=false` 时明确返回 `INF_E_DIAG_REMOTE_EXPORT_DISABLED`；
   - `target_ref` 非法、snapshot 不存在、checksum/写出失败时返回可判定错误码。
3. 020 不得顺手放开 wildcard/prefix remote target，也不得把 checksum 退化为占位字符串或弱摘要算法。

## 6. Build 三件套

1. 代码目标：更新 diagnostics 详细设计、diagnostics 专项 TODO、infrastructure 总 TODO 和 worklog，并新增 blocker deliverable。
2. 测试目标：执行 process validation，确认 6.5.4 与 `DIA-BLK-005` / `DIA-TODO-020` 的台账状态一致。
3. 验收命令：
   - `rg -n "### 6.5.4|sha256:<64hex>|local://diagnostics/<artifact_name>.jsonl|D-BLK-03 已解阻" docs/architecture/DASALL_infra_diagnostics模块详细设计.md`
   - `rg -n "DIA-BLK-005|DIA-TODO-020|Not Started|已解阻" docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`

## 7. 风险与回退

1. 若后续实现把 `ExportFormat::Json` 重新解释成“普通单对象 `.json` 文件”，会直接破坏本轮 blocker fix，并让 diagnostics 导出格式重新失去可验证边界。
2. 若 020 或后续迭代把 remote allow-list 从 exact-match 放宽成 prefix/wildcard，会显著扩大导出攻击面，必须重新进入 blocker 状态。
3. 若实现继续保留硬编码 checksum 或接受 `md5` / `sha1`，会让 `SnapshotExportResult.checksum` 失去稳定语义，直接回退本轮设计结论。