# RT-FIX-007 runtime optional degraded semantics closeout

来源任务：RT-FIX-007
完成日期：2026-05-21
关联缺口：RT-GAP-006
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/architecture/DASALL-daemon本地控制面详细设计.md`、`docs/todos/runtime/deliverables/RT-FIX-005-runtime-production-observability-health-closeout.md`

## 1. 任务边界

1. 本轮只收口 runtime owner 对 optional degraded semantics 的结构化输出，不扩张到 runtime_support 的 installed positive knowledge probe、scheduler / background worker 模型、release runner 或 qemu 级证据。
2. authoritative 问题定义固定为：当 required ports 完整而 optional ports 缺失时，runtime 必须以 degraded-ready 而不是 fatal / ready-only 口径输出，并同时提供显式 degraded reason、readiness 字段、AgentResult evidence 与 daemon ping/readiness degraded reasons。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree focused unit / integration tests 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| init output owner | `runtime/include/AgentTypes.h` 现为 `AgentInitResult` 增加 `projected_readiness`、`missing_required_ports`、`missing_optional_ports`、`degraded_reasons` | runtime init surface 不再靠 diagnostics 字符串倒推出 degraded-ready，optional port gap 现在有结构化字段 |
| runtime owner projection | `runtime/src/AgentFacade.cpp` 现显式填充 degraded reasons，并在 degraded run 上统一输出 `runtime_readiness:degraded`、`runtime_degraded_reason:optional_port_gap`、`runtime_missing_optional:*`、`knowledge_unavailable` / `llm_unavailable` | required/optional port gap 现由 runtime owner 在 init/result 两个出口面统一投影，不再散落在局部测试断言中 |
| runtime regression | `tests/unit/runtime/AgentInitResultReadinessTest.cpp` 与 `tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp` 现锁定显式 readiness / degraded reason surface | degraded semantics 不再只由 diagnostics 文本或单条 tags 间接表达 |
| daemon readiness bridge | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp` 与 `apps/daemon/src/main.cpp` 现把 runtime degraded reasons 透传到 daemon ping/readiness payload | daemon 本地控制面不再只给出 `runtime_entrypoint_degraded_ready` 粗粒度原因，而能上浮 optional port 细项 |
| access regression | `tests/unit/access/DaemonReadinessCommandTest.cpp` 现断言 `runtime_optional_port_gap`、`runtime_missing_optional:knowledge`、`runtime_missing_optional:llm` 出现在 degraded readiness payload 中 | local control-plane ready/degraded 信号现在与 runtime owner 语义一致 |

## 3. 设计结论

1. required/optional readiness owner 继续固定在 runtime：`RuntimeDependencySet::describe_readiness()` 决定 port 完整性，`AgentFacade` 负责把它投影为 init/result surface；daemon health service 只消费结果，不重新判定 runtime 语义。
2. `AgentInitResult` 现在有显式 projected readiness 与 degraded reasons；diagnostics 继续保留为人类可读辅助文本，但不再是唯一真值来源。
3. `AgentResult` 继续用 tags 承载 degraded evidence，因为 frozen contracts 没有独立 readiness 对象；本轮通过 `runtime_degraded_reason:optional_port_gap` 与 `runtime_missing_optional:*` 固化这条证据面。
4. daemon ping/readiness 继续复用既有 `degraded_reasons` 容器，而不是新造一套 daemon-private runtime health contract。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 为 runtime init surface 固化 explicit degraded readiness / reasons | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp` |
| 为 AgentResult 固化 degraded reason evidence | `runtime/src/AgentFacade.cpp`、`tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp` |
| 为 daemon ping/readiness 上浮 runtime degraded reasons | `access/include/AccessGatewayFactory.h`、`access/src/AccessGatewayFactory.cpp`、`apps/daemon/src/main.cpp` |
| 锁定 runtime init readiness projection | `tests/unit/runtime/AgentInitResultReadinessTest.cpp` |
| 锁定 required/optional degraded semantics integration | `tests/integration/agent_loop/RuntimeRequiredOptionalPortsIntegrationTest.cpp` |
| 锁定 daemon readiness payload 的 degraded reasons | `tests/unit/access/DaemonReadinessCommandTest.cpp` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-007` / `RT-GAP-006`。
2. 本轮不扩张到 runtime_support 的 knowledge installed positive probe，也不把当前结果外推为 scheduler / background worker 已完成。
3. 本轮不使用 qemu / kvm；更高层 environment evidence 继续留给后续 runtime / packaging 任务。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_runtime_agent_init_result_readiness_unit_test dasall_runtime_required_optional_ports_integration_test`：通过。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(AgentInitResultReadinessTest|RuntimeRequiredOptionalPortsIntegrationTest)$'`：通过。
3. `cmake --build build/vscode-linux-ninja --target dasall_access_daemon_readiness_command_unit_test dasall-daemon`：通过。
4. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^DaemonReadinessCommandTest$'`：通过。

## 7. 完成判定

1. `RT-GAP-006` 已在当前树关闭。
2. optional port gap 现在会稳定投影为 degraded-ready init surface、AgentResult degraded evidence 与 daemon readiness degraded reasons，不再在 fatal / ready / degraded 之间漂移。
3. runtime 章节剩余优先级已收敛为 `RT-GAP-007` scheduler / background worker 模型，再继续推进更高层 release-runner / soak 证据。