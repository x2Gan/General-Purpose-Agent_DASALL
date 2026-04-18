# DASALL Memory 子系统专项 TODO

最近更新时间：2026-04-18（完成 MEM-TODO-001/002/003/004/005/006/007/008A/008B/009/009A/010/011/012/013/014/015/022/025）  
阶段：Detailed Design -> Special TODO  
适用范围：memory/  
当前结论：Memory 子系统已具备 L3/L2 混合粒度的专项拆分条件；`memory/include` 根与 `config/context/error/vector/working/writeback` 稳定子目录、`IMemoryManager.h`、`IContextOrchestrator.h`、`IMemoryStore.h`、`IStoreTransaction.h`、`ISummarizer.h`、`MaintenanceRequest.h`、`MaintenanceReport.h`、`working/IWorkingMemoryBoard.h`、`working/WorkingMemorySnapshot.h`、`working/WorkingMemoryExportRequest.h`、`working/WorkingMemoryExportResult.h`、`vector/IEmbeddingAdapter.h`、`vector/VectorMemoryIndexAdapter.h`、`config/MemoryConfig.h`、`context/MemoryContextRequest.h`、`context/ContextAssemblyResult.h`、`error/MemoryError.h`、`writeback/MemoryWritebackRequest.h`、`writeback/WritebackResult.h`、`writeback/SummaryGenerationRequest.h`、`writeback/SummaryGenerationResult.h`、`writeback/SummaryProjection.h`、`MemoryBuildSkeleton.cpp`、`src/MemoryManager.cpp`、`src/MemoryManagerFactory.cpp`、`src/working/WorkingMemoryBoard.cpp`、`src/store/sqlite/SqliteSchemaMigrator.cpp`、`src/store/sqlite/SqliteMemoryStore.cpp`、`src/store/sqlite/RowMappers.cpp`、`src/vector/UnavailableVectorMemoryIndexAdapter.cpp`、`sql/memory/V001__initial_schema.sql`、`MemoryInterfaceCompileTest`、`MemoryManagerLifecycleTest`、`MemoryManagerSmokeTest`、`WorkingMemoryBoardTest`、`WorkingMemorySnapshotTest`、`WorkingMemoryBoardConcurrencyTest`、`SchemaMigrationTest`、`SqliteTransactionTest`、`SqliteMemoryStoreTest`、`VectorMemoryAdapterTest`、`tests/mocks/include/FakeMemoryStore.h`、`tests/integration/memory/` 以及 `MemoryIntegrationTopologySmokeTest` 已落盘，memory 模块的 unit / integration 测试拓扑已进入顶层聚合且可被发现；public manager/orchestrator/store/summarizer/vector/working 接口、context / writeback request/result ABI、working export / maintenance supporting objects、summary/store/vector supporting objects、`MemoryConfig` 配置模型与 `MemoryError` 错误映射面已冻结第一批 supporting types，`MemoryManager` 生命周期骨架、`WorkingMemoryBoard` 核心、SQLite schema migration baseline 与 `SqliteMemoryStore` 的 Session/Turn/Summary/Fact/Experience/maintenance 基线均已可执行，`011 ~ 015` 的生命周期与主存储阶段已完成，后续主链可直接进入 Context 与 Writeback 组装链，仅剩 concrete vector backend 选型仍是独立评审 blocker。

## 1. 文档头

本文档严格基于以下输入生成：

1. `docs/architecture/DASALL_memory子系统详细设计.md`
2. `docs/architecture/DASALL_Agent_architecture.md`
3. `docs/architecture/DASALL_Engineering_Blueprint.md`
4. `docs/adr/ADR-005-architecture-review-baseline.md`
5. `docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md`
6. `docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md`
7. `docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md`
8. `docs/ssot/CrossModuleDataProjectionMatrix.md`
9. `docs/ssot/InfraConcurrencyPolicy.md`
10. `docs/ssot/InfraIntegrationTopology.md`
11. `docs/plans/DASALL_工程落地实现步骤指引.md`
12. `docs/development/DASALL_工程协作与编码规范.md`
13. 当前 contracts 收敛与交付基线：
   - `docs/todos/contracts/deliverables/WP05-T006-Memory对象定义.md`
   - `docs/todos/contracts/deliverables/WP05-T007-记忆事实对象定义.md`
   - `docs/todos/contracts/deliverables/WP05-T012-接口准入评估单.md`
14. 当前代码与测试现状：`memory/CMakeLists.txt`、`tests/unit/memory/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/contract/CMakeLists.txt`、`tests/contract/memory/*`、`tests/contract/context/ContextPacketFieldContractTest.cpp`、`tests/mocks/include/MockMemoryStore.h`
15. 现有专项 TODO / 交付风格基线：`docs/todos/tools/DASALL_tools子系统专项TODO.md`、`docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md` 及各 deliverables 目录

本文档目的不是补写新架构，也不是默认宣称 memory 已经进入编码执行，而是把当前 detailed design 转换为：

1. 可回溯的约束清单。
2. 可执行的粒度评估结论。
3. Design -> TODO 的工程映射。
4. 最小原子化任务清单。
5. 显式 blocker、解阻条件、质量门与回退策略。

---

## 2. 子系统目标与范围

### 2.1 子系统目标

根据 memory 详设 1.1、架构 4.6/5.3、蓝图 3.7，Memory 子系统的工程目标固定为：

1. 管理 `WorkingMemory`、`ShortTermMemory`、`LongTermMemory`、`VectorMemory`、`ExperienceMemory` 五层记忆结构，而不是退化为聊天历史缓存。
2. 以 `ContextOrchestrator` 为 memory 内部主控点，向 runtime / cognition / llm 提供已完成语义裁剪的 `ContextPacket`。
3. 接收回合结果、`ObservationDigest`、恢复结论和摘要候选，完成结构化写回、冲突检测、经验沉淀和压缩闭环。
4. 在预算不足、历史过长或 profile 裁剪场景下，稳定执行压缩、裁剪和分层保留，而不是把 raw history 直接上推给 llm。
5. 为 `memory/include`、`memory/src`、`tests/unit/memory`、`tests/integration/memory`、CMake 和 gate 提供可实现、可测试、可审计的落盘计划。

### 2.2 范围边界

纳入本专项 TODO 的对象：

1. memory 模块公共接口与 supporting types。
2. `MemoryManager`、`ContextOrchestrator`、`WorkingMemoryBoard`、`SqliteMemoryStore`、`CompressionCoordinator`、`WritebackCoordinator`、`MemoryConflictResolver`、`MemoryMaintenanceWorker`、`VectorMemoryIndexAdapter` 的工程落地。
3. memory 模块本地配置、错误语义、schema migration、WAL/并发约束、测试拓扑与 gate。
4. 基于 frozen contracts 的 `Turn`、`Session`、`SummaryMemory`、`MemoryFact`、`ExperienceMemory` 消费与验证。

不纳入本专项 TODO 的对象：

1. llm 的 Prompt 资产、消息渲染、provider payload 生成。
2. runtime 的主状态机、恢复裁定、checkpoint 执行逻辑。
3. knowledge 的查询理解、检索重排与结构化证据组装算法。
4. tools 的执行编排、副作用治理与 ResultProjector 本体。
5. 远程分布式 memory service、跨主机数据库集群与网络存储方案。
6. 任何 shared contracts 的新增 admission，除非已完成单独 contracts 评审。

---

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

为与 memory 详设 2.1 的 `MEM-C001 ~ MEM-C022` 保持一一映射，以下约束清单按同序号展开。

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| MEM-TC001 | memory 详设 1.1、2.1；架构 4.6、5.3 | Must | memory 必须承担五层记忆与 `ContextPacket` 语义装配闭环，不是聊天记录存储器 | TODO 必须覆盖 Working/ShortTerm/LongTerm/Experience 主链与 ContextOrchestrator |
| MEM-TC002 | ADR-006 3.2、6.1 | Must | `ContextOrchestrator` 归属 memory，拥有语义上下文装配权与语义预算裁剪权 | `ContextOrchestrator` / `CandidateCollector` / `BudgetAllocator` 必须优先落地 |
| MEM-TC003 | ADR-006 3.2、3.3 | Must-Not | memory 不得生成最终 messages、provider payload 或 rendered prompt | 不得把 llm prompt/rendering 任务写入 memory TODO |
| MEM-TC004 | ADR-007 3.2、3.3 | Must | memory 可以沉淀恢复结果与经验摘要，但不得决定 retry / compensate / abort_safe 的最终执行 | `WritebackCoordinator` 和 `ExperienceMemory` 任务不得侵入 runtime 恢复裁定 |
| MEM-TC005 | ADR-008 3.2、3.4 | Must-Not | memory 不得形成独立主循环、用户交互入口或全局调度中心 | `MemoryManager` 只能作为 runtime-facing facade，不得扩张为 orchestrator |
| MEM-TC006 | 架构 5.3.3；蓝图 3.7；memory 详设 2.1 | Must | token 不足时必须优先保留 `goal`、`constraints`、`latest_observation` | `BudgetAllocator` / `ContextOrchestrator` 的验收必须显式校验保留优先级 |
| MEM-TC007 | 架构 5.3.4；memory 详设 2.1、6.12.3 | Must | Long-Term Memory 写入必须保留 evidence、confidence 和 conflict handling | `MemoryConflictResolver` 与事实写回不可后置省略 |
| MEM-TC008 | 架构 5.3.7；WP05-T006/T007；memory 详设 2.1 | Must | 历史压缩必须保留 `confirmed_facts`；`open_questions` 和 `pending_actions` 不得丢失，但应通过 `WorkingMemory` 或 runtime checkpoint 保留，而不是强行扩 shared contracts | `CompressionCoordinator` / `WorkingMemoryBoard` 必须分层保存稳定摘要与瞬态问题，不得回扩 contracts |
| MEM-TC009 | 蓝图 3.7；memory 详设 2.1 | Must-Not | memory 不依赖 cognition / llm / tools 的具体实现类 | `CandidateCollector`、`CompressionCoordinator`、vector adapter 只能依赖抽象接口，include / link 不得越层 |
| MEM-TC010 | DASALL_tools子系统详细设计.md；SSOT `CrossModuleDataProjectionMatrix` | Must | tools 只输出 `Observation` / `ObservationDigest`，经 runtime 投影后交给 memory；不允许 tools 直写 memory 持久层 | writeback / store 任务只能接 runtime-facing request，不得为 tools 暴露直写入口 |
| MEM-TC011 | WP05-T006 | Must | `Turn`、`Session`、`SummaryMemory` 只表达稳定存储面，不得吸收 `SessionContext`、checkpoint、raw payload 字段 | TODO 不得把 shared contracts 当作运行态上下文或恢复载体 |
| MEM-TC012 | WP05-T007 | Must | `MemoryFact`、`ExperienceMemory` 只表达事实与经验沉淀面，不得吸收 runtime 控制字段、provider 字段、checkpoint 字段 | Fact / Experience 写回任务不得扩 shared contracts 以承载控制态 |
| MEM-TC013 | WP05-T012；架构 7.4；蓝图 9.4 | Must | `IMemoryStore`、`IMemoryManager`、`IContextOrchestrator` 应先落在 `memory/include`，不能直接 admission 到 contracts | 公共接口任务必须限定在 memory 模块 public surface |
| MEM-TC014 | DASALL_工程协作与编码规范.md 3.6/3.7 | Must | 模块边界错误必须显式上报；新增公共接口必须同步补 unit 或 contract 测试 | 接口和错误模型任务必须绑定明确测试与失败可观测性 |
| MEM-TC015 | SQLite WAL 文档；memory 详设 6.7 | Must | 若选 SQLite WAL，必须接受单 writer、多 reader、同主机、需要 checkpoint 管理的约束 | store / migration / maintenance 任务必须围绕单逻辑主库与 WAL 运维设计 |
| MEM-TC016 | SQLite WAL 文档；memory 详设 6.7.2a、6.12.6 | Must | 使用 WAL 时必须声明 checkpoint 策略、reader gap、`SQLITE_BUSY` 处理与连接管理边界 | `SqliteMemoryStore` / `MemoryMaintenanceWorker` 必须显式落盘 PASSIVE checkpoint、busy timeout 与 reader gap 策略 |
| MEM-TC017 | Azure Retry pattern；memory 详设 6.9.1 | Should | 仅在仓储层对可判定幂等的本地存储错误做极小范围重试，避免 layered retry | writeback / store / maintenance 只能保留 bounded local retry，不得引入跨层重试风暴 |
| MEM-TC018 | Azure Compensating Transaction pattern；ADR-007 | Should | 写回与补偿记录应具备可追溯、可恢复、幂等执行的元数据，但补偿执行权不在 memory | Experience / audit / writeback 任务需保留 recovery metadata，不得替代 runtime 执行补偿 |
| MEM-TC019 | 蓝图 7.6；memory 详设 9.1 | Must | Memory 设计必须映射到 unit / integration / contract / failure injection 测试 | 测试拓扑、gate 与 failure 路径都必须单列任务 |
| MEM-TC020 | 架构 5.3.2；蓝图 3.7 | Should | `VectorMemory` 默认可裁剪；在边缘低配 profile 下允许关闭而不影响 `Session` / `Turn` / `Summary` 基线 | MEM-TODO-022 先做 unavailable baseline，MEM-TODO-023 / 029 再冻结 concrete backend 与 profile 策略 |
| MEM-TC021 | 架构 5.3.2；DASALL_llm子系统详细设计.md | Must-Not | `ProgrammaticMemory` 不能复制 llm / tools 资产正文；当前阶段只允许保留稳定 asset ref 或标签 | `ExperienceMemory` / `ProgrammaticMemory` 相关任务不得持久化 Prompt / Skill / Tool 资产本体 |
| MEM-TC022 | SQLite WAL 文档 2026-03-20 更新 | Must | 若 memory 采用 WAL 且存在多线程写入 / 检查点并发，SQLite 版本应至少为 3.51.3 或采用已回补修复版本 | `SqliteMemoryStore` / CI / `third_party` 需显式校验或钉住 SQLite 版本基线 |

