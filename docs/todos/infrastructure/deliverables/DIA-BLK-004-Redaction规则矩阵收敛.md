# DIA-BLK-004 Redaction 规则矩阵收敛

日期：2026-04-07  
任务：DIA-BLK-004  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md](docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md) 将 `DIA-TODO-018` 标记为 `Blocked`，根因明确为“字段分级、deny-list 与 redaction.profile 规则矩阵未冻结”。
2. [docs/architecture/DASALL_infra_diagnostics模块详细设计.md](docs/architecture/DASALL_infra_diagnostics模块详细设计.md) 已冻结 `DiagnosticsSnapshot` 的 `summary`、`evidence_refs`、`redaction_profile`、`exporter_hint` 边界，也明确主链必须“先脱敏，再存储，再导出”，但此前没有可执行的字段级矩阵。
3. [infra/include/diagnostics/DiagnosticsTypes.h](infra/include/diagnostics/DiagnosticsTypes.h) 对 `DiagnosticsSnapshot` 的公开字段已经冻结，这意味着 018 不能通过新增共享对象来逃避脱敏边界，只能在私有 `RedactionEngine` 中处理。
4. [infra/src/diagnostics/DiagnosticsServiceFacade.cpp](infra/src/diagnostics/DiagnosticsServiceFacade.cpp) 当前主链已经能产出真实 `Executor -> EvidenceCollector -> SnapshotAssembler` 输出；如果继续在未冻结 redaction 规则的情况下实现 store/export，只会把敏感字段泄露风险向后扩散。
5. [docs/worklog/DASALL_开发执行记录.md](docs/worklog/DASALL_开发执行记录.md) 的 `记录 #164` 已把 017 的剩余风险明确成“脱敏链路仍未落盘，assembler 当前直接组装 executor/evidence 输出”。

## 2. 外部参考

1. Microsoft Azure Monitor 的个人数据处理建议明确指出，优先策略应是“过滤、模糊化、匿名化或调整采集数据”，避免把个人或敏感数据直接落到日志/导出面；本轮据此把 diagnostics v1 的 redaction 原则收敛为“先脱敏，再落盘/导出”，且不允许 redaction fail 后继续持久化。
   - https://learn.microsoft.com/en-us/azure/azure-monitor/logs/personal-data-mgmt
2. Azure Architecture Center 的 Gatekeeper pattern 强调安全网关应先做 validation 和 sanitize，再把请求交给受信后端；本轮据此把 `RedactionEngine` 视作 diagnostics store/export 前的 sanitize gate，而不是 store/export 内部各自散落的后置修补。
   - https://learn.microsoft.com/en-us/azure/architecture/patterns/gatekeeper

## 3. 阻塞修复与设计结论

阻塞分类：

1. `DIA-BLK-004` 属于 context blocker：主链对象和组件边界已经冻结，但没有权威 redaction matrix，继续写 018 只会把“字段怎么处理”硬编码进实现并放大后续导出风险。

最小 blocker-fix：

1. 在 diagnostics 详细设计中新增专门章节，冻结 `strict` / `compat` 两档 profile、deny-list 最小集合、受控 evidence scheme 以及 redaction failure 的兜底规则。
2. 把 `DiagnosticsSnapshot` 公开字段映射到可执行的字段分级矩阵，明确哪些字段原样保留、哪些字段固定占位、哪些字段只允许 canonical summary/受控 token。
3. 明确 compat 只是“保留已验证的可调试上下文”，不是“允许原样透传”；一旦出现未知 evidence scheme 或原始 payload 引用，必须失败并阻断 store/export。

设计结论：

1. `infra.diagnostics.redaction.profile` v1 仅允许 `strict` 和 `compat`；其他值统一映射为 `INF_E_DIAG_REDACTION_FAIL`。
2. v1 deny-list 冻结为不区分大小写的 ASCII token：`secret`、`password`、`token`、`authorization`、`cookie`、`apikey`、`credential`。
3. `command.actor_ref` 在 strict/compat 两档都不得原样落盘，统一收敛到 `actor://redacted`。
4. `command.args` 在 strict 下只允许收敛到命令默认/安全 token，在 compat 下可以保留已通过 schema 校验的 token，但命中 deny-list 的 value 必须改写为 `redacted`。
5. `summary` 在 strict 下必须收敛为 canonical summary，在 compat 下允许保留 executor summary，但 deny-list 片段必须替换为 `[REDACTED]`。
6. `evidence_refs` 仅允许受控 scheme：`logs://`、`metrics://`、`health://`、`errors://`、`command://`、`snapshot://`、`export://`、`error://`；若出现 `raw://`、`inline://`、`data:` 或未知 scheme，一律视为 redaction failure。
7. `exporter_hint` 在 redaction 通过后只允许 `local_file`；任何 remote/export target hint 继续视为失败，避免 020 之前误开远程导出面。

