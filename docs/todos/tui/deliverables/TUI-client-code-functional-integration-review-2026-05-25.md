# TUI 客户端代码与功能集成全面评估

> 用户请求：学习 DASALL 设计文档，调研相关行业实践，全面评估检查 TUI 客户端的代码与功能集成。

## 结论摘要

当前源码方向与 DASALL TUI 设计方案一致：TUI 保持 Product & Access Layer 壳层定位，通过 `ITuiDataSource` / `DaemonTuiDataSource` / `TuiIpcController` 接入 daemon，MVU reducer 只消费受控 projection，不持有 runtime/context/recovery/model route/tool policy owner 权。

用户截图中仍出现的双 Assistant receipt、`decision: awaiting none`、composer 卡在 pending、无 response 文本，和当前 build-tree 源码状态不一致。最可能原因是手工运行命中了滞后的 installed binary：当前 shell `dasall` 指向 `/usr/bin/dasall`，该文件时间戳为 5 月 7 日；build-tree `build/vscode-linux-ninja/apps/tui/dasall` 为 5 月 25 日。字符串检查也显示 `/usr/bin/dasall` 仍包含旧 `submit_turn_receipt` 路径，而 build-tree binary 已包含 `TranscriptMessageAppended` 与 `processing..`。

因此，当前最高风险不是源码局部修复缺失，而是 release/installed 证据尚未覆盖用户截图对应的 completed-response 真实路径。

## 设计基线核对