### 3.2 当前代码与测试现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| `memory/CMakeLists.txt` | `dasall_memory` 当前编译 `src/MemoryBuildSkeleton.cpp`，configure 时校验 `memory/include` 根与 `config/context/error/vector/working/writeback` 稳定子目录，并保留 `dasall_contracts` link 依赖 | memory 已退出 placeholder-only 静态库状态，公共 surface 骨架已接入构建图 |
| `memory/include/` | 已存在根目录及 `config/context/error/vector/working/writeback` 六个稳定子目录；`IMemoryManager.h`、`IContextOrchestrator.h`、`IMemoryStore.h`、`IStoreTransaction.h`、`ISummarizer.h`、`vector/IEmbeddingAdapter.h`、`vector/VectorMemoryIndexAdapter.h`、`config/MemoryConfig.h`、`context/MemoryContextRequest.h`、`context/ContextAssemblyResult.h`、`error/MemoryError.h`、`writeback/MemoryWritebackRequest.h`、`writeback/WritebackResult.h`、`writeback/SummaryGenerationRequest.h`、`writeback/SummaryGenerationResult.h`、`writeback/SummaryProjection.h` 已按 6.6 / 6.10.1a / 6.5.3 / 6.9.2 / 6.12.3 / 6.12.5 / 6.3.2 / 6.12.6 落盘 | 公共 ABI 落点已从骨架进入接口、supporting types、配置模型与错误映射冻结阶段，可承接后续实现任务 |
| `memory/src/` | 已移除 `placeholder.cpp`，新增 `MemoryBuildSkeleton.cpp` 作为 skeleton anchor，并新增 `vector/UnavailableVectorMemoryIndexAdapter.cpp` 作为 vector unavailable baseline | 真实实现已从骨架扩展到 vector no-op baseline，但 working / store concrete 实现仍待推进 |
| `tests/unit/memory/CMakeLists.txt` / `MemoryInterfaceCompileTest.cpp` | 已注册 `dasall_memory_interface_compile_unit_test` / `MemoryInterfaceCompileTest`，并通过 `tests/unit/CMakeLists.txt` 纳入顶层 unit executable 聚合；compile test 已真实消费 manager/orchestrator/store/summarizer 接口、summary/store supporting objects、config / context / writeback supporting types与 error 映射 ABI | memory unit 测试入口已从 topology 锚点升级为 public ABI compile gate |
| `tests/unit/memory/CMakeLists.txt` / `VectorMemoryAdapterTest.cpp` | 已注册 `dasall_vector_memory_adapter_unit_test` / `VectorMemoryAdapterTest`；测试覆盖 disabled / enabled-but-unavailable 两条 availability gate，验证 no-op upsert/search/rebuild 不调用 `IEmbeddingAdapter` | vector unavailable baseline 已具备独立单测出口，不再只停留在设计说明 |
| `tests/mocks/include/MockMemoryStore.h` / `FakeMemoryStore.h` | 既有 `MockMemoryStore.h` 仍保留简单 KV 脚手架；新增 `FakeMemoryStore.h` 已继承 `IMemoryStore` 并覆盖事务、session/turn/summary、fact/experience query 与 quarantine 基线 | memory store fake 已不再只有 KV 读写面，可承接后续 store/unit 场景 |
| `tests/integration/CMakeLists.txt` | 已接入 `memory` 子目录，并聚合 `${DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS}` 到顶层 integration executable 列表 | memory integration topology 已进入顶层聚合 |
| `tests/integration/memory/*` | 已存在 `CMakeLists.txt` 与 `MemoryIntegrationTopologySmokeTest.cpp`，注册 `MemoryIntegrationTopologySmokeTest` / `dasall_memory_integration_topology_smoke_integration_test` | memory integration smoke / discoverability 基线已落盘，可承接后续 `MemoryContextAssembleIntegrationTest`、`MemoryWritebackIntegrationTest` |
| `tests/contract/CMakeLists.txt` | 已注册 `TurnSessionSummaryMemoryContractTest`、`MemoryFactExperienceContractTest`、`ContextPacketFieldContractTest` | memory shared contracts 与 ContextPacket 边界 gate 已具备可复用入口 |
| `tests/contract/memory/*` | 已存在 `TurnSessionSummaryMemoryContractTest.cpp`、`MemoryFactExperienceContractTest.cpp` | shared memory 对象契约基线已稳定，不需要重建 |
| `contracts/include/memory/*` | `Turn`、`Session`、`SummaryMemory`、`MemoryFact`、`ExperienceMemory` 已冻结 | Build 任务应复用既有 contracts，而不是回写新字段 |
| `tests/mocks/include/MockMemoryStore.h` | 仅提供简单 KV mock，不具备 `IMemoryStore` 事务/查询接口 | 可作为脚手架信号，但不能替代未来 `IMemoryStore` 的高保真 fake/stub |

---

## 4. 粒度可行性评估

### 4.1 总体结论

结论：Memory 当前可直接生成 L3 / L2 混合专项 TODO，不适合整体按纯 L3 无阻塞推进。

当前最细可安全落盘粒度：

1. L3：`MemoryContextRequest`、`ContextAssemblyResult`、`MemoryWritebackRequest`、`WritebackResult`、`MemoryConfig`、`IMemoryStore`、`IStoreTransaction`、`IContextOrchestrator`、`WorkingMemoryBoard`、`CandidateCollector`、`BudgetAllocator`、`MemoryConflictResolver`、`SqliteSchemaMigrator`、`SqliteMemoryStore`、`MemoryMaintenanceWorker`。
2. L2：`IMemoryManager`、`MemoryManager`、`CompressionCoordinator`、`WritebackCoordinator`、`VectorMemoryIndexAdapter`。
3. L0：无组件 ownership / dependency 歧义；仅剩 1 个 concrete vector backend 评审项。

判断依据：