### 3.1 字段分级矩阵

| 字段 | 字段等级 | strict | compat | 失败条件 |
|---|---|---|---|---|
| `snapshot_id` | S0 结构锚点 | 保留原值 | 保留原值 | 无 |
| `command.command_name` / `command.request_scope` / `command.timeout_ms` / `command.command_id` | S0 结构锚点 | 保留原值 | 保留原值 | 非白名单命令不进入 success 路径 |
| `command.actor_ref` | S2 身份敏感 | `actor://redacted` | `actor://redacted` | 无 |
| `command.args` | S1 操作上下文 | 收敛为安全默认 token | 保留 schema 合法 token，命中 deny-list 的 value 改写为 `redacted` | token 无法稳定重写或 schema 非法 |
| `summary` | S1 可分享摘要 | canonical summary | 保留 executor summary，deny-list 片段改写为 `[REDACTED]` | 替换后仍含 deny-list token |
| `evidence_refs` | S0/S1 引用锚点 | 仅保留受控 scheme，必要时替换尾部敏感片段 | 同 strict | 出现 `raw://`、`inline://`、`data:` 或未知 scheme |
| `redaction_profile` | S0 策略锚点 | `strict` | `compat` | profile 非 strict/compat |
| `exporter_hint` | S1 导出提示 | `local_file` | `local_file` | 非 `local_file` |

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 `strict/compat` profile 边界 | `infra/src/diagnostics/RedactionEngine.cpp` 直接按两档 profile 实现 redaction，不再猜测第三档策略 |
| 冻结 deny-list 最小集合 | `DiagnosticsRedactionTest` / `DiagnosticsRedactionFailureTest` 新增命中 deny-list 的成功遮蔽与失败阻断路径 |
| 冻结受控 evidence scheme 白名单 | `RedactionEngine` 只接受 `logs://`、`metrics://`、`health://`、`errors://`、`command://`、`snapshot://`、`export://`、`error://` |
| 冻结 strict canonical summary 与 safe args | 018 实现中把 `health.snapshot` / `queue.stats` / `thread.dump` 的 strict 输出收敛到稳定摘要和安全 token |
| 冻结 redaction failure 不得继续 store/export | facade 在 018 后必须在 redaction fail 时返回 `INF_E_DIAG_REDACTION_FAIL`，不写 snapshot store |

## 5. 对 DIA-TODO-018 的直接交接

1. `DIA-TODO-018` 可以从 `Blocked` 转为 `Not Started`，并按本交付物与 diagnostics 详细设计 6.5.3 直接实现 `RedactionEngine.cpp` 骨架。
2. 018 的最小完成边界应包括：
   - strict profile 成功把 actor/args/summary 收敛到安全输出；
   - compat profile 成功遮蔽 deny-list 片段但保留可调试摘要；
   - unknown/raw evidence scheme 触发 `INF_E_DIAG_REDACTION_FAIL`；
   - redaction fail 时阻断后续落盘与导出路径。
3. 018 不得顺手扩张出新的共享对象，也不得把 remote/export 策略提前并入 RedactionEngine。

## 6. Build 三件套

1. 代码目标：更新 diagnostics 详细设计、diagnostics 专项 TODO、infrastructure 总 TODO 和 worklog，并新增 blocker deliverable。
2. 测试目标：执行 process validation，确认 6.5.3 与 `DIA-BLK-004` / `DIA-TODO-018` 的台账状态一致。
3. 验收命令：
   - `rg -n "### 6.5.3|actor://redacted|raw://|inline://|data:" docs/architecture/DASALL_infra_diagnostics模块详细设计.md`
   - `rg -n "DIA-BLK-004|DIA-TODO-018|已解阻|Not Started" docs/todos/infrastructure/DASALL_infrastructure_diagnostics组件专项TODO.md docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`

## 7. 风险与回退

1. 若后续实现把 compat 扩成“敏感字段原样透传”，将直接回退本轮 blocker fix，并重新放大 diagnostics 导出面泄露风险。
2. 若下一轮把 `raw://`、`inline://` 或未知 scheme 视为“允许落盘再说”，会破坏“先脱敏再存储/导出”的主链安全门。
3. 若 future profile 需要比 strict/compat 更细的策略，必须通过新的 design gate 增量引入，不能在 v1 原位改写两档语义。