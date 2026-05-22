# TUI-TODO-002 `/clear` 语义决策

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只冻结 TUI `/clear` 的会话语义：是否创建新 session、是否要求 daemon `close/open`、是否保留 input history，以及这些结论对 parser 和 session lifecycle 的直接约束。
2. 本任务不实现 `apps/tui` 生产代码，不新增 runtime/access session seam，不提前完成 `TUI-TODO-026`，也不把 prototype/fake 行为误写成 daemon-backed 正式能力。
3. 本任务只修复 `BLK-TUI-002` 这一 context blocker；若结论依赖 `BLK-TUI-007` 才能成立，则必须 fail-closed，而不是偷渡未冻结的 session open/close/query 行为。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 5.2 节已把 `/clear` 标成候选行为：“清空当前前台 transcript + 新建当前进程内 session”，同时明确首版采用短生命周期前台 session，每次进入 `dasall` 创建新前台 `session_id`，退出才显式 close。
2. 同一文档第 5.4 节已把“会话记录”和“输入历史”分离：transcript 只属于当前前台 session；input history 是用户已输入/已发送内容的独立浏览与检索面。因此 `/clear` 不应把清 transcript 和清 input history 混成同一动作。
3. 同一文档第 5.6 节说明 `/clear` 当前 owner 边界是 “TUI + session seam”，而 `/exit` 才是“关闭当前前台 session 并退出”。这意味着 `/clear` 不能与 `/exit` 共享完全相同的 close 语义。
4. 同一文档第 13 节把 `TUI-OQ-001` 定义为“`/clear` 是清空视图还是新建当前进程内 session”，并要求在 Phase 0 冻结；`TUI-RISK-001` 同时指出当前 session seam 不完整，无法可靠 open/close 前台 session。
5. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已把 `BLK-TUI-002`、`BLK-TUI-007` 并列写成 `/clear` 和 session lifecycle 的前置风险，其中 `BLK-TUI-007` 明确 runtime session open/close/query seam 仍不完整，当前允许的保守口径是“`/exit` 可本地退出并记录 close unavailable，`/clear` 保持本地视图行为”。
6. `docs/architecture/DASALL_access子系统详细设计.md` 第 6.19.1 节已冻结 daemon/access 的 `session_id` 生成策略：daemon 客户端可按请求级生成新 `session_id`，或按客户端 session token 关联。这说明“切到新 session”可以通过新的 session 绑定来表达，不要求 `/clear` 在当前阶段先拥有完整的 public close/open IPC seam。

## 3. 外部参考

1. OpenAI Conversation State 指南把会话状态分成“手工携带上下文”和“持久 conversation object”两类；开始新会话的关键动作是切换到新的 conversation 标识或不再继续前一个 conversation，而不是要求 UI 先原地销毁旧后端对象。这支持把 `/clear` 设计成“切换到新的前台会话语义”，而不是强依赖即时 backend close/open。
   - 参考：https://developers.openai.com/api/docs/guides/conversation-state

## 4. 冻结结论

1. `/clear` 在 TUI v1 中冻结为：清空当前前台 transcript 与当前 session 绑定的本地展示状态，并切换到“新的当前进程内前台 session 语义”。
2. 上述“新 session”是用户心智和前台绑定语义，不等于 `/clear` 当下必须调用已冻结的 daemon `close_session()` / `open_session()` 双动作。v1 不要求 `/clear` 立即触发 daemon close/open。
3. 在 `BLK-TUI-007` 未解前，`/clear` 的正式后端约束是 fail-closed：
   - 不强行调用未冻结的 public session close/open/query seam。
   - 不伪造“旧 session 已安全关闭”的事实。
   - 不把 `/clear` 降格成仅擦除屏幕而继续复用旧 `session_id`。
4. `/clear` 对后续真实请求的语义要求是：清理掉当前前台 session 的绑定，让下一次用户 submit 进入新的 foreground session，并获得新的 `session_id`；旧 session 若已存在持久化/运行态事实，继续由 runtime/access owner 按其生命周期管理，不由 TUI 在本任务内越权清扫。
5. `/clear` 保留 input history。原因是 input history 与 transcript 已在 5.4 分层：前者服务输入回忆与检索，后者服务当前前台 session 展示。`/clear` 只重置当前会话视图，不清空历史输入条目。
6. `/clear` 应清空当前 composer draft 与当前 session 相关 banner/状态摘要，避免用户在“新前台 session”里继续携带上一个 session 的未发送草稿或状态提示；但保留可通过 history recall / reverse search 找回的历史输入记录。
7. `/exit` 与 `/clear` 必须继续区分：`/exit` 的 owner 目标是“退出并触发 close 或可观测 close failure”；`/clear` 的 owner 目标是“留在当前 TUI 进程中，开始新的前台会话语义”。