1. 详设已经给出完整接口清单、核心对象字段、主流程/异常流程、目录建议、测试出口和 Design -> Build 映射。
2. 绝大多数组件都具备类名、方法名、输入输出、失败语义和建议测试名，可直接进入接口级或函数语义级拆分。
3. 当前真正阻碍 L3 落地的不是主链缺乏设计，而是少量 supporting object 未成表，以及组件职责 ownership 仍有重复声明。
4. 因此专项 TODO 可以直接进入执行；当前不再有补设计前置项，只剩 1 个向量后端评审项。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| `WorkingMemoryExportRequest` / `WorkingMemoryExportResult` | 6.6、6.12.1 | L3 | 6.5.3a 已补字段、落点与 owner；6.12.1 已补 facade 包装语义 | 无明显缺口 | 作为 `IMemoryManager` supporting object 直接进入接口落盘任务 |
| `SummaryGenerationRequest` / `SummaryGenerationResult` / `SummaryProjection` | 6.3.1、6.12.2 | L3 | 6.5.3b 已补 schema、落点与中间投影边界；6.12.2 已补 fallback / merge / materialize 语义 | 无明显缺口 | 作为 `ISummarizer` / `CompressionCoordinator` supporting objects 直接进入接口与模板路径任务 |
| `MemoryContextRequest` / `ContextAssemblyResult` | 6.5.3、6.12.2 | L3 | 字段、用途、调用链、warnings/degraded 语义明确 | 无明显缺口 | 直接拆对象定义任务 |
| `MemoryWritebackRequest` / `WritebackResult` / `FactCandidate` / `ExperienceCandidate` | 6.5.3、6.12.3 | L3 | 字段、事务边界、partial / degraded / retryable 语义明确 | 无明显缺口 | 直接拆对象族定义任务 |
| `MemoryConfig` 及子配置 | 6.10.1a | L3 | `StorageConfig`、`ContextConfig`、`VectorConfig`、`MaintenanceConfig` 字段完整 | 无明显缺口 | 直接拆配置模型任务 |
| `MemoryError.h` | 6.6、6.9.2 | L2 | file path 已建议，错误域与返回语义已表述 | 具体对象/枚举名未冻结 | 以“错误域映射面”拆任务，不臆造不必要类型 |
| `IMemoryManager` | 6.6、6.12.1 | L2 | 方法集、生命周期、失败语义、依赖注入与 export supporting objects 已明确 | 无明显缺口 | 可在 supporting types 与 include 布局任务完成后直接冻结接口 |
| `IContextOrchestrator` | 6.6、6.12.2 | L3 | 方法、输入输出、装配顺序明确 | 无明显缺口 | 直接拆接口任务 |
| `IMemoryStore` / `IStoreTransaction` + query types | 6.6、6.12.5 | L3 | 完整方法集合、Supporting query/result struct、事务语义明确 | 无明显缺口 | 直接拆接口与 supporting types 任务 |
| `MemoryManager` | 6.12.1 | L2 | 生命周期、组合根、测试锚点与 `working_memory_board` 非 writeback 用途边界明确 | 无明显缺口 | 可直接进入骨架与 facade 转发实现 |
| `WorkingMemoryBoard` | 6.12.4 | L3 | slot / snapshot / TTL / LRU / concurrency 语义完整 | 无明显缺口 | 直接拆类实现任务 |
| `CandidateCollector` | 6.8.1、6.12.2 | L3 | request/set 结构、查询步骤、失败降级、测试锚点明确 | 无明显缺口 | 直接拆实现任务 |
| `BudgetAllocator` | 6.12.2 | L3 | policy / budget / trim action / over-budget 逻辑完整 | 无明显缺口 | 直接拆实现任务 |
| `CompressionCoordinator` | 6.3.1、6.12.2 | L2 | 模板路径、增量合并、fallback 语义、supporting objects、构造依赖与持久化 surface 已明确 | 无明显缺口 | 可直接进入阶段 1 模板路径与 `ISummarizer` 注入点实现 |
| `ContextOrchestrator` | 6.8.1、6.12.2 | L3 | assemble 顺序、trim 规则、slot mapping 表与测试锚点明确 | 无明显缺口 | 直接拆实现任务 |
| `MemoryConflictResolver` | 6.12.3 | L3 | `ConflictAction`、`ConflictRecord`、`ConflictResolutionPlan` 完整 | 无明显缺口 | 直接拆实现任务 |
| `WritebackCoordinator` | 6.8.2a、6.12.3 | L2 | 事务边界、partial semantics、private method 划分、测试锚点与 WorkingMemoryBoard 唯一 owner 边界明确 | 无明显缺口 | 可直接进入核心事务与附属写入实现 |
| `SqliteSchemaMigrator` | 6.7.2b、6.12.5 | L3 | migration file 规范、checksum、status/migrate 接口明确 | 无明显缺口 | 直接拆实现任务 |
| `SqliteMemoryStore` | 6.7.2a、6.12.5 | L3 | CRUD/query/transaction/row mapper/close 语义完整 | 无明显缺口 | 直接拆实现任务 |
| `VectorMemoryIndexAdapter` / `IEmbeddingAdapter` | 6.3.2、6.12.6 | L2 | 抽象接口、available/no-op 语义、search/upsert/rebuild 明确 | concrete backend 未冻结 | 先实现 unavailable baseline，再单列 backend 选型评审 |
| `MemoryMaintenanceWorker` | 6.12.6 | L3 | request/report/policy、主动/被动调度、checkpoint/retention/rebuild 流程明确 | 无明显缺口 | 直接拆实现任务 |

---

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| export / summarize supporting object 缺口 | 6.3.1、6.6、6.12.1、6.12.2 | 补设计 / schema | MEM-TODO-001、002 | 先补对象定义，再允许接口和组件进入 Build |
| 组件 ownership / dependency 歧义 | 6.12.1、6.12.2、6.12.3 | 补设计 / 收敛 | MEM-TODO-003、004 | `SummaryRepository` dependency 歧义与 `WorkingMemoryBoard` owner 重复声明均已收敛；A 阶段补设计解阻完成 |
| memory include 布局与公共 ABI | 6.6、8.1、8.2 | 目录 / 接口布局 | MEM-TODO-005、009、010 | 先建立 module public surface，再推进实现 |
| context supporting types | 6.5.3、6.6、6.12.2 | 数据结构 | MEM-TODO-006 | `MemoryContextRequest` / `ContextAssemblyResult` 直接决定 assemble 链路 |
| writeback supporting types | 6.5.3、6.12.3 | 数据结构 | MEM-TODO-007 | request/result/partial semantics 是写回闭环的前提 |
| config model | 6.10.1a、6.10.3 | 配置 / profile 投影 | MEM-TODO-008A | `MemoryConfig` 需先冻结，才能承接 store / vector / maintenance 默认策略 |
| error mapping | 6.6、6.9.2 | 错误处理 | MEM-TODO-008B | storage busy / schema mismatch / validation rejected 等错误域需先固化 |
| summarizer abstraction interface | 6.3.1、6.12.2 | 接口定义 | MEM-TODO-009A | 先冻结 `ISummarizer` 契约，再推进 `CompressionCoordinator` 模板路径与阶段 2 注入点 |
| lifecycle facade | 6.6、6.12.1 | 生命周期 / 组合根 | MEM-TODO-011 | `MemoryManager` 是 runtime-facing 唯一必选入口 |
| WorkingMemory 黑板 | 6.12.4；8.2 Slice 1 | 内存数据结构 / 并发 | MEM-TODO-012 | snapshot/restore/TTL/LRU 是主链 Smoke 的前提 |
| SQLite store 与 migration | 6.7.2a、6.7.2b、6.12.5 | 持久化 / schema | MEM-TODO-013、014、015 | 先主链表，再 Fact/Experience/maintenance 支持 |
| context assemble pipeline | 6.8.1、6.12.2 | 组装 / 预算 / 压缩 | MEM-TODO-016、017、018、019 | 先 CandidateCollector 和 BudgetAllocator，再接 Compression 和 ContextOrchestrator |
| writeback / conflict pipeline | 6.8.2a、6.12.3 | 写回 / 冲突处理 | MEM-TODO-020、021 | 保证 core transaction 与 derived writes 分层 |
| vector unavailable baseline / maintenance / profile gate | 6.3.2、6.12.6、9.1 | 适配器 / 运维 / 兼容性 | MEM-TODO-022、023、024、029 | 先落盘 no-op vector baseline 解开前置依赖，再评审 concrete backend；maintenance 不阻塞主链 |
| tests topology / smoke / failure / evidence | 8.1、8.2、8.3、9.1、9.2 | 测试 / gate / 交付回写 | MEM-TODO-025、026、027、028、029、030 | memory 必须先接 topology，再回写 smoke / writeback / failure / profile gate 证据 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 设计缺口收敛类任务 | 是 | MEM-TODO-001、002、003、004 |
| 接口定义类任务 | 是 | MEM-TODO-009、009A、010、022 |
| 数据结构定义类任务 | 是 | MEM-TODO-001、002、006、007、008A |
| 生命周期与初始化类任务 | 是 | MEM-TODO-011、013 |
| 适配器 / 桥接类任务 | 是 | MEM-TODO-018、022、024 |
| 异常与错误处理类任务 | 是 | MEM-TODO-008B、020、021、028 |
| 配置与 Profile 裁剪类任务 | 是 | MEM-TODO-008A、023、029 |
| 测试与门禁类任务 | 是 | MEM-TODO-025、026、027、028、029 |
| 文档 / 交付证据回写类任务 | 是 | MEM-TODO-023、030 |

---

## 6. 原子任务清单

说明：除文档一致性任务使用 `rg` 检索外，本章验收命令统一以 `cmake -S . -B build-ci -G "Unix Makefiles" &&` 作为配置前缀；表中标注“后续集成验收”的测试项表示在相应后置任务完成后追加验证，不作为当前任务的即时通过条件。

