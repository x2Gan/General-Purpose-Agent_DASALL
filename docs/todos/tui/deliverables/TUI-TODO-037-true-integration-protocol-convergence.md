# TUI-TODO-037 true daemon-backed integration 与协议收敛口径

状态：Done
日期：2026-05-25
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只冻结 TUI 评审后新增的 true daemon-backed integration 口径：测试分层、协议 owner、schema version、operation dispatch、错误 taxonomy、临时 socket E2E 策略与 038~044 的依赖顺序。
2. 本任务不实现 daemon/access server handler，不修改 `apps/tui` submit path，不新增 installed smoke，也不拆 formal/prototype core；这些分别后置到 `TUI-TODO-038~043`。
3. 本任务的目标是收敛“改 server 还是改 client”的协议分岔，并明确现有 scripted IPC / focused integration 不能继续外推为 true daemon-backed ready。

## 2. 本地事实与近场判别

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 4.2、9.5.3、9.5.4 节已冻结：TUI 作为 Product & Access Layer 入口壳层，只能经 `ITuiDataSource` / `TuiIpcController` 消费 daemon-backed projection，不能直接持有 runtime 主控或 access 私有 transport 语义。
2. `docs/todos/tui/deliverables/TUI-TODO-003-daemon-projection-seam.md` 已冻结 TUI projection seam：stable contract 应为 `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope`，固定五个 operation，失败分支绑定 `reason_domain` + `reason_code`，且 CLI `DaemonClientResponse` / raw `UdsResponseFrame` 不能被 TUI 直接复用。
3. `apps/tui/src/ipc/TuiIpcController.h` 已存在 `kTuiIpcSchemaVersion = "tui_ipc.v1"`、`TuiIpcRequestEnvelope`、`TuiIpcResponseEnvelope` 以及 `open_session`、`submit_turn`、`poll_events`、`route_catalog`、`close_session` 五个 client-side operation surface。
4. 代码搜索表明 `access/` 当前没有 `tui_ipc.v1`、`TuiIpcRequestEnvelope` 或 `TuiIpcResponseEnvelope` 命中；daemon 侧尚未实现与 client 相同 schema 的 server handler。
5. `access/src/daemon/DaemonProtocolAdapter.cpp` 当前只处理既有 `ipc_uds` daemon frame，并围绕 `Ping`、`Status`、`Cancel`、`Readiness`、`Diagnostics`、`Knowledge` 等既有 command kind 解码/编码；它不会识别 TUI 的五个 operation，也不会产出 `TuiIpcResponseEnvelope`。
6. 因此当前缺口不是“client 还没会发请求”，而是“daemon/access 还没有接收并解释 `tui_ipc.v1` 的 server-side owner surface”；继续把 client 改回 CLI frame 会直接违背 `TUI-TODO-003` 已冻结的 projection seam。

## 3. 外部参考

1. Martin Fowler《The Practical Test Pyramid》指出：contract/integration test 与 end-to-end test 必须分层，高层 broad-stack tests 数量应少，但必须覆盖真实高价值用户路径；不能用低层 fixture 替代真实端到端路径。
   - 参考：https://martinfowler.com/articles/practical-test-pyramid.html
2. Protocol Buffers 官方 schema 演进指南指出：稳定协议一旦在用，已有字段/语义不应重解释；兼容演进应优先采用加法扩展与显式版本边界，而不是回收或改写既有语义。
   - 参考：https://protobuf.dev/programming-guides/editions/#updating-a-message-type

## 4. 收敛决策

### 4.1 选型结论

1. 选择新增 daemon/access `tui_ipc.v1` server handler，不回退 client 去复用既有 CLI daemon frame。
2. 复用现有本地 UDS/IIPC transport 与 daemon 入口进程，但在 access/daemon owner 边界新增独立的 TUI 协议适配层，避免把 CLI 私有 command surface 重新包装成 TUI contract。
3. `TuiIpcController` 继续作为 formal TUI client-side owner；`access/src/daemon/TuiIpcProtocolAdapter.*` 作为 server-side owner；两端通过同一个 `tui_ipc.v1` schema 和五个 operation 对齐。

### 4.2 为什么不改 client 去贴现有 daemon frame