## 5. 场景矩阵

| 场景 | `/clear` 冻结行为 | 明确禁止 | 备注 |
|---|---|---|---|
| fake/no-daemon prototype | 清空假 transcript 和本地状态，重置为新 foreground session 的 fake 起点 | 假装已完成真实 daemon close/open | 可先做本地 deterministic 行为 |
| daemon-backed 正式路径，但 session seam 未冻结 | 清空当前前台展示并解除旧 session 绑定；下一次 submit 使用新 session 语义 | 直接调用未冻结的 close/open IPC、宣称旧 session 已被正式回收 | 与 `BLK-TUI-007` 对齐 |
| 用户触发 `/clear` 后立即继续输入 | composer 是空草稿；history recall 仍可找回旧输入 | 自动把旧 draft 直接带入新 session | 避免 session 心智混淆 |
| 用户执行 `/exit` | 不适用；应走退出语义 | 用 `/clear` 冒充 `/exit` 或吞掉 close failure | `/exit` 仍独立测试 |

## 6. 对后续任务的直接约束

1. `TUI-TODO-013` 在 `BLK-TUI-002` 解阻后，不应再把 `/clear` 解析成 blocked/local pending action；应把它映射为明确的 local action，并在帮助文案中说明其“新前台 session、保留 input history”的含义。
2. `TUI-TODO-026` 必须把 `/clear` 和 `/exit` 分开验证：
   - `/clear`：当前 transcript 清空、composer draft 清空、下一次 submit 绑定新 `session_id`、input history 保留。
   - `/exit`：触发 close 或可观测 close failure，并退出进程。
3. `TUI-TODO-023` / `BLK-TUI-007` 后续若补齐 public session seam，可以把 `/clear` 的 backend 行为扩展为“best-effort close previous foreground session”，但前提是不会改变本文件冻结的用户可见语义；若语义会变，需要新 deliverable 重新过 gate。
4. 任何后续实现都不得把 `/clear` 写成仅清屏命令，也不得把它做成多 session list/history viewer 的替代入口。

## 7. Design -> Build 映射

| 后续任务 | 锁定的代码目标 | 锁定的测试目标 | 锁定的验收命令 |
|---|---|---|---|
| `TUI-TODO-013` | `apps/tui/src/command/TuiSlashCommandParser.h/.cpp` 需要把 `/clear` 映射成明确 local action，而不是 blocked 占位 | `TuiSlashCommandParserTest` | `ctest --preset vscode-linux-ninja -R "TuiSlashCommandParser" --output-on-failure` |
| `TUI-TODO-026` | `apps/tui/src/command/TuiSlashCommandParser.cpp`、`apps/tui/src/data/DaemonTuiDataSource.cpp`、`apps/tui/src/model/TuiReducer.cpp` 需要区分 `/clear` 与 `/exit` 语义，并让下一次 submit 绑定新 `session_id` | `TuiSessionLifecycleIntegrationTest` | `ctest --preset vscode-linux-ninja -R "TuiSessionLifecycle" --output-on-failure` |
| `BLK-TUI-007` 后续解阻 | `apps/tui/src/data/DaemonTuiDataSource.cpp` / projection seam 文档只可在不改变本文件可见语义的前提下补 session close/open 细节 | `DaemonTuiDataSourceContractTest`、`TuiSessionLifecycleIntegrationTest` | `ctest --preset vscode-linux-ninja -R "Tui(DaemonDataSource|SessionLifecycle)" --output-on-failure` |

## 8. D Gate 结果

1. `BLK-TUI-002` 已被收敛为单一结论：`/clear` 不是单纯清屏，也不是 `/exit` 的别名，而是“留在当前进程、切到新前台 session 语义、保留 input history”的会话重置动作。
2. `BLK-TUI-007` 没有被误写为已完成；本文件明确把 daemon close/open 约束保持为 future-only backend detail，而不是当前 Phase 0 的前置承诺。
3. 后续 Build 三件套已锁定到 `TUI-TODO-013` 与 `TUI-TODO-026`，对应 parser、session lifecycle 和 focused tests 均已有明确出口。

结论：TUI-TODO-002 D Gate = PASS。本任务为文档决策任务，无独立 B 阶段；完成条件是 deliverable、专项 TODO、总账与 worklog 口径同步回写。