### 6.1 前置补设计任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MEM-TODO-001 | Done | 补齐 WorkingMemory 导出对象定义 | memory 详设 6.6、6.12.1；阶段 G；编码规范 3.6/3.7 | 6.6 `IMemoryManager`；6.12.1 `MemoryManager` | L0 | 更新 `docs/architecture/DASALL_memory子系统详细设计.md` 与本专项 TODO，补成 `WorkingMemoryExportRequest/WorkingMemoryExportResult` 字段表与 owner 说明 | `WorkingMemoryExportRequest`、`WorkingMemoryExportResult` | 文档一致性：导出 request/result 字段、错误语义、调用者、落盘位置可通过文本检索命中 | `rg -n "WorkingMemoryExportRequest|WorkingMemoryExportResult" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md` | 无 | MEM-BLK-01 | 完成本任务 | 更新后的 memory 详设；更新后的本专项 TODO；2026-04-17 已通过 `rg -n "WorkingMemoryExportRequest|WorkingMemoryExportResult" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md`，确认 6.5.3a / 6.12.1 已具备字段、owner、返回语义与落盘位置，并同步回写本专项 TODO 的 4.1 / 4.2 / 8 / 11 章节 | 仅当 export request/result 具备字段、职责、返回语义和落盘位置，且不再只是接口签名引用时完成 |
| MEM-TODO-002 | Done | 补齐 Summary 生成 supporting objects | memory 详设 6.3.1、6.12.2；MEM-E01 | 6.3.1 `ISummarizer`；6.12.2 `CompressionCoordinator` | L0 | 更新 `docs/architecture/DASALL_memory子系统详细设计.md` 与本专项 TODO，补成 `SummaryGenerationRequest`、`SummaryGenerationResult`、`SummaryProjection` schema | `SummaryGenerationRequest`、`SummaryGenerationResult`、`SummaryProjection` | 文档一致性：summarizer 输入输出、模板路径与 fallback 语义可检索 | `rg -n "SummaryGenerationRequest|SummaryGenerationResult|SummaryProjection" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md` | 无 | MEM-BLK-02 | 完成本任务 | 更新后的 memory 详设；更新后的本专项 TODO；2026-04-17 已通过 `rg -n "SummaryGenerationRequest|SummaryGenerationResult|SummaryProjection" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md`，确认 6.5.3b / 6.12.2 已具备 schema、fallback、增量合并与 module-local 边界，并同步回写本专项 TODO 的 4.1 / 4.2 / 6.2 / 8 / 11 章节 | 仅当 `ISummarizer` 的 supporting objects 成表，且 `CompressionCoordinator` 的阶段 2 输入输出不再悬空时完成 |
| MEM-TODO-003 | Done | 收敛 CompressionCoordinator 持久化依赖口径 | memory 详设 6.3、6.6、6.12.2；设计决策注记 | 6.3 组件职责；6.6 `IMemoryStore`；6.12.2 `CompressionCoordinator` | L0 | 更新 `docs/architecture/DASALL_memory子系统详细设计.md` 与本专项 TODO，统一 `CompressionCoordinator` 是依赖 `IMemoryStore`、逻辑 `SummaryRepository` port，还是独立 `SummaryRepository` 接口 | `CompressionCoordinator` 构造依赖；`SummaryRepository`；`IMemoryStore` | 文档一致性：不再同时出现“统一由 `SqliteMemoryStore` 承载”和“构造函数注入 `SummaryRepository&`”的双口径 | `rg -n "CompressionCoordinator|SummaryRepository|IMemoryStore|SqliteMemoryStore" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md` | 无 | MEM-BLK-03 | 完成本任务 | 更新后的 memory 详设；更新后的本专项 TODO；2026-04-17 已通过 `rg -n "CompressionCoordinator|SummaryRepository|IMemoryStore|SqliteMemoryStore" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md`，确认 `CompressionCoordinator` 构造依赖与摘要持久化 surface 已固定为 `IMemoryStore`，`SummaryRepository` 仅保留为逻辑职责名，并同步回写本专项 TODO 的 4.1 / 4.2 / 6.2 / 8 / 11 章节 | 仅当 `CompressionCoordinator` 的 persistence dependency 与 store 架构口径统一，且可直接映射到头文件 / 构造函数时完成 |
| MEM-TODO-004 | Done | 收敛 WorkingMemoryBoard 更新 ownership | memory 详设 6.12.1、6.12.3 | 6.12.1 `MemoryManager.write_back`；6.12.3 `WritebackCoordinator.update_working_board` | L0 | 更新 `docs/architecture/DASALL_memory子系统详细设计.md` 与本专项 TODO，明确 `WorkingMemoryBoard` 更新由 `MemoryManager` 或 `WritebackCoordinator` 单点负责 | `MemoryManager.write_back`；`WritebackCoordinator.update_working_board` | 文档一致性：writeback 链路只保留一处 `WorkingMemoryBoard` 更新 owner 描述 | `rg -n "MemoryManager|WritebackCoordinator|update_working_board|WorkingMemoryBoard 更新" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md` | 无 | MEM-BLK-04 | 完成本任务 | 更新后的 memory 详设；更新后的本专项 TODO；2026-04-17 已通过 `rg -n "MemoryManager|WritebackCoordinator|update_working_board|WorkingMemoryBoard 更新" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md`，确认 `WritebackCoordinator` 为 WorkingMemoryBoard 更新唯一 owner，`MemoryManager` 只保留 facade 转发与 export snapshot 边界，并同步回写本专项 TODO 的 4.1 / 4.2 / 5.1 / 6.2 / 8 / 11 章节 | 仅当 WorkingMemory 更新 owner 唯一、调用顺序清晰、测试责任可单点归属时完成 |

### 6.2 Build-ready 任务