1. 现有 daemon frame 面向 CLI control-plane，返回的是 `UdsResponseFrame` / `PublishEnvelope` 驱动的 CLI-style result，不是 TUI read-model projection。
2. 若让 TUI 退回既有 CLI frame，`route_catalog`、`poll_events`、`close_session` 的 TUI 语义会被迫塞进 CLI command/payload 约定，破坏 `TUI-TODO-003` 的 owner 边界。
3. 这条路会让 `ScriptedIpc` 看起来更容易复用，但本质是在扩大低层 fixture 假象，而不是收敛 true daemon-backed integration。

## 5. 证据分层冻结

| 证据层级 | 允许采信的结论 | 当前资产 | 明确禁止外推 |
|---|---|---|---|
| focused / scripted IPC | client codec、controller、reducer、data source contract 与 app orchestration 局部正确 | `ScopedIpcOverride`、`ScriptedIpc`、`TuiIpcControllerTest`、021~029 focused integration | 不得外推为真实 daemon/access 已处理 `tui_ipc.v1` |
| true daemon-backed E2E | formal TUI 通过真实 daemon/access 临时 socket 完成 `open -> route -> submit -> poll -> close` | `TUI-TODO-041` 完成后才可声明 | 不得外推为 installed package ready |
| installed package smoke | 已安装 `dasall` + installed daemon 在本机完成同一路径，并产出 artifact | `TUI-TODO-042` | 不得外推为 release-ready 或 qemu-ready |
| release-ready purity + closeout | formal binary 不含 fake contamination，且文档明确区分 focused / true E2E / installed smoke | `TUI-TODO-043`、`044` | 不得跳过 blocked 项直接宣称 ready |

冻结结论：037 之后，021~029 与 031~034 的历史证据仍然有效，但它们的可信结论只停留在 focused / command-release / packaging gate，不再被允许描述为 true daemon-backed。

## 6. `tui_ipc.v1` 协议冻结面

### 6.1 Schema 与演进规则

1. `schema_version` 固定为 `tui_ipc.v1`。
2. `TuiIpcRequestEnvelope` / `TuiIpcResponseEnvelope` 继续作为 client/server 逻辑 envelope 名称；server 侧实现可在内部自行映射，但对外语义必须与 client 一致。
3. v1 之后只允许加法扩展：新增可选 payload 字段、新 metadata key、新 operation；不允许重解释既有 operation 名、错误 code、必填相关联字段。
4. schema 不匹配必须稳定映射为 `reason_domain=protocol`、`reason_code=schema_mismatch`；不得退化为泛化 parse failure。

### 6.2 Operation dispatch

| operation | server owner | 成功结果 | 最小负例 |
|---|---|---|---|
| `open_session` | access/daemon TUI protocol adapter -> session/bootstrap projection seam | `TuiSessionView` | schema mismatch、permission denied、daemon unavailable |
| `submit_turn` | access admission/dispatch -> runtime handoff -> receipt projection | `TuiTurnReceipt` | validation rejected、request rejected、timeout |
| `poll_events` | access/runtime event projection seam | `TuiEventProjection` batch + status deltas | session not found、timeout、malformed response |
| `route_catalog` | access profile/route projection seam | `TuiRouteCatalogView` | route catalog unavailable、schema mismatch |
| `close_session` | access/daemon session close seam | close ack / close result | session not found、close unavailable |

冻结规则：038 必须以显式 operation dispatch 实现这五条路径；不得把它们重新编码成 CLI `status` / `run` / `cancel` 命令字符串，再由 client 端做二次猜测。

### 6.3 错误 taxonomy

1. 保持 `reason_domain` + `reason_code` 双字段判定；message 仅用于展示。
2. v1 必须覆盖并回归以下稳定错误码：`permission_denied`、`daemon_unavailable`、`timeout`、`schema_mismatch`、`malformed_response`、`validation_failed`、`request_rejected`、`session_not_found`、`close_unavailable`、`route_catalog_unavailable`。
3. 038 单元测试至少覆盖 unknown operation、schema mismatch、validation rejected 三个负例；041 E2E 再覆盖真正的 daemon-backed happy path。

## 7. 临时 socket / headless E2E 策略

