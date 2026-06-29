# WP-MEM-GAP-016 context assemble shared-contract uplift closeout

来源任务：WP-MEM-GAP-016
关联缺口：GAP-P3-C / MEM-E07
完成日期：2026-06-29

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-016 / GAP-P3-C / MEM-E07`，不把 `IMemoryStore` / `IContextOrchestrator` 的 shared interface admission、`WP-MEM-GAP-017` 的历史遗留清理或 `WP-MEM-GAP-018` 的 soak gate 增强混入同一轮。
2. authoritative 问题定义固定为：`MemoryContextRequest` / `ContextAssemblyResult` 对应的 supporting objects 是否已经在 runtime <-> memory 边界稳定到足以提升为 shared contracts，同时保持 `IMemoryManager` / `IContextOrchestrator` owner 仍留在 memory。
3. owner 边界保持不变：本轮只把 request/result supporting types 提升到 [contracts/include/context](../../../../contracts/include/context)，并在 [memory/include/context/MemoryContextRequest.h](../../../../memory/include/context/MemoryContextRequest.h) / [memory/include/context/ContextAssemblyResult.h](../../../../memory/include/context/ContextAssemblyResult.h) 保留 compatibility alias；`IMemoryStore` / `IContextOrchestrator` 继续是 memory-owned public interface，不进入 contracts admission。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §11.2 明确把 `MEM-B01` 定义为“`ContextAssembleRequest/Result` 尚未冻结为 shared contracts”，同时把 `MEM-E07` 触发条件固定为“runtime/memory 接口在多子系统消费后稳定”。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已把 `WP-MEM-GAP-016` 固定为“仅当多消费者出现时触发；将 `MemoryContextRequest` / `ContextAssemblyResult` 移入 `contracts/`”。
3. 当前本地实现已形成稳定多消费者面：runtime [runtime/src/AgentOrchestrator.cpp](../../../../runtime/src/AgentOrchestrator.cpp) 负责构造 request 并消费 result，knowledge projection glue [runtime/src/KnowledgeEvidenceProjector.h](../../../../runtime/src/KnowledgeEvidenceProjector.h) 在同一 request 面上追加 evidence，memory compile/contract/integration tests 也都已围绕同一字段集收口，没有第二套并行 shape。
4. Martin Fowler 的 DTO pattern 指出：当远端边界需要稳定传输数据时，应把成组数据封装为独立 transfer object，以隔离调用方与底层实现；这与本轮把 context assemble request/result 从 memory module-local supporting types 提升为 shared supporting contracts 的目标一致。
5. protobuf.dev 的 schema 演进建议强调共享 message 应保持加性兼容、避免新增 required field，并尽量把消息拆到独立文件中；这直接支撑本轮新建独立 [contracts/include/context/ContextAssembleRequest.h](../../../../contracts/include/context/ContextAssembleRequest.h) 与 [contracts/include/context/ContextAssembleResult.h](../../../../contracts/include/context/ContextAssembleResult.h)，同时保留旧 memory header 作为兼容别名层。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| `ContextAssembleRequest/Result` 提升为 shared contracts，同时不破坏现有 memory public include 路径 | [contracts/include/context/ContextAssembleRequest.h](../../../../contracts/include/context/ContextAssembleRequest.h)、[contracts/include/context/ContextAssembleResult.h](../../../../contracts/include/context/ContextAssembleResult.h)、[memory/include/context/MemoryContextRequest.h](../../../../memory/include/context/MemoryContextRequest.h)、[memory/include/context/ContextAssemblyResult.h](../../../../memory/include/context/ContextAssemblyResult.h) |
| 需要一个 focused contract gate 锁定 shared request/result 的默认值、payload、warnings/degraded 语义 | [tests/contract/context/ContextAssembleContractTest.cpp](../../../../tests/contract/context/ContextAssembleContractTest.cpp)、[tests/contract/CMakeLists.txt](../../../../tests/contract/CMakeLists.txt) |
| memory compile gate 需要显式证明 compatibility alias 仍与 shared contract 同型 | [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |
| `IMemoryStore` / `IContextOrchestrator` 不应因本轮 uplift 被误判为已进入 shared interface admission | 继续保留 [tests/contract/smoke/InterfaceCatalogContractTest.cpp](../../../../tests/contract/smoke/InterfaceCatalogContractTest.cpp) 的 `IMemoryStore` / `IContextOrchestrator` awaiting state，不在本轮改动接口 catalog |

## 4. 设计决策

1. shared uplift 只覆盖 supporting objects，不扩到 interface admission。`MemoryContextRequest` / `ContextAssemblyResult` 现在只是 `contracts::ContextAssembleRequest` / `contracts::ContextAssembleResult` 的 compatibility alias；`IMemoryManager` / `IContextOrchestrator` 的 ownership 仍留在 memory。
2. 兼容性优先于命名替换。本轮不强推 runtime / tests 全量改名到 `contracts::ContextAssemble*`；现有 `memory::MemoryContextRequest` / `memory::ContextAssemblyResult` 继续可用，避免在同一原子任务里引入大范围 include churn。
3. request/result 字段面继续保持加性演进约束：不新增 required-like hard gate，不重写现有默认值，不改变 `ContextPacket` payload shape；这与 protobuf 的共享 schema 演进约束一致。
4. `MEM-B01` 可在本轮解除，但 `MEM-B02` 仍保留：supporting contracts 已收敛，不再阻塞 future interface review；然而 `IMemoryStore` / `IContextOrchestrator` 是否进入 contracts admission 仍属于独立后续评审，不在 016 同轮扩张处理。

## 5. D Gate

1. 设计边界明确：本轮只移动 request/result supporting types，保留 memory public alias，不推进 interface catalog 变更。
2. Build 三件套完整：代码目标、测试目标、验收命令已锁定为 shared contract headers、contract test、memory compile gate。
3. blocker 结论：`MEM-B01` 已满足触发条件并在本轮闭合；`MEM-B02` 仍存在，但不再阻塞 016 的 supporting-object uplift，只继续阻塞未来 `IContextOrchestrator` / `IMemoryStore` 的 shared interface admission。

## 6. 代码结果

1. 新增 [contracts/include/context/ContextAssembleRequest.h](../../../../contracts/include/context/ContextAssembleRequest.h)，把 `request_id/session_id/trace_id/stage/user_turn/goal_summary/constraints_summary/latest_observation_digest_summary/visible_tools/token_budget_hint/latency_budget_ms/external_evidence/retrieval_evidence_refs` 冻结为 shared request surface。
2. 新增 [contracts/include/context/ContextAssembleResult.h](../../../../contracts/include/context/ContextAssembleResult.h)，把 `result_code/context_packet/dropped_sections/compression_notes/warnings/degraded` 冻结为 shared result surface。
3. 更新 [memory/include/context/MemoryContextRequest.h](../../../../memory/include/context/MemoryContextRequest.h) 与 [memory/include/context/ContextAssemblyResult.h](../../../../memory/include/context/ContextAssemblyResult.h)，将 module public type 改为对 shared contracts 的 compatibility alias，而不是继续各自拥有独立定义。
4. 更新 [tests/contract/CMakeLists.txt](../../../../tests/contract/CMakeLists.txt)，注册 `ContextAssembleContractTest`，让 shared request/result 进入 contract discoverability。
5. 新增 [tests/contract/context/ContextAssembleContractTest.cpp](../../../../tests/contract/context/ContextAssembleContractTest.cpp)，覆盖：
   - 正例：runtime projection 字段、`ContextPacket` payload、warnings/degraded 语义保持稳定；
   - 负例：default-constructed request/result 不应合成 request id、warnings 或 degraded state。
6. 更新 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)，显式 include shared headers，并以 `static_assert(std::is_same_v<...>)` 锁定 `memory::MemoryContextRequest == contracts::ContextAssembleRequest`、`memory::ContextAssemblyResult == contracts::ContextAssembleResult` 的 compile-time 兼容语义。

## 7. 验证

1. `Build_CMakeTools(buildTargets=["dasall_contract_context_assemble_test","dasall_memory_interface_compile_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["ContextAssembleContractTest","MemoryInterfaceCompileTest"])`
   - 结果：当前 VS Code CMake Tools 环境返回泛化 `生成失败`，未给出可归因到本轮代码的 test assertion 信息；因此本轮验收回退到 build-tree 直接执行测试二进制。
3. `./build/vscode-linux-ninja/tests/contract/dasall_contract_context_assemble_test`
   - 结果：通过，`ContextAssembleContractTest: 4 passed, 0 failed`。
4. `./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_interface_compile_unit_test`
   - 结果：通过，补充显式记账输出 `MemoryInterfaceCompileTest: PASS`。
5. `./build/vscode-linux-ninja/tests/contract/dasall_contract_interface_catalog_test`
   - 结果：通过，`6 passed, 0 failed`；证明本轮 supporting-object uplift 没有误放开 `IMemoryStore` / `IContextOrchestrator` 的 interface admission。

## 8. 完成判定

1. `WP-MEM-GAP-016 / GAP-P3-C / MEM-E07` 已闭合：`ContextAssembleRequest/Result` 已进入 shared contracts，并有 focused contract/compile gate 锁定语义。
2. `MEM-B01` 已解除；future shared interface admission 现在只剩 `MEM-B02`，不再被 request/result supporting object 缺失阻塞。
3. 当前 memory 的 P3 焦点已从 `WP-MEM-GAP-016 / -017 / -018` 收窄为 `WP-MEM-GAP-017 / -018`；016 不再是开放缺口。