| ID | 状态 | 任务标题 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| MEM-TODO-005 | Done | 新增 memory 公共 include 布局与 CMake 骨架 | memory 详设 6.6、8.1、8.2；当前代码现状 | 6.6 接口清单；8.1 目录建议；7 MEM-D001 | L2 | 建立 `memory/include/`；保留既有 include path 配置，仅替换 `memory/CMakeLists.txt` 中的 `placeholder.cpp` source 列表并确认 `dasall_contracts` link 依赖；替换 `tests/unit/memory/CMakeLists.txt` 占位内容 | `memory/include/*`；`memory/CMakeLists.txt` | unit：`MemoryInterfaceCompileTest`；contract：既有 memory/context contract tests 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_memory_interface_compile_unit_test dasall_contract_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest" --output-on-failure` | 无 | 无 | 无 | `memory/CMakeLists.txt`；`memory/src/MemoryBuildSkeleton.cpp`；`memory/include/{config,context,error,vector,working,writeback}/.gitkeep`；`tests/unit/memory/CMakeLists.txt`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；`tests/unit/CMakeLists.txt`；2026-04-17 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_interface_compile_unit_test`、`dasall_contract_tests`，并验证 `MemoryInterfaceCompileTest`、`TurnSessionSummaryMemoryContractTest`、`MemoryFactExperienceContractTest`、`ContextPacketFieldContractTest` 全绿 | 仅当 `dasall_memory` 不再是 placeholder-only，且 unit / contract 入口可承载后续 memory 文件时完成 |
| MEM-TODO-006 | Done | 定义 context 请求与结果对象族 | memory 详设 6.5.3、6.6、6.12.2 | 6.5.3 `MemoryContextRequest`、`ContextAssemblyResult` | L3 | 新增 `memory/include/context/MemoryContextRequest.h`、`memory/include/context/ContextAssemblyResult.h` | `MemoryContextRequest`、`ContextAssemblyResult` | unit：`MemoryInterfaceCompileTest`；后续集成验收：`ContextOrchestratorBudgetTest`（在 MEM-TODO-026 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-005 | 无 | 无 | `memory/include/context/MemoryContextRequest.h`；`memory/include/context/ContextAssemblyResult.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure`，确认 `MemoryInterfaceCompileTest` 编译消费 request/result 字段、默认 token/latency budget、`warnings`/`degraded` 语义与 `ContextPacket` payload 对接，且全量 unit 结果保持 `100% tests passed, 0 tests failed out of 299` | 仅当 request/result 字段与 6.5.3 一致，且 warnings/degraded 语义可被编译消费并为后续 `ContextOrchestratorBudgetTest` 留出稳定接口时完成 |
| MEM-TODO-007 | Done | 定义 writeback 请求与结果对象族 | memory 详设 6.5.3、6.12.3 | 6.12.3 `MemoryWritebackRequest`、`WritebackResult` | L3 | 新增 `memory/include/writeback/MemoryWritebackRequest.h`、`memory/include/writeback/WritebackResult.h` | `MemoryWritebackRequest`、`WritebackResult`、`FactCandidate`、`ExperienceCandidate` | unit：`MemoryInterfaceCompileTest`；后续集成验收：`MemoryWritebackIntegrationTest`（在 MEM-TODO-027 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-005 | 无 | 无 | `memory/include/writeback/MemoryWritebackRequest.h`；`memory/include/writeback/WritebackResult.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 writeback request/result、`FactCandidate` / `ExperienceCandidate`、`partial` / `retryable_storage_failure` / `degraded` 语义可被编译消费，且 `Turn` 自带 tool / observation refs 未被重复展开 | 仅当 request/result/partial/retryable 语义与 6.12.3 对齐，且不混入 runtime 恢复字段并为后续 `MemoryWritebackIntegrationTest` 留出稳定接口时完成 |
| MEM-TODO-008A | Done | 定义 MemoryConfig 配置模型 | memory 详设 6.10.1a、6.10.3；阶段 G/H | 6.10.1a `MemoryConfig`；6.10.3 profile defaults | L3 | 新增 `memory/include/config/MemoryConfig.h` | `StorageConfig`、`ContextConfig`、`ExperienceConfig`、`VectorConfig`、`MaintenanceConfig` | unit：`MemoryInterfaceCompileTest`；后续集成验收：`MemoryProfileCompatibilityTest`（在 MEM-TODO-029 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-005 | 无 | 无 | `memory/include/config/MemoryConfig.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 storage/context/experience/vector/maintenance 默认值、profile-safe vector disabled baseline 与 maintenance schedule 配置位可被编译消费 | 仅当配置键、默认值和 profile 投影点与详设表格一致，且可被 store / vector / maintenance 任务复用时完成 |
| MEM-TODO-008B | Done | 定义 MemoryError 错误域映射面 | memory 详设 6.6、6.9.2；工程规范 3.6/3.7 | 6.9.2 错误语义表 | L2/L3 | 新增 `memory/include/error/MemoryError.h` | `StorageBusy`、`SchemaMismatch`、`ValidationRejected`、errno / `ResultCode` 映射入口 | unit：`MemoryInterfaceCompileTest`；后续故障验收：`MemoryFailureInjectionTest`（在 MEM-TODO-028 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-005 | 无 | 无 | `memory/include/error/MemoryError.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 `StorageBusy` / `SchemaMismatch` / `ValidationRejected` 及 `StorageUnavailable` / `ConfigInvalid` 的 `ResultCode`、warning key、audit scope、retryable 语义与 errno 映射入口可被编译消费 | 仅当错误域、warning / audit 语义和 errno / `ResultCode` 映射面冻结，且 failure path 有统一入口时完成 |
| MEM-TODO-009 | Done | 定义 IMemoryManager 与 IContextOrchestrator 接口 | memory 详设 6.6、6.12.1、6.12.2；WP05-T012 | 6.6 `IMemoryManager`、`IContextOrchestrator` | L2/L3 | 新增 `memory/include/IMemoryManager.h`、`memory/include/IContextOrchestrator.h` | `IMemoryManager`；`IContextOrchestrator` | unit：`MemoryInterfaceCompileTest`；后续生命周期验收：`MemoryManagerLifecycleTest`（在 MEM-TODO-011 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-001、005、006、007、008A、008B | 无 | 无 | `memory/include/IMemoryManager.h`；`memory/include/IContextOrchestrator.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 `init/prepare_context/write_back/export_working_memory_snapshot/run_maintenance` 与 `assemble` 签名保持在 `memory/include`、直接消费已冻结 config/context/writeback surface，并以前向声明方式为 WorkingMemory export / maintenance supporting types 预留稳定接口面 | 仅当接口仍停留在 `memory/include`、不越权进入 contracts，且 lifecycle / assemble 方法签名完整并为后续 manager 生命周期测试留出稳定面时完成 |
| MEM-TODO-009A | Done | 定义 ISummarizer 抽象接口 | memory 详设 6.3.1、6.12.2；MEM-E01 | 6.3.1 `ISummarizer`；6.12.2 `CompressionCoordinator` | L2/L3 | 新增 `memory/include/ISummarizer.h` | `ISummarizer`；`SummaryGenerationRequest`；`SummaryGenerationResult` | unit：`MemoryInterfaceCompileTest`；后续集成验收：`CompressionCoordinatorSummarizerTest`（在 MEM-TODO-018 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-002、005 | 无 | 无 | `memory/include/ISummarizer.h`；`memory/include/writeback/SummaryGenerationRequest.h`；`memory/include/writeback/SummaryGenerationResult.h`；`memory/include/writeback/SummaryProjection.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 summarizer 注入口径、summary supporting objects 字段与 fallback/degraded 语义可被编译消费，不再仅停留在设计文档 | 仅当 `ISummarizer` 接口可在不泄漏 llm 具体实现的前提下注入 `CompressionCoordinator`，且 supporting objects 不再悬空时完成 |
| MEM-TODO-010 | Done | 定义 IMemoryStore 与 store supporting types | memory 详设 6.6、6.12.5；WP05-T012 | 6.6 `IMemoryStore`、`IStoreTransaction`；6.12.5 query/result structs | L3 | 新增 `memory/include/IMemoryStore.h`、`memory/include/IStoreTransaction.h`，并在 `tests/mocks/include/` 增补 `FakeMemoryStore.h` 或升级 `MockMemoryStore.h` 以覆盖事务 / query 接口 | `IMemoryStore`、`IStoreTransaction`、`SessionLoadRequest/Bundle`、`StoreResult`、`FactQuery*`、`ExperienceQuery*` | unit：`MemoryInterfaceCompileTest`；后续存储验收：`SqliteMemoryStoreTest` / `SchemaMigrationTest`（在 MEM-TODO-013 ~ 015 完成后追加） | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | MEM-TODO-005、008A、008B | 无 | 无 | `memory/include/IMemoryStore.h`；`memory/include/IStoreTransaction.h`；`tests/mocks/include/FakeMemoryStore.h`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；2026-04-18 已通过 `cmake --build build-ci --target dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^MemoryInterfaceCompileTest$' --output-on-failure`，确认 store / transaction 接口、`SessionLoadRequest/Bundle`、`StoreResult`、`FactQuery*`、`ExperienceQuery*` 以及 fake store 的事务 / query / quarantine 基线可被编译消费，且测试替身不再只有简单 KV 语义 | 仅当 store 接口、事务接口和 supporting structs 与 6.6 / 6.12.5 一致，且测试替身不再只是简单 KV 形态时完成 |
| MEM-TODO-011 | Done | 实现 MemoryManager 生命周期骨架与工厂函数 | memory 详设 6.6.1、6.12.1、7 MEM-D001 | 6.12.1 `MemoryManager`；6.6.1 `create_memory_manager` | L2 | 新增 `memory/src/MemoryManager.cpp`、`memory/src/MemoryManagerFactory.cpp` | `MemoryManager::init`、`shutdown`、`prepare_context`、`write_back`、`export_working_memory_snapshot`、`run_maintenance`；`create_memory_manager` | unit：`MemoryManagerLifecycleTest`、`MemoryManagerSmokeTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryManager(Lifecycle|Smoke)Test" --output-on-failure` | MEM-TODO-004、005、006、007、008A、008B、009、010 | 无 | 无 | `memory/include/IMemoryManager.h`；`memory/include/MaintenanceRequest.h`；`memory/include/MaintenanceReport.h`；`memory/include/working/WorkingMemorySnapshot.h`；`memory/include/working/WorkingMemoryExportRequest.h`；`memory/include/working/WorkingMemoryExportResult.h`；`memory/src/MemoryManagerInternal.h`；`memory/src/MemoryManager.cpp`；`memory/src/MemoryManagerFactory.cpp`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；`tests/unit/memory/MemoryManagerLifecycleTest.cpp`；`tests/unit/memory/MemoryManagerSmokeTest.cpp`；`tests/unit/memory/CMakeLists.txt`；`memory/CMakeLists.txt`；2026-04-18 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_interface_compile_unit_test`、`dasall_vector_memory_adapter_unit_test`、`dasall_memory_manager_lifecycle_unit_test`、`dasall_memory_manager_smoke_unit_test`，并验证 `MemoryInterfaceCompileTest`、`VectorMemoryAdapterTest`、`MemoryManagerLifecycleTest`、`MemoryManagerSmokeTest` 全绿 | 仅当生命周期状态机、组合根和 shutdown 语义可执行，且 `WorkingMemoryBoard` owner 不再重复声明时完成 |
| MEM-TODO-012 | Done | 实现 WorkingMemoryBoard 核心与 snapshot 基线 | memory 详设 6.12.4；8.2 Slice 1 | 6.12.4 `WorkingMemoryBoard` | L3 | 新增 `memory/include/working/IWorkingMemoryBoard.h`、`memory/include/working/WorkingMemorySnapshot.h`、`memory/src/working/WorkingMemoryBoard.cpp`；优先落盘 slot / TTL / LRU / snapshot 核心，export / restore facade 已解除 supporting object 阻塞 | `IWorkingMemoryBoard`；`WorkingMemoryBoard::set_slot/get_slot/remove_slot/clear_session/export_snapshot/restore_snapshot/evict_expired` | unit：`WorkingMemoryBoardTest`、`WorkingMemorySnapshotTest`、`WorkingMemoryBoardConcurrencyTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "WorkingMemoryBoard.*Test|WorkingMemorySnapshot.*Test" --output-on-failure` | MEM-TODO-005、008A | 无 | 无 | `memory/include/working/IWorkingMemoryBoard.h`；`memory/include/working/WorkingMemorySnapshot.h`；`memory/src/working/WorkingMemoryBoard.cpp`；`memory/src/MemoryManagerInternal.h`；`memory/src/MemoryManager.cpp`；`memory/src/MemoryManagerFactory.cpp`；`tests/unit/memory/WorkingMemoryBoardTest.cpp`；`tests/unit/memory/WorkingMemorySnapshotTest.cpp`；`tests/unit/memory/WorkingMemoryBoardConcurrencyTest.cpp`；`tests/unit/memory/MemoryManagerSmokeTest.cpp`；`tests/unit/memory/MemoryInterfaceCompileTest.cpp`；`tests/unit/memory/CMakeLists.txt`；`memory/CMakeLists.txt`；2026-04-18 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_interface_compile_unit_test`、`dasall_memory_manager_lifecycle_unit_test`、`dasall_memory_manager_smoke_unit_test`、`dasall_memory_working_memory_board_unit_test`、`dasall_memory_working_memory_snapshot_unit_test`、`dasall_memory_working_memory_board_concurrency_unit_test`，并验证 `MemoryInterfaceCompileTest`、`MemoryManagerLifecycleTest`、`MemoryManagerSmokeTest`、`WorkingMemoryBoardTest`、`WorkingMemorySnapshotTest`、`WorkingMemoryBoardConcurrencyTest` 全绿 | 仅当 TTL、LRU、snapshot/restore 基线和多 session 隔离可二值断言，且 export / restore facade 对接缺口被显式隔离时完成 |
| MEM-TODO-013 | Done | 实现 SqliteSchemaMigrator 与初始 schema | memory 详设 6.7.2b、6.12.5；7 MEM-D004 | 6.7.2b `SqliteSchemaMigrator`；6.12.5 migration 流 | L3 | 新增 `memory/src/store/sqlite/SqliteSchemaMigrator.cpp`、`sql/memory/V001__initial_schema.sql` | `SqliteSchemaMigrator::migrate/status`；`schema_migrations`；`V001__initial_schema.sql` | unit：`SchemaMigrationTest`、`SqliteTransactionTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "SchemaMigration.*Test|SqliteTransaction.*Test" --output-on-failure` | MEM-TODO-005、008A、008B、010 | 无 | 无 | `memory/src/store/sqlite/SqliteSchemaMigrator.h`；`memory/src/store/sqlite/SqliteSchemaMigrator.cpp`；`sql/memory/V001__initial_schema.sql`；`memory/CMakeLists.txt`；`tests/unit/memory/SchemaMigrationTest.cpp`；`tests/unit/memory/SqliteTransactionTest.cpp`；`tests/unit/memory/CMakeLists.txt`；2026-04-18 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_schema_migration_unit_test`、`dasall_memory_sqlite_transaction_unit_test`，并验证 `MemoryInterfaceCompileTest`、`SchemaMigrationTest`、`SqliteTransactionTest` 全绿 | 仅当首次建库、增量 migration、checksum mismatch 拒绝与事务回滚路径都可验证时完成 |
| MEM-TODO-014 | Done | 实现 SqliteMemoryStore 的 Session/Turn/Summary 主路径 | memory 详设 6.7.2a、6.12.5；7 MEM-D004 | 6.12.5 `SqliteMemoryStore` Session/Turn/Summary | L3 | 新增 `memory/src/store/sqlite/SqliteMemoryStore.cpp`、`memory/src/store/sqlite/RowMappers.cpp` 的主链部分 | `open/close`；`load_session_bundle`；`create_session`；`append_turn`；`update_session_active`；`upsert_summary`；`load_latest_summary`；`begin_immediate` | unit：`SqliteMemoryStoreTest`、`SqliteTransactionTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "SqliteMemoryStoreTest|SqliteTransactionTest" --output-on-failure` | MEM-TODO-005、008A、008B、010、013 | 无 | 无 | `memory/src/store/sqlite/SqliteMemoryStore.h`；`memory/src/store/sqlite/SqliteMemoryStore.cpp`；`memory/src/store/sqlite/RowMappers.h`；`memory/src/store/sqlite/RowMappers.cpp`；`memory/src/MemoryManagerFactory.cpp`；`memory/CMakeLists.txt`；`tests/unit/memory/SqliteMemoryStoreTest.cpp`；`tests/unit/memory/SqliteTransactionTest.cpp`；`tests/unit/memory/MemoryManagerSmokeTest.cpp`；`tests/unit/memory/CMakeLists.txt`；2026-04-18 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_manager_smoke_unit_test`、`dasall_memory_schema_migration_unit_test`、`dasall_memory_sqlite_transaction_unit_test`、`dasall_memory_sqlite_store_unit_test`，并验证 `MemoryManagerSmokeTest`、`SchemaMigrationTest`、`SqliteTransactionTest`、`SqliteMemoryStoreTest` 全绿 | 仅当 writer / readers、主链 CRUD 和 RAII 事务语义都可执行时完成 |
| MEM-TODO-015 | Done | 实现 SqliteMemoryStore 的 Fact/Experience 与 maintenance 查询路径 | memory 详设 6.12.5；7 MEM-D004、MEM-D007 | 6.12.5 `FactQuery*`、`ExperienceQuery*`、`quarantine_record` | L3 | 扩展 `memory/src/store/sqlite/SqliteMemoryStore.cpp` 的 Fact/Experience/maintenance 部分 | `query_facts`、`insert_fact`、`supersede_fact`、`query_experiences`、`insert_experience`、`count_turns`、`quarantine_record` | unit：`SqliteMemoryStoreTest`；contract：memory shared contracts 不回退 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -R "SqliteMemoryStoreTest|MemoryFactExperienceContractTest|TurnSessionSummaryMemoryContractTest" --output-on-failure` | MEM-TODO-010、013、014 | 无 | 无 | `memory/src/store/sqlite/SqliteMemoryStore.cpp`；`memory/src/store/sqlite/RowMappers.h`；`memory/src/store/sqlite/RowMappers.cpp`；`tests/unit/memory/SqliteMemoryStoreTest.cpp`；2026-04-18 已通过 CMake Tools 构建 `dasall_memory`、`dasall_memory_sqlite_store_unit_test`、`dasall_memory_sqlite_transaction_unit_test`、`dasall_contract_memory_fact_experience_test`、`dasall_contract_turn_session_summary_memory_test`，并验证 `SqliteTransactionTest`、`SqliteMemoryStoreTest`、`MemoryFactExperienceContractTest`、`TurnSessionSummaryMemoryContractTest` 全绿 | 仅当 Fact / Experience / maintenance 查询写入都可通过同一 store surface 暴露，且不修改 shared contracts 时完成 |
| MEM-TODO-016 | NotStarted | 实现 CandidateCollector | memory 详设 6.8.1、6.12.2；8.2 M3 | 6.12.2 `CandidateCollector` | L3 | 新增 `memory/src/context/CandidateCollector.cpp`；`search_vector` 必须先判断 `VectorMemoryIndexAdapter::is_available()`，不得对 concrete backend 建立硬依赖 | `CandidateCollector::collect`；`load_session_context`；`query_relevant_facts`；`query_relevant_experiences`；`search_vector`；`estimate_tokens` | unit：`CandidateCollectorTest`、`CandidateCollectorVectorOffTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "CandidateCollector.*Test" --output-on-failure` | MEM-TODO-006、010、012、014、015、022 | 无 | 无 | `memory/src/context/CandidateCollector.cpp` | 仅当多源候选收集、独立失败降级、vector unavailable 降级和 token 粗估逻辑都可断言时完成 |
| MEM-TODO-017 | NotStarted | 实现 BudgetAllocator | memory 详设 6.10.2、6.12.2；阶段 G Gate | 6.12.2 `BudgetAllocator` | L3 | 新增 `memory/src/context/BudgetAllocator.cpp` | `BudgetAllocator::allocate`；`compute_slot_budgets`；`compute_trim_actions` | unit：`BudgetAllocatorTest`、`ContextOrchestratorBudgetTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "BudgetAllocatorTest|ContextOrchestratorBudgetTest" --output-on-failure` | MEM-TODO-006、008A | 无 | 无 | `memory/src/context/BudgetAllocator.cpp` | 仅当不同 stage / risk / latency 下的额度分配与 trim actions 都可二值判定时完成 |
| MEM-TODO-018 | NotStarted | 实现 CompressionCoordinator 模板压缩路径 | memory 详设 6.3.1、6.12.2；7 MEM-D005 | 6.12.2 `CompressionCoordinator`；6.3.1 `ISummarizer` 阶段 1/2 | L2 | 实现 `memory/src/writeback/CompressionCoordinator.cpp` 的模板路径、增量合并与 fallback 占位，消费已冻结的 `ISummarizer` 抽象接口 | `CompressionCoordinator::compress`；`extract_structured_summary`；`extract_decisions`；`extract_confirmed_facts`；`extract_tool_outcomes`；`build_summary_text` | unit：`CompressionCoordinatorTest`、`CompressionCoordinatorSummarizerTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "CompressionCoordinator.*Test" --output-on-failure` | MEM-TODO-002、003、005、009A、014 | 无 | 无 | `memory/src/writeback/CompressionCoordinator.cpp` | 仅当模板路径、增量合并、summarizer injection point 与中文关键词边界测试锚点都落地时完成 |
| MEM-TODO-019 | NotStarted | 实现 ContextOrchestrator 与 ContextPacket 槽位映射 | memory 详设 6.8.1、6.12.2；7 MEM-D002 | 6.12.2 `ContextOrchestrator`；6.8.1 slot mapping 表 | L3 | 新增 `memory/src/context/ContextOrchestrator.cpp` | `ContextOrchestrator::assemble`；`build_packet`；`needs_compression` | unit / integration：`ContextOrchestratorTest`、`ContextOrchestratorDegradedTest`、`MemoryContextAssembleIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "ContextOrchestrator.*Test|MemoryContextAssembleIntegrationTest" --output-on-failure` | MEM-TODO-006、016、017、018、025 | 无 | 无 | `memory/src/context/ContextOrchestrator.cpp` | 仅当候选收集、预算裁剪、压缩触发和 10 槽位映射都可稳定输出 `ContextPacket` 时完成 |
| MEM-TODO-020 | NotStarted | 实现 MemoryConflictResolver | memory 详设 6.12.3；7 MEM-D007 | 6.12.3 `MemoryConflictResolver` | L3 | 新增 `memory/src/conflict/MemoryConflictResolver.cpp` | `resolve`；`find_related_facts`；`is_semantically_conflicting`；`determine_action` | unit：`MemoryConflictResolverTest`、`ConflictResolverDegradedTest`、`FactConflictResolverTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R "MemoryConflictResolver.*Test|ConflictResolver.*Test|FactConflictResolverTest" --output-on-failure` | MEM-TODO-010、015 | 无 | 无 | `memory/src/conflict/MemoryConflictResolver.cpp` | 仅当 Accept / Supersede / Reject / Coexist 四路径及仓储查询失败降级都可验证时完成 |
| MEM-TODO-021 | NotStarted | 实现 WritebackCoordinator 核心事务与附属写入分层 | memory 详设 6.8.2a、6.12.3；7 MEM-D006 | 6.12.3 `WritebackCoordinator` | L2 | 新增 `memory/src/writeback/WritebackCoordinator.cpp` | `persist`；`persist_core_transaction`；`persist_derived_data`；`persist_vector_sidecar`；`update_working_board` | unit / integration：`WritebackCoordinatorCoreTest`、`WritebackCoordinatorPartialTest`、`MemoryWritebackIntegrationTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "WritebackCoordinator.*Test|MemoryWritebackIntegrationTest" --output-on-failure` | MEM-TODO-004、007、010、012、014、015、020、022、025 | 无 | 无 | `memory/src/writeback/WritebackCoordinator.cpp` | 仅当 core transaction、partial writeback、vector best-effort 与 audit 发射路径都能按分层语义执行时完成 |
| MEM-TODO-022 | Done | 实现 VectorMemory unavailable baseline 与 IEmbeddingAdapter 接线 | memory 详设 6.3.2、6.12.6；7 MEM-D008 | 6.3.2 `IEmbeddingAdapter`；6.12.6 `VectorMemoryIndexAdapter` | L2 | 在 `memory/include/vector/*`、`memory/src/vector/*` 落盘抽象接口与 unavailable / no-op baseline，为 `CandidateCollector` 和 `WritebackCoordinator` 提供可判定的 availability gate | `IEmbeddingAdapter`；`VectorMemoryIndexAdapter::is_available/upsert/search/health/rebuild_index` | unit：`VectorMemoryAdapterTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests && ctest --test-dir build-ci -R VectorMemoryAdapterTest --output-on-failure` | MEM-TODO-005、008A | 无 | 无 | `memory/include/vector/IEmbeddingAdapter.h`；`memory/include/vector/VectorMemoryIndexAdapter.h`；`memory/src/vector/UnavailableVectorMemoryIndexAdapter.cpp`；`tests/unit/memory/VectorMemoryAdapterTest.cpp`；`tests/unit/memory/CMakeLists.txt`；`memory/CMakeLists.txt`；2026-04-18 已通过 `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_vector_memory_adapter_unit_test dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R '^(VectorMemoryAdapterTest|MemoryInterfaceCompileTest)$' --output-on-failure`，确认 vector disabled / backend unavailable 时 `is_available=false`、upsert/search/rebuild 为 no-op，且 `IEmbeddingAdapter` 不会被错误调用 | 仅当 vector 关闭或 backend 不可用时主链继续可用，且 availability / no-op 语义可断言并足以解开上游前置依赖时完成 |
| MEM-TODO-023 | NotStarted | 评审并冻结 concrete vector backend 选型 | memory 详设 6.3.2、6.7.1、10.2、11.2；阶段 G/H | 6.3.2 backend strategy；10.2 compatibility；11.2 MEM-B03 | L1 | 更新 `docs/architecture/DASALL_memory子系统详细设计.md` 与本专项 TODO，冻结 `sqlite-vss`、`hnswlib` 或 `none` 的默认实现顺序和 profile 策略 | concrete `VectorMemory` backend 决策 | 设计一致性：backend type、profile 默认值、依赖来源、回退策略可检索 | `rg -n "sqlite-vss|hnswlib|VectorMemory|backend_type|edge_minimal" docs/architecture/DASALL_memory子系统详细设计.md docs/todos/memory/DASALL_memory子系统专项TODO.md` | MEM-TODO-022 | MEM-BLK-05 | 完成本任务 | 更新后的 memory 详设；更新后的本专项 TODO | 仅当 concrete backend 选型、关闭条件和 profile fallback 被明确，且不再停留在“可选 sidecar”模糊描述时完成 |
| MEM-TODO-024 | NotStarted | 实现 MemoryMaintenanceWorker | memory 详设 6.12.6；7 MEM-D009 | 6.12.6 `MemoryMaintenanceWorker` | L3 | 新增 `memory/src/maintenance/MemoryMaintenanceWorker.cpp` | `execute`；`run_wal_checkpoint`；`run_turn_retention`；`run_fact_retention`；`run_experience_retention`；`run_quarantine_cleanup`；`run_vector_rebuild` | unit / integration：`MemoryMaintenanceRetentionTest`、`MemoryMaintenanceCheckpointTest`、`MemoryMaintenanceIntegrationTest`、`MemoryCheckpointBusyTest` | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "MemoryMaintenance.*Test|MemoryCheckpointBusyTest" --output-on-failure` | MEM-TODO-008A、008B、010、015、022、025 | 无 | 无 | `memory/src/maintenance/MemoryMaintenanceWorker.cpp` | 仅当 checkpoint / retention / quarantine / rebuild 的分层执行和主动 / 被动调度模式都可验证时完成 |
| MEM-TODO-025 | Done | 注册 memory unit / integration 测试拓扑 | memory 详设 8.1、9.1；SSOT `InfraIntegrationTopology`；当前代码现状 | 8.1 目录建议；9.1 测试矩阵 | L2 | 新增 `tests/unit/memory/*`、`tests/integration/memory/*` 目录与 CMake 注册；更新 `tests/integration/CMakeLists.txt` | `tests/unit/memory/CMakeLists.txt`；`tests/integration/memory/CMakeLists.txt`；`tests/integration/CMakeLists.txt` | discoverability：`ctest -N` 可发现 memory unit / integration 用例 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory_interface_compile_unit_test dasall_memory_integration_topology_smoke_integration_test && ctest --test-dir build-ci -N | rg "MemoryInterfaceCompileTest|MemoryIntegrationTopologySmokeTest"` | MEM-TODO-005 | 无 | 无 | `tests/integration/memory/CMakeLists.txt`；`tests/integration/memory/MemoryIntegrationTopologySmokeTest.cpp`；`tests/integration/CMakeLists.txt`；2026-04-18 已通过 CMake Tools 发现 `dasall_memory_integration_topology_smoke_integration_test` 与 `MemoryIntegrationTopologySmokeTest`，并验证 `MemoryInterfaceCompileTest`、`MemoryIntegrationTopologySmokeTest` 通过；`build-ci` 的 `ctest -N` 已同时列出两条 memory unit / integration 用例 | 仅当 memory 测试拓扑进入顶层聚合且 discoverable，不再依赖占位目录时完成 |
| MEM-TODO-026 | NotStarted | 验证 MemoryManager smoke 与 context assemble 最小闭环 | memory 详设 8.2、8.3、9.1；阶段 G Gate | 8.2 MEM-M1~M3；8.3 Slice 1/2 | L2 | 新增 smoke / integration 用例代码 | `MemoryManagerSmokeTest`；`MemoryManagerLifecycleTest`；`MemoryContextAssembleIntegrationTest`；`ContextOrchestratorBudgetTest` | unit / integration：init -> prepare_context -> export -> shutdown 最小闭环可执行 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_integration_tests && ctest --test-dir build-ci -R "MemoryManagerSmokeTest|MemoryManagerLifecycleTest|MemoryContextAssembleIntegrationTest|ContextOrchestratorBudgetTest" --output-on-failure` | MEM-TODO-011、012、016、017、018、019、025 | 无 | 无 | `tests/unit/memory/MemoryManagerSmokeTest.cpp`；`tests/integration/memory/MemoryContextAssembleIntegrationTest.cpp` | 仅当上下文连续性与预算压缩 gate 可通过自动化证明时完成 |
| MEM-TODO-027 | NotStarted | 验证 writeback 主链与 contracts 兼容 Gate | memory 详设 8.2、9.1、9.2；WP05-T006/T007 | 8.2 MEM-M4；9.1 integration / contract matrix | L2 | 新增 writeback integration 用例并串联 contracts gate | `MemoryWritebackIntegrationTest`；`TurnSessionSummaryMemoryContractTest`；`MemoryFactExperienceContractTest`；`ContextPacketFieldContractTest` | integration / contract：Turn / Summary / Fact / Experience 写回闭环和 shared contracts 边界同时通过 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -R "MemoryWritebackIntegrationTest|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest" --output-on-failure` | MEM-TODO-019、020、021、025 | 无 | 无 | `tests/integration/memory/MemoryWritebackIntegrationTest.cpp`；更新后的 `tests/contract/CMakeLists.txt` 复用链路 | 仅当 writeback 主路径全绿且 shared contracts / `ContextPacket` 边界无回退时完成 |
| MEM-TODO-028 | NotStarted | 验证 failure injection 与 checkpoint busy 路径 | memory 详设 6.9.1、9.1、9.4；7 MEM-D009、MEM-D010 | 9.4 失败注入测试点；6.9.1 `SQLITE_BUSY` 路径 | L2 | 新增 failure injection 用例 | `MemoryFailureInjectionTest`；`MemoryCheckpointBusyTest` | failure：BUSY、schema mismatch、disk full、summary quarantine、vector off 至少覆盖主要分支 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_integration_tests && ctest --test-dir build-ci -R "MemoryFailureInjectionTest|MemoryCheckpointBusyTest" --output-on-failure` | MEM-TODO-021、024、025 | 无 | 无 | `tests/integration/memory/MemoryFailureInjectionTest.cpp`；`tests/integration/memory/MemoryCheckpointBusyTest.cpp` | 仅当每类故障都能产出明确 result / warning / audit 证据，而非静默吞错时完成 |
| MEM-TODO-029 | NotStarted | 验证 profile compatibility 与 discoverability Gate | memory 详设 6.10.3、9.1、9.5、10.2；阶段 G/H | 6.10.3 profile defaults；9.5 quality gates | L2 | 新增 profile integration 用例并校验 discoverability | `MemoryProfileCompatibilityTest`；`ctest -N` discoverability | integration：`desktop_full`、`edge_balanced`、`edge_minimal` 的 vector / maintenance / compression / WAL 策略差异可验证 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci -R MemoryProfileCompatibilityTest --output-on-failure` | MEM-TODO-008A、022、023、024、025 | MEM-BLK-05 | 完成 MEM-TODO-023 | `tests/integration/memory/MemoryProfileCompatibilityTest.cpp`；更新后的 tests topology | 仅当 profile 差异与 discoverability 同时可验证，且 edge profile 不被强行要求启用向量能力时完成 |
| MEM-TODO-030 | NotStarted | 回写 memory 专项 Gate 与交付证据 | memory 详设 7、8、9；文档治理基线；工程执行规范 | 7 Design -> Build；9 质量门；docs/worklog 回写原则 | L2 | 更新 `docs/todos/memory/DASALL_memory子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` | Gate 结果、命令证据、blocker 状态、后续动作 | 交付回写：全量命令、通过 / 失败结论、风险残留、下一步均有证据 | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -R "Memory.*Test|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest"` | MEM-TODO-026、027、028、029 | 无 | 无 | 更新后的本专项 TODO；更新后的 `docs/worklog/DASALL_开发执行记录.md` | 仅当每个 Gate 都有命令证据、状态更新和后续动作回写时完成 |

---

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 补设计解阻 | MEM-TODO-001 ~ 004 | 已全部完成 | supporting object 与 ownership / dependency 歧义已收敛，可作为后续 Build 的稳定前置 |
| B 公共 ABI、测试拓扑与基线适配器 | MEM-TODO-005、025、006、007、008A、008B、009、009A、010、022 | 005、025 已完成；006/007/008A/008B/010/022 可并行；009 依赖 001；009A 依赖 002 | 已建立 include 布局、unit/integration discoverability 基线、`MemoryInterfaceCompileTest` 与 `MemoryIntegrationTopologySmokeTest`；下一步继续冻结 supporting types、config/error、store、summarizer 与 vector unavailable baseline |
| C 生命周期与主存储基线 | MEM-TODO-011 ~ 015 | 011/012/013/014/015 已完成 | 已落 `MemoryManager` 生命周期骨架、`WorkingMemoryBoard` 核心、SQLite schema migration baseline 与 `SqliteMemoryStore` 全部主存储路径；下一步进入 Context 与 Writeback 组装链 |
| D Context 组装链 | MEM-TODO-016 ~ 019 | 016/017 可并行；018 依赖 002/003/009A；019 串联收口 | `CandidateCollector` / `BudgetAllocator` 先稳定，再接 `CompressionCoordinator` 和 `ContextOrchestrator`；022 已前移，不再形成阶段循环 |
| E 写回与冲突链 | MEM-TODO-020 ~ 021 | 020 先于 021 | 先把 conflict plan 规则独立出来，再做 core transaction + derived writes |
| F 向量评审与 Maintenance | MEM-TODO-023 ~ 024 | 023 为评审门；024 可在 022、025 后推进 | concrete backend 评审不阻塞主链，maintenance 在主链稳定后接入 |
| G Tests / Gates | MEM-TODO-026 ~ 030 | 026 为关键里程碑：Context Assemble Smoke Gate；027/028 可分段推进；029 依赖 023；030 最终收口 | 先过 Context assemble smoke，再做 writeback、failure、profile 与证据回写 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| MEM-GATE-01 | supporting types 冻结门 | 阶段 A-B 结束前 | export / summarize supporting objects、writeback/context objects、config/error 面全部落盘且无未定义签名引用 | 回退到补设计任务，不进入 manager / compression / writeback 实现 |
| MEM-GATE-02 | 公共接口冻结门 | 阶段 B 结束前 | `IMemoryManager`、`IContextOrchestrator`、`ISummarizer`、`IMemoryStore`、`IStoreTransaction` 头文件齐备且不进入 contracts admission | 回退接口层，禁止直接写实现 |
| MEM-GATE-03 | placeholder 移除门 | 阶段 C 前 | `dasall_memory` 不再只依赖 `src/placeholder.cpp`，`memory/include` 真实存在 | 修复 CMake / include 布局后再继续 |
| MEM-GATE-04 | Context 组装门 | 阶段 D 收口时 | `MemoryContextAssembleIntegrationTest`、`ContextOrchestratorBudgetTest` 通过；预算裁剪符合 goal/constraints/latest_observation 优先级 | 回退 ContextOrchestrator / BudgetAllocator / CompressionCoordinator |
| MEM-GATE-05 | Writeback 主链门 | 阶段 E 收口时 | `MemoryWritebackIntegrationTest`、`FactConflictResolverTest` 通过；partial / retryable 语义正确 | 回退 WritebackCoordinator / ConflictResolver / store 写路径 |
| MEM-GATE-06 | failure injection 门 | 阶段 G 前 | `MemoryFailureInjectionTest`、`MemoryCheckpointBusyTest` 通过；故障有明确 warning/result/audit | 回退 failure handling / maintenance / error mapping |
| MEM-GATE-07 | contracts compatibility 门 | 任意 shared contracts 相关改动后 | `TurnSessionSummaryMemoryContractTest`、`MemoryFactExperienceContractTest`、`ContextPacketFieldContractTest` 全绿 | 回退 shared object 使用方式，不修改 contracts |
| MEM-GATE-08 | profile / discoverability 门 | 最终收口前 | `MemoryProfileCompatibilityTest` 通过，且 `ctest -N` 能发现 memory unit/integration 测试 | 先修 tests topology 与 profile 投影，不宣称 ready |
| MEM-GATE-09 | ADR 边界一致性门 | 每个阶段收口时 | `ContextPacket` 不含 final_messages / provider_payload；`WritebackCoordinator` 不含 retry / replan / abort_safe 执行控制；`MemoryManager` 不形成独立主循环；include / link 依赖图不包含 cognition / llm / tools 具体实现头 | 回退越权字段、删除非法依赖并重新评审架构边界 |

---

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响范围 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|---|
| MEM-BLK-01 | 已解除：`WorkingMemoryExportRequest/Result` 已在详设 6.5.3a 与 6.12.1 补齐字段表、owner 和 facade 返回语义（2026-04-17） | 不再阻塞 facade / smoke / export path；`WorkingMemoryBoard` 的 export / restore facade 对接条件已具备 | MEM-TODO-009、011；MEM-TODO-012 的 export / restore facade 对接 | 已满足：导出 request/result 字段、owner、返回语义、文件位置已冻结 | 无；继续保持 `MemoryManager` 包装 export envelope、`IWorkingMemoryBoard` 只返回 `WorkingMemorySnapshot` |
| MEM-BLK-02 | 已解除：`SummaryGenerationRequest/Result` 与 `SummaryProjection` 已在详设 6.5.3b 与 6.12.2 补齐 schema、fallback 与 module-local 边界（2026-04-17） | 不再阻塞 `ISummarizer`、Compression 阶段 2 与测试接口定义 | MEM-TODO-009A、018 | 已满足：supporting object schema、模板路径与 summarizer 路径切换边界已冻结 | 无；继续保持 `SummaryProjection` 只作为 module-local 中间对象，摘要持久化统一由 `CompressionCoordinator` 映射到 `SummaryMemory` |
| MEM-BLK-03 | 已解除：`CompressionCoordinator` 的构造依赖与摘要持久化 surface 已固定为 `IMemoryStore`，`SummaryRepository` 仅保留为逻辑职责名（2026-04-17） | 不再阻塞 Compression 实现、工厂 wiring 与测试 stub | MEM-TODO-018、019 | 已满足：构造依赖、逻辑职责层与持久化 owner 已统一 | 无；继续保持 `CompressionCoordinator` 只注入 `IMemoryStore` 与可选 `ISummarizer`，不新增独立 `SummaryRepository` port |
| MEM-BLK-04 | 已解除：`WritebackCoordinator` 为 `WorkingMemoryBoard` 更新唯一 owner，`MemoryManager` 仅保留 facade 转发与 export snapshot 边界（2026-04-17） | 不再阻塞 writeback 实现、manager smoke 与测试归因 | MEM-TODO-011、021 | 已满足：唯一 owner、调用顺序与 facade 边界已统一 | 无；继续保持 `MemoryManager` 不直接修改 working board，所有 writeback mutation 统一经 `WritebackCoordinator::update_working_board()` |
| MEM-BLK-05 | concrete vector backend 选型未冻结，`sqlite-vss` / `hnswlib` / `none` 的默认顺序不明确 | vector concrete backend、profile compatibility | MEM-TODO-023、029 | 冻结 backend strategy、profile 默认值与 fallback | 完成 MEM-TODO-023 |

当前活跃阻塞项：MEM-BLK-05。MEM-BLK-01 / 02 / 03 / 04 已解除，且与 MEM-TODO-009 / 009A / 011 / 012 / 018 / 019 / 021 的前置关系一致。

---

## 9. 验收与质量门

### 9.1 验收矩阵

| 验收层 | 覆盖对象 | 核心用例 | 通过标准 |
|---|---|---|---|
| Design Consistency | MEM-TODO-001 ~ 004 | export / summarize supporting objects、ownership / dependency 口径、owner 描述一致性检索 | 相关字段 / 接口 / owner 描述可通过 `rg` 命中且无双口径矛盾 |
| Unit | `WorkingMemoryBoard`、`BudgetAllocator`、`CompressionCoordinator`、`MemoryConflictResolver`、`SqliteSchemaMigrator`、`SqliteMemoryStore` | TTL/LRU、budget trim、summary fallback、conflict resolution、migration checksum、transaction rollback | 组件级断言全绿，错误分类稳定 |
| Contract | `Turn`、`Session`、`SummaryMemory`、`MemoryFact`、`ExperienceMemory`、`ContextPacket` | required / field-rule / boundary 守卫、ContextPacket 槽位边界 | 既有 contract tests 不回退 |
| Integration | `MemoryManager`、`ContextOrchestrator`、`WritebackCoordinator`、`MemoryMaintenanceWorker` | assemble 主路径、writeback 主路径、checkpoint/retention、profile diff | 关键主链路用例可重复执行 |
| Failure Injection | SQLite BUSY、schema mismatch、disk full、summary quarantine、vector off | retryable / degraded / fatal / quarantine mapping | 每类故障都有明确结果与观测证据 |
| Compatibility | `desktop_full`、`edge_balanced`、`edge_minimal` | vector 开关、compression 强度、WAL/checkpoint 策略差异 | profile 行为与配置一致 |

### 9.2 质量 Gate 命令基线

说明：除文档一致性与静态检索型 gate 外，以下命令统一默认以前缀 `cmake -S . -B build-ci -G "Unix Makefiles" &&` 完成配置；仅展示 `ctest` 片段的条目表示需在对应 build target 已完成后执行。

| Gate | 建议命令 | 说明 |
|---|---|---|
| Public surface compile | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_memory_interface_compile_unit_test && ctest --test-dir build-ci -R "MemoryInterfaceCompileTest" --output-on-failure` | 公共头与 supporting types 编译门 |
| Working memory | `ctest --test-dir build-ci -R "WorkingMemoryBoard.*Test|WorkingMemorySnapshot.*Test" --output-on-failure` | 黑板、TTL、snapshot、并发 |
| Store / migration | `ctest --test-dir build-ci -R "SqliteMemoryStoreTest|SchemaMigration.*Test|SqliteTransaction.*Test" --output-on-failure` | schema / CRUD / 事务 |
| Context assemble | `ctest --test-dir build-ci -R "ContextOrchestrator.*Test|MemoryContextAssembleIntegrationTest|ContextOrchestratorBudgetTest" --output-on-failure` | assemble / trim / compression gate |
| Writeback | `ctest --test-dir build-ci -R "WritebackCoordinator.*Test|MemoryWritebackIntegrationTest|FactConflictResolverTest" --output-on-failure` | 核心事务、partial、冲突 |
| Failure injection | `ctest --test-dir build-ci -R "MemoryFailureInjectionTest|MemoryCheckpointBusyTest" --output-on-failure` | BUSY / degrade / checkpoint |
| Contract compatibility | `ctest --test-dir build-ci -R "TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest" --output-on-failure` | shared contracts 与 ContextPacket 边界 |
| ADR boundary audit | `rg -n "final_messages|provider_payload|rendered_prompt|retry_step|replan|abort_safe|#include .*cognition|#include .*llm|#include .*tools" memory/CMakeLists.txt memory/include memory/src tests` | 复核越权字段与非法实现依赖；目标是零越界命中或仅保留经 ADR 白名单评审的命中 |
| Final gate | `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_memory dasall_unit_tests dasall_contract_tests dasall_integration_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -R "Memory.*Test|TurnSessionSummaryMemoryContractTest|MemoryFactExperienceContractTest|ContextPacketFieldContractTest"` | 全量 discoverability + unit/contract/integration |

---

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 缓解动作 | 回退策略 |
|---|---|---|---|---|
| 上下文权回流到 llm | High | 为了图省事在 memory 内直接拼 messages / provider payload | 严格执行 ADR-006 检查；`ContextPacket` 只做语义槽位 | 回退到 `ContextOrchestrator -> ContextPacket` 单向产出 |
| recovery 越权进入 memory | High | 在 writeback / maintenance 中加入 retry / compensate 执行控制 | 所有恢复相关字段只允许作为结果沉淀，不允许作为执行指令 | 回退到 `WritebackResult.retryable_storage_failure` + runtime 决策 |
| schema 漂移或 migration 破坏兼容 | High | 直接改表、改 checksum 或无版本迁移 | 统一走 `SqliteSchemaMigrator`、checksum 与 `schema_migrations` | 启动失败即拒绝进入 Running，回退到上一稳定 schema |
| SQLite 版本基线不满足 | High | CI / 目标环境的 SQLite 版本低于 3.51.3，仍尝试启用 WAL + 多线程写入 / 检查点并发 | 在 `third_party` 或 CMake FetchContent 中显式管控 SQLite 版本，并在 configure / build gate 增加 version probe | 未满足基线时禁止宣称 WAL 并发 ready，回退到已回补版本或受控单线程 smoke 配置 |
| WAL 膨胀 / 锁争用放大 | Medium | 长读事务、checkpoint 饥饿、持锁做 I/O | 单 writer、多 reader、PASSIVE checkpoint、出锁后 I/O、busy timeout 指标化 | 回退到更保守的 checkpoint / profile 配置，必要时关闭 auto maintenance |
| Summary 模板误判 | Medium | 中文关键词启发式命中重复、否定句误判、英文混排召回下降 | 保留关键词边界测试、允许 `summarizer_fallback` / no-summary 降级 | 回退到 recent turns + latest summary，不强制写新 summary |
| vector backend 拖垮主链 | Medium | embedding/backend 不可用仍参与核心事务 | 先实现 unavailable/no-op baseline，concrete backend 单列评审 | profile 下默认关闭 VectorMemory，不影响 Session/Turn/Summary |
| mock 基础设施不足 | Medium | 继续使用仅具 KV 能力的 `MockMemoryStore` 承担 `IMemoryStore` 事务 / 查询测试 | 在 MEM-TODO-010 / 025 中补 `FakeMemoryStore` 或升级 `MockMemoryStore`，覆盖事务、query 和 failure semantics | 暂时把关键验证上移到 integration / contract gate，延后依赖高保真 fake 的 unit 案例宣称 ready |
| tests 拓扑不可发现 | Medium | memory unit/integration 未进入顶层 CMake | 把 tests topology 作为显式任务和 gate | 回退到只声称 contracts gate，有 discoverability 再宣称 ready |
| ownership 冲突导致实现分叉 | Medium | `WorkingMemoryBoard` 更新或 summary 持久化 owner 双口径 | 把补设计任务前置到阶段 A | 回退到单点 owner，删除重复责任描述 |

---

## 11. 可行性结论

### 11.1 是否可以直接进入执行

可以进入执行，但不是“无前置条件直接编码”。

当前建议的执行方式：

1. MEM-TODO-001 ~ 007、008A、008B、009、009A、010、011、012、013、014、015、022、025 已完成；公共 ABI、测试拓扑、vector unavailable baseline、manager lifecycle skeleton、working-board 核心、schema migration baseline 与 sqlite store 全路径均已收口。
2. 再进入 MEM-TODO-016 ~ 021 的主链实现，按 Context -> Writeback 的顺序收口。
3. 主链稳定后执行 MEM-TODO-023、024、026 ~ 030，其中 MEM-TODO-023 是当前唯一剩余评审 blocker，MEM-TODO-026 是关键里程碑：Context Assemble Smoke Gate。

### 11.2 当前可落到的最细粒度

当前最细可直接落到：

1. L3：context / writeback / config 对象定义、store/query structs、WorkingMemoryBoard、CandidateCollector、BudgetAllocator、MemoryConflictResolver、SqliteSchemaMigrator、SqliteMemoryStore、MaintenanceWorker。
2. L2：`IMemoryManager`、`IContextOrchestrator`、`ISummarizer`、`MemoryManager`、`CompressionCoordinator`、`WritebackCoordinator`、`VectorMemoryIndexAdapter`。
3. L0：仅限 1 个未解 blocker 项，不得伪装成 Build-ready 任务。

### 11.3 不可继续细化的证据缺口

当前仍阻止个别任务进入纯函数级拆分的最小证据缺口只有：

1. concrete vector backend 未冻结。

在这 1 项之外，当前 detailed design 已经足以支撑接口级、函数语义级和数据结构级的专项 TODO 拆解。