1. `TUI-TODO-039` 必须为 formal `dasall` 提供受控 socket override，只改变测试入口的 socket path，不改变默认 operator model；默认生产路径继续指向 `/run/dasall/daemon.sock`。
2. `TUI-TODO-041` 必须在临时目录拉起真实 daemon/access socket，并使用 formal TUI client path 驱动 `open_session -> route_catalog -> submit_turn -> poll_events -> close_session`。
3. `TUI-TODO-041` 明确禁止 `ScopedIpcOverride`、`ScriptedIpc`、`FakeTuiDataSource` 或 prototype-only fake scenario 参与 true E2E 结论。
4. `TUI-TODO-042` 的 installed smoke 可复用同一 artifact 结构，但运行对象必须变成 installed `dasall` 与 installed daemon；build-tree temp socket 通过不等于 installed 通过。

## 8. Design->Build 映射

| 后续任务 | Build 目标 | 测试目标 | 验收出口 | 依赖关系 |
|---|---|---|---|---|
| `TUI-TODO-038` | 新增 `access/src/daemon/TuiIpcProtocolAdapter.*` 并接入 daemon/access server dispatch | `TuiIpcProtocolAdapterTest` / server handler 负例 | `cmake --build --preset vscode-linux-ninja --target dasall_tui_ipc_protocol_adapter_unit_test && ./build/vscode-linux-ninja/tests/unit/access/daemon/dasall_tui_ipc_protocol_adapter_unit_test` | 直接承接本设计决策 |
| `TUI-TODO-039` | formal `dasall` 增加 socket override / headless seam | `DasallTuiSocketOverrideTest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_socket_override_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_socket_override_integration_test` | 复用本任务冻结的 temp socket 策略 |
| `TUI-TODO-040` | `TuiApp` submit action 接通 `ITuiDataSource::submit_turn()` | `TuiAppSubmitTurnIntegrationTest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_submit_turn_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_submit_turn_integration_test` | 依赖 039 的 formal socket seam |
| `TUI-TODO-041` | 真实 daemon-backed E2E harness | `TuiDaemonBackedE2ETest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_daemon_backed_e2e_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_daemon_backed_e2e_integration_test` | 依赖 038、039、040 |
| `TUI-TODO-042` | installed `dasall` + installed daemon smoke + artifact dump | packaging smoke / autopkgtest | `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-tui-smoke bash scripts/packaging/pkg_smoke_install.sh --tui-daemon-backed-check` | 依赖 041 |
| `TUI-TODO-043` | 拆 formal/prototype core，守 fake contamination gate | `DasallTuiEntrypointPurityTest` | `cmake --build --preset vscode-linux-ninja --target dasall-tui dasall_tui_entrypoint_purity_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_entrypoint_purity_integration_test` | 与 041 并列，不阻塞 038/039/040 |
| `TUI-TODO-044` | 回写 closeout 口径、残余风险、回退策略 | evidence consistency | `rg -n "TUI-TODO-037" docs/todos/tui/deliverables/TUI-TODO-044-review-closeout-evidence.md && rg -n "true daemon-backed" docs/worklog/DASALL_开发执行记录.md` | 最终收口 |

## 9. 风险与回退策略

1. 若 038 不能提供 daemon/access `tui_ipc.v1` handler，Gate-TUI-11 继续 Blocked；021~029 的 scripted/focused evidence 只能继续维持 client-side ready 口径。
2. 若 039 只给 prototype path 提供 socket override，而 formal `dasall` 仍绑死 `/run/dasall/daemon.sock`，041 不得开始。
3. 若 040 只让 reducer 进入 `submitting` 状态，却没有调用 `ITuiDataSource::submit_turn()`，则 `SubmitRequested` 仍属于 UX 假闭环，BLK-TUI-010 保持 Open。
4. 若 043 未通过 purity gate，即使 041/042 通过，也只能声明 daemon-backed integration 已闭合，不能声明 formal release binary clean。

## 10. D Gate 结果

1. 协议分岔已收敛：选用“新增 daemon/access `tui_ipc.v1` server handler”，拒绝“把 client 改回 CLI daemon frame”。
2. true daemon-backed integration、installed smoke、release purity 三层证据现已严格分层，037~044 的依赖关系与每个任务的 Build 三件套均已明确。
3. `BLK-TUI-009` 仍保持 Open，但其解法已从“方向不明”收敛为“执行 038”；`BLK-TUI-010` 仍由 039/040 处理。

结论：TUI-TODO-037 = Done，D Gate = PASS，下一原子任务为 `TUI-TODO-038`。