- `docs/architecture/DASALL_TUI客户端设计方案.md` 要求 TUI 作为 Product & Access Layer 入口壳层，正式链路经 `apps/tui -> platform IPC/UDS -> apps/daemon -> access -> runtime`。
- TUI 首版采用 unary + accepted_async + poll，不宣称端到端 streaming；这与当前 `DaemonTuiDataSource` 委托 `TuiIpcController` 的路径一致。
- TUI 只展示受控投影，不展示 raw Chain-of-Thought、provider-private reasoning、secret 或 raw tool output；当前 reducer/status panel 仍在 projection 层消费 `TuiEventProjection` / `TuiStatusProjection`。
- `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 中强调 focused/scripted IPC evidence、true daemon-backed E2E、release binary purity、installed smoke 必须分层；当前主要缺口正是在 installed/formal completed path。

## 行业实践映射

- FTXUI 的公开说明强调 C++ 原生、组件事件模型、keyboard/mouse、UTF-8/fullwidth、animations、CMake 集成；DASALL 选择 FTXUI + MVU + renderer adapter 是合理方向。
- prompt_toolkit 的成熟输入模型强调 `PromptSession`、连续多轮输入 history、key bindings、validation、cursor shape、async prompt，说明 TUI composer 必须被当作长会话输入系统，而不是一次性 stdin。
- Textual 的实践强调 key event、focus、binding/action、widget、loading indicator、testing/pilot；这对应 DASALL 当前需要强化的 focus/event-loop/headless interactive 测试。
- Aider 等 terminal-first AI 工具证明“终端内长会话 Agent 工作流”是成熟产品方向；但这类产品通常需要明确的 transcript、状态反馈、后台任务进度和可复现测试入口。

## 源码检查结果

### 已闭合或基本闭合

- submit 后用户输入进入 transcript：`TuiApp::dispatch_composer_submit()` 当前追加 `TranscriptMessageAppended` 的 `user` 行，而不是本地伪造 assistant receipt。
- daemon completed response 可投影：IPC DTO / client codec / server adapter 已传递 `response_text`，reducer receipt 分支优先显示 `response_text`，没有 response 时才 fallback 到 receipt summary。
- `pending_interaction="none"` 不再显示为 `awaiting none`：status panel 已把 literal `none` 视为无 pending。
- completed/rejected/cancelled/timeout/failed 等 terminal stage 会释放 composer busy；`current_tool=access.submit` 不再单独锁住输入。
- renderer 已支持输入等待态 cursor blink 和提交等待态 dots spinner；snapshot test 覆盖了 `processing..` 和隐藏 draft cursor。

### 仍存在的集成风险

1. P0：installed `/usr/bin/dasall` 滞后于 build-tree。用户直接运行 `dasall` 大概率复现旧行为，即使当前源码和 build-tree 已修复。
2. P0：formal/scripted smoke 只覆盖 accepted_async queued path，不能证明 completed + `AgentResult.response_text` + transcript 去重 + composer release 的正式入口行为。
3. P1：缺少显式“两轮连续 submit”app-level regression。当前 completed release 有测试，但应再断言第二次 Enter 真正产生第二个 submit request。
4. P1：真实 production runtime 是否稳定填充 `PublishEnvelope.agent_result.response_text` 仍需 installed/daemon/full path 证据。当前 completed response E2E 使用 custom runtime backend，能证明 TUI 投影能力，但不能单独证明生产 backend 输出质量。
5. P2：interactive animation/headless loop 仍偏 snapshot，缺少更接近真实键盘事件节奏的 focus/cursor/spinner 驱动测试。

## 可执行验证

本次评估执行的验证：

```bash
Build_CMakeTools(buildTargets=[
  dasall-tui,
  dasall_tui_submit_turn_integration_test,
  dasall_tui_daemon_backed_e2e_integration_test,
  dasall_tui_scripted_smoke_integration_test,
  dasall_tui_main_layout_snapshot_unit_test,
  dasall_tui_status_panel_unit_test,
  dasall_tui_ipc_controller_unit_test,
  dasall_tui_ipc_protocol_adapter_unit_test,
  dasall_tui_entrypoint_purity_integration_test,
  dasall_tui_socket_override_integration_test
])
```

结果：`0`。

`RunCtest_CMakeTools` 返回仓库已知泛化错误“生成失败”，`testFailure` 为空；随后直接运行对应可执行文件：

```bash
./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_submit_turn_integration_test && \
./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_daemon_backed_e2e_integration_test && \
./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_scripted_smoke_integration_test && \
./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_main_layout_snapshot_unit_test && \
./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_status_panel_unit_test && \
./build/vscode-linux-ninja/tests/unit/tui/dasall_tui_ipc_controller_unit_test && \
./build/vscode-linux-ninja/tests/unit/access/dasall_tui_ipc_protocol_adapter_unit_test && \
./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_entrypoint_purity_integration_test && \
./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_socket_override_integration_test && \
echo PASS
```

结果：`PASS`。

二进制路径检查：

```bash
command -v dasall
ls -l build/vscode-linux-ninja/apps/tui/dasall /usr/bin/dasall
strings /usr/bin/dasall | rg 'DASALL_TUI_SCRIPTED_SMOKE|submit_turn_receipt|TranscriptMessageAppended|processing\.\.'
strings build/vscode-linux-ninja/apps/tui/dasall | rg 'DASALL_TUI_SCRIPTED_SMOKE|submit_turn_receipt|TranscriptMessageAppended|processing\.\.'
```

关键结果：

- `command -v dasall` -> `/usr/bin/dasall`
- `/usr/bin/dasall` -> 5 月 7 日 installed binary，仍含旧 `submit_turn_receipt`
- `build/vscode-linux-ninja/apps/tui/dasall` -> 5 月 25 日 build-tree binary，含 `TranscriptMessageAppended` 与 `processing..`

## 建议修复优先级

1. P0：重切 installed binary/package，并运行 installed smoke，确保用户直接运行 `dasall` 命中最新 TUI。
2. P0：扩展 `DasallTuiScriptedSmokeTest`，新增 completed backend 场景，断言 user transcript、assistant `response_text`、无重复 assistant summary、无 `awaiting none`、composer 释放。
3. P1：在 `TuiAppSubmitTurnIntegrationTest` 增加两轮连续 submit regression，验证 completed event 后第二轮 submit 真实进入 data source。
4. P1：补一条 production-like daemon/runtime completed response 证据，确认 `PublishEnvelope.agent_result.response_text` 在非 custom backend 下可达。
5. P2：为正式 binary 增加 build identity / source revision 输出或 smoke artifact，降低后续 installed vs build-tree 混淆成本。

## 评估判定

当前 TUI 客户端架构边界、核心状态模型与 daemon-backed 接入方向是合格的；源码层面已经覆盖用户最初报告的大部分体验缺陷。真正阻塞“用户可用”的是安装态/正式入口证据缺口：如果继续用 `/usr/bin/dasall` 测，会继续看到旧行为；如果只看 focused tests，又会漏掉 completed-response installed path。下一轮应优先把 completed response 的正式入口测试与 installed package smoke 打通。