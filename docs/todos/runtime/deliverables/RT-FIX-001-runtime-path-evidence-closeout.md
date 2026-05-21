# RT-FIX-001 runtime path evidence closeout

来源任务：RT-FIX-001
完成日期：2026-05-21
关联缺口：RT-GAP-001
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/ssot/RuntimeAppCompositionV1.md`

## 1. 任务边界

1. 本轮只收口 runtime path evidence 分层，不扩 durable checkpoint / replay、deadline / cancellation、production observability、app-binary / installed / release runner 证据。
2. authoritative 问题定义固定为：production direct LLM success、cognition-first success、true tool-positive full chain 与 recovery-positive path 不得再共用或混写同一正向结果标签。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用 build-tree 本地 integration tests 作为权威证据。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| 最终结果 owner | `runtime/src/AgentFacade.cpp` 同时持有 `RuntimeDependencySet.external_evidence` 与 `OrchestratorRunResult.used_tool_round` / `used_recovery_round` | path 分类可以在最终 `AgentResult` 出口统一归一化，不必把标签判定散落到 `AgentOrchestrator` 深层分支 |
| 合同承载面 | `contracts::AgentResult.tags` 与 guard 已存在，允许稳定携带非空检索标签 | `runtime_path:*` 无需新增 supporting contract，即可作为可审计出口标签 |
| direct / tool 回归 | `RuntimeUnaryIntegrationTest` 已同时覆盖 `runtime_path:direct_llm` 与 `runtime_path:tool_positive`，并断言两类标签互斥 | direct path 成功不再被误记为 tool-positive 或其它 full-path |
| full-chain 回归 | `FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest` 新增 `AgentFacade -> builtin query -> services data lane` case | `knowledge -> memory -> runtime -> tools -> services` query 主链可锁定 `runtime_path:tool_positive`，且不混写 `direct_llm` / `cognition_first` / `recovery_positive` |
| services loopback 契约 | `CapabilityServicesLoopbackFixture` 的 data snapshot 已接受 `default` query projection | `agent.dataset` builtin 默认 query projection 与 loopback data lane 对齐，不再被 mock capability snapshot 误拒绝 |

## 3. 设计结论

1. `runtime_path:*` 的最终归一化 owner 固定为 `AgentFacade`，而不是继续把标签判断塞进 `AgentOrchestrator` 的深层流程。原因不是“Facade 更方便”，而是它天然拥有最终 `AgentResult` 出口与 `OrchestratorRunResult` 的路径信号汇合点。
2. 本轮允许的 runtime path 标签固定为四个：`runtime_path:direct_llm`、`runtime_path:cognition_first`、`runtime_path:tool_positive`、`runtime_path:recovery_positive`。
3. 分类顺序固定为：`recovery_positive` -> `tool_positive` -> `direct_llm` -> `cognition_first`。其中：
   - `used_recovery_round=true` 优先归为 `runtime_path:recovery_positive`；
   - 否则 `used_tool_round=true` 归为 `runtime_path:tool_positive`；
   - 否则若 `external_evidence` 含 `required-live-baseline`，归为 `runtime_path:direct_llm`；
   - 否则若 `external_evidence` 含 `cognition-first-forced`，或该轮存在 `llm_manager` 且未命中更高优先级标签，则归为 `runtime_path:cognition_first`。
4. 同一 `AgentResult` 最多保留一个 `runtime_path:*` 标签；写入新标签前必须先清理既有 `runtime_path:` 前缀标签，禁止 `direct_llm`、`tool_positive`、`recovery_positive` 等并存。
5. path 标签只允许出现在 `Completed` / `PartiallyCompleted` 结果上；`Failed` / `Cancelled` / `Timeout` 不得伪造正向 path evidence。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 最终 `AgentResult` 只保留一个 runtime path 分类 | `runtime/src/AgentFacade.cpp` |
| direct LLM 与 tool-positive 不混写 | `tests/integration/agent_loop/RuntimeUnaryIntegrationTest.cpp` |
| true full chain 通过 services data lane 时固定为 `tool_positive` | `tests/integration/full_business_chain/FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest.cpp` |
| loopback data snapshot 接受 `agent.dataset` 默认 query projection | `tests/mocks/include/CapabilityServicesLoopbackFixture.h` |
| app/runtime composition SSOT 明确 path tag 使用规则 | `docs/ssot/RuntimeAppCompositionV1.md` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-001` / `RT-GAP-001`。
2. 本轮不增加 recovery-positive 的新正向 probe，也不把当前结果外推为 durable recovery、installed package 或 release-runner ready。
3. 本轮不使用 qemu / kvm；任何更高层验证仍归 `RT-FIX-006` 或 packaging / release gate。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_runtime_unary_integration_test dasall_fullint_012_knowledge_memory_llm_tools_services_cross_chain`：通过。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(RuntimeUnaryIntegrationTest|FullIntKnowledgeMemoryLlmToolsServicesCrossChainTest)$'`：通过。

## 7. 完成判定

1. `RT-GAP-001` 已关闭。
2. runtime direct path 成功不再被文档或测试外推为 cognition / tools / recovery full path。
3. `AgentResult` 现具唯一 `runtime_path:*` 分类标签，可区分 direct_llm、cognition_first、tool_positive、recovery_positive 的出口口径。
4. 本结论不外推为 qemu、installed package、release runner 或 recovery-positive 已具独立正向环境证据；这些仍属于后续任务范围。