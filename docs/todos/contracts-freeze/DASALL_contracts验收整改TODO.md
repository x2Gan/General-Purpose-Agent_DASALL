# DASALL contracts 验收整改 TODO

最近更新时间：2026-03-23

## 1. 文档目标

本清单用于将 2026-03-23 `contracts/` 验收报告中识别出的 4 个关键问题，以及后续补充识别出的 `boundary/` 可发现性与组织优化问题，拆分为可执行、可追踪、可验收的最小原子整改任务。

本清单遵循 `contracts-freeze` 既有执行纪律：

1. 每个任务只解决一个明确问题。
2. 每个任务均包含代码目标、测试目标、验收命令三件套。
3. 每个任务必须能给出二值结论：通过或阻断。
4. 设计变更不得绕开 blueprint、实施计划和已冻结 ADR。

---

## 2. 研读资料基线

1. [docs/todos/contracts-freeze/deliverables/DASALL_contracts交付验收报告-2026-03-23.md](docs/todos/contracts-freeze/deliverables/DASALL_contracts%E4%BA%A4%E4%BB%98%E9%AA%8C%E6%94%B6%E6%8A%A5%E5%91%8A-2026-03-23.md)
2. [docs/plans/DASALL_contracts冻结实施计划.md](docs/plans/DASALL_contracts冻结实施计划.md)
3. [docs/plans/DASALL_工程落地实现步骤指引.md](docs/plans/DASALL_%E5%B7%A5%E7%A8%8B%E8%90%BD%E5%9C%B0%E5%AE%9E%E7%8E%B0%E6%AD%A5%E9%AA%A4%E6%8C%87%E5%BC%95.md)
4. [docs/architecture/DASSALL_Agent_architecture.md](docs/architecture/DASSALL_Agent_architecture.md)
5. [docs/architecture/DASALL_Engineering_Blueprint.md](docs/architecture/DASALL_Engineering_Blueprint.md)
6. [docs/architecture/DASALL_contracts目录设计说明.md](docs/architecture/DASALL_contracts%E7%9B%AE%E5%BD%95%E8%AE%BE%E8%AE%A1%E8%AF%B4%E6%98%8E.md)
7. [docs/architecture/DASALL_boundary治理与优化说明.md](docs/architecture/DASALL_boundary%E6%B2%BB%E7%90%86%E4%B8%8E%E4%BC%98%E5%8C%96%E8%AF%B4%E6%98%8E.md)
8. 现有 `contracts/include/`、`tests/contract/`、`scripts/ci/` 代码与 gate 资产

---

## 3. 完成标准

1. 5 组问题均拆分为最小原子任务，不混合交付目标。
2. 每个任务都包含：输入依据、代码目标、测试目标、验收命令、完成判定。
3. 若涉及 blueprint 对象新增，必须同步补齐 contract tests 和差异矩阵回写。
4. 若涉及 gate 扩展，必须同步补齐 required tests 列表与负向门禁验证。

---

## 4. 问题 -> 原子任务拆分总表

| 问题编号 | 问题描述 | 原子任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|---|
| P1 | 序列化稳定性未覆盖 blueprint 口径的全核心对象 | T001-T004 | 扩展序列化 schema/adapter/测试矩阵/coverage gate | 新增对象 round-trip/unknown field/backward compatibility tests | 见各任务 |
| P2 | E2E 仅覆盖单 Agent 主链路 | T005-T007 | 增补工具调用、多 Agent、checkpoint 恢复 e2e contract tests | 3 条新 e2e 链路通过 | 见各任务 |
| P3 | blueprint 对象与当前 contracts 存在显式差异 | T008-T011 | 新增缺失对象或明确替代映射目录 | 新对象/映射测试与矩阵验证通过 | 见各任务 |
| P4 | 嵌入式资源边界与 wire-level 约束未显式化 | T012-T015 | 新增资源约束与 wire contract 规则文档/guards/tests | 资源边界与 wire 规则自动化校验通过 | 见各任务 |
| P5 | boundary 目录的职责分层、Gate 命名与可发现性不足 | T016-T019 | 新增目录索引、调整 milestone gate 组织、补语义化别名与迁移说明 | 新目录组织与 alias 头文件可编译、文档与 smoke tests 同步 | 见各任务 |

---

## 5. 原子任务清单

| 子任务 ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| T001 | Not Started | 补齐主链路对象序列化兼容测试 | blueprint 契约测试要求、P1 | 扩展 `tests/contract/serialization/SerializationCompatibilityContractTest.cpp`，覆盖 `GoalContract`、`Observation`、`ObservationDigest`、`BeliefState`、`Checkpoint`、`AgentResult` 的最小稳定 schema round-trip | 新增对应 round-trip、required missing、unknown field tolerant 子测试 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R SerializationCompatibilityContractTest --output-on-failure` | 主链路对象均纳入序列化契约测试，测试通过 |
| T002 | Not Started | 补齐边界对象序列化兼容测试 | blueprint 契约测试要求、P1 | 在 `tests/contract/serialization/` 新增边界对象序列化测试文件，覆盖 `PromptComposeRequest`、`PromptComposeResult`、`ReflectionDecision`、`RecoveryRequest`、`RecoveryOutcome`、`MultiAgentRequest`、`MultiAgentResult`、`WorkerTask`、`WorkerLease` | 新增边界对象 round-trip、legacy field migration、forbidden wire field negative tests | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "SerializationCompatibilityContractTest|BoundarySerializationCompatibilityContractTest" --output-on-failure` | 边界对象进入可执行 serialization suite |
| T003 | Not Started | 补齐子域对象序列化兼容测试 | blueprint 契约测试要求、P1 | 在 `tests/contract/serialization/` 新增子域对象序列化测试，覆盖 `ToolRequest`、`ToolResult`、`ToolDescriptor`、`ToolIR`、`Turn`、`Session`、`SummaryMemory`、`MemoryFact`、`ExperienceMemory`、`LLMRequest`、`LLMResponse`、`EventType`、`EventPayload`、`SubTaskGraph` | 新增 round-trip、enum fallback、schema extension tolerant 子测试 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "SerializationCompatibilityContractTest|SubdomainSerializationCompatibilityContractTest" --output-on-failure` | 子域对象进入 serialization suite |
| T004 | Not Started | 扩展覆盖矩阵与 gate 到“全核心对象序列化口径” | P1、现有 CoverageMatrix/V1 gate | 更新 `contracts/include/boundary/CoverageMatrixGuards.h`、`contracts/include/boundary/V1ReadyChecklistGuards.h`、`scripts/ci/wp05_contract_gate.sh` 或后续 gate 脚本，新增 serialization 覆盖对象与 required tests | `CoverageMatrixContractTest`、`V1ReadyChecklistContractTest`、gate 负向用例全部更新 | `bash scripts/ci/wp05_contract_gate.sh` | gate 对新增 serialization 资产具有阻断能力 |
| T005 | Not Started | 增补工具调用 e2e contract test | blueprint e2e 要求、P2 | 在 `tests/contract/e2e/` 新增工具调用链路测试，覆盖 `AgentRequest -> ToolRequest -> ToolResult -> Observation -> ObservationDigest -> AgentResult` | 新增单文件 e2e contract test 并接入 `tests/contract/CMakeLists.txt` | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ToolCallContractE2ETest --output-on-failure` | 工具调用链路具备端到端 contract 验证 |
| T006 | Not Started | 增补多 Agent orchestrator-worker e2e contract test | blueprint e2e 要求、P2 | 在 `tests/contract/e2e/` 新增多 Agent 链路测试，覆盖 `MultiAgentRequest -> WorkerTask/WorkerLease -> MultiAgentResult -> AgentResult` | 新增多 Agent e2e contract test 并接入 CMake | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentContractE2ETest --output-on-failure` | 多 Agent 链路具备端到端 contract 验证 |
| T007 | Not Started | 增补 checkpoint 恢复 e2e contract test | blueprint e2e 要求、P2 | 在 `tests/contract/e2e/` 新增恢复链路测试，覆盖 `Checkpoint -> ReflectionDecision -> RecoveryRequest -> RecoveryOutcome -> AgentResult/继续执行` | 新增恢复 e2e contract test 并接入 CMake | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryContractE2ETest --output-on-failure` | checkpoint 恢复链路具备端到端 contract 验证 |
| T008 | Not Started | 明确 blueprint 缺失对象的“新增/替代/延期”分类 | P3、差异矩阵 | 新增或更新差异矩阵文档，逐项标记 `Implemented / Replaced / Deferred / Missing`，并给出 owner 与下一步动作 | 为差异矩阵新增 smoke test 输入清单或 catalog 检查资产 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ObjectBoundaryCatalogContractTest --output-on-failure` | 缺失对象状态不再模糊 |
| T009 | Not Started | 落地 blueprint 中明确缺失且低耦合的对象第一批 | P3、blueprint 对象清单 | 新增第一批低耦合对象：`AgentInitConfig`、`ResumeToken`、`SessionSnapshot`、`ModelRoute`、`StreamHandle` | 为新增对象补齐 contract tests | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "StableTypePresenceContractTest|.*ContractTest" --output-on-failure` | 第一批低耦合 blueprint 对象进入 contracts |
| T010 | Not Started | 落地 context/policy/task 缺失对象或建立替代映射 | P3、blueprint 对象清单 | 新增 `ContextAssembleRequest`、`ContextAssembleResult`、`CompressionRequest`、`CompressionResult`、`PolicyDecision`、`PromptPolicyDecision`、`TaskRequest`、`TaskState`，或在无法立即新增时提供正式替代映射 catalog | 为新增对象或替代映射补齐 contract tests | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "InterfaceCatalogContractTest|StableTypePresenceContractTest|.*ContractTest" --output-on-failure` | 中间对象与策略对象不再处于隐式缺位状态 |
| T011 | Not Started | 处理 `TaskGraph` / `ToolRoute` / `CompensationAction` 等高耦合对象 | P3、blueprint 对象清单 | 新增这些对象，或在 `TaskDomainContracts` / `tool/` 下建立正式替代关系说明与守卫 | 新增 catalog/guard/test | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "TaskDomainContractTest|ToolDescriptorIRContractTest|CoverageMatrixContractTest" --output-on-failure` | blueprint 差异缩减到可解释范围 |
| T012 | Not Started | 显式化对象资源预算规则 | P4、嵌入式长期运行要求 | 新增 `contracts/include/boundary/ResourceBudgetGuards.h` 或同类目录资产，定义对象字段数、字符串/数组上界、推荐 payload 边界 | 新增 `ResourceBudgetContractTest.cpp` | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ResourceBudgetContractTest --output-on-failure` | contracts 首次具备嵌入式资源边界守卫 |
| T013 | Not Started | 显式化 wire-level 约束规则 | P4、实施计划排除项、后续量产需求 | 新增 `WireContractSchema` 或同类 schema catalog，定义 canonical wire names、unknown field 策略、deprecated field 策略、binary/text transport 预留位 | 新增 wire schema contract test | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R WireContractSchemaContractTest --output-on-failure` | 语义契约向 wire 契约过渡有正式规则 |
| T014 | Not Started | 将嵌入式资源规则接入 gate | P4、现有 V1 gate | 更新 coverage/checklist/gate，使资源预算与 wire schema 成为 required gate | 更新 smoke/gate tests 和负向 gate 验证 | `bash scripts/ci/wp05_contract_gate.sh` 或后续 gate 脚本 | 资源规则未通过时 gate 可阻断 |
| T015 | Not Started | 回写计划与差异矩阵，统一对外口径 | P1-P4 完成后 | 回写实施计划、TODO、验收报告、差异矩阵，明确 V1 与 blueprint 全量口径区别 | 文档一致性检查与对应 smoke test 若存在则通过 | `ctest --test-dir build-ci -R "V1ReadyChecklistContractTest|CoverageMatrixContractTest" --output-on-failure` | 文档、代码、gate 三者口径一致 |
| T016 | Not Started | 新增 boundary 目录索引资产 | P5、boundary 治理说明 | 新增 `contracts/include/boundary/README.md` 或 `contracts/include/boundary/BoundaryIndex.h`，按 `catalog / guards / governance / milestones / adr` 说明职责与入口文件 | 新增 `BoundaryIndexContractTest.cpp` 或更新现有 smoke tests，校验关键分组项存在且索引可追溯 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "BoundaryGuardsContractTest|ObjectBoundaryCatalogContractTest|BoundaryIndexContractTest" --output-on-failure` | 新成员可通过单一索引定位 boundary 目录职责和入口 |
| T017 | Not Started | 将 milestone gate 文件与普通 boundary guard 分组 | P5、boundary 治理说明 | 将 `M2ChecklistGuards.h`、`M3ChecklistGuards.h`、`M4ChecklistGuards.h`、`V1ReadyChecklistGuards.h`、`CoverageMatrixGuards.h`、`DomainRolloutGuards.h` 收拢到 `contracts/include/boundary/milestones/`，必要时保留兼容转发头 | 相关 smoke tests、contract tests、include 路径构建全部通过 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "M2ChecklistContractTest|M3ChecklistContractTest|M4ChecklistContractTest|V1ReadyChecklistContractTest|CoverageMatrixContractTest|DomainRolloutContractTest" --output-on-failure` | boundary 目录层级能直接区分“对象边界守卫”和“里程碑 Gate” |
| T018 | Not Started | 为 milestone gate 增加语义化别名头文件 | P5、boundary 治理说明 | 新增语义化别名头：`CrossCuttingFreezeChecklist.h`、`MainFlowFreezeChecklist.h`、`BoundaryFreezeChecklist.h`、`ContractsV1ReadyChecklist.h`，内部转发到现有或新路径 | 新增 `BoundaryMilestoneAliasContractTest.cpp`，验证新旧 include 均可用且返回结果一致 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "BoundaryMilestoneAliasContractTest|M2ChecklistContractTest|M3ChecklistContractTest|M4ChecklistContractTest|V1ReadyChecklistContractTest" --output-on-failure` | 后续维护者可通过语义化文件名理解 milestone gate 用途 |
| T019 | Not Started | 回写 boundary 相关文档、测试与迁移说明 | P5、T016-T018 | 更新 `docs/architecture/DASALL_boundary治理与优化说明.md`、freeze deliverables、如有必要新增迁移说明，统一 boundary 新目录结构、旧名兼容策略和引用规范 | 文档一致性检查通过，相关 smoke tests 无回归 | `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "BoundaryGuardsContractTest|ADRBoundaryRegressionContractTest|CoverageMatrixContractTest|V1ReadyChecklistContractTest" --output-on-failure` | boundary 目录说明、代码组织与测试入口三者一致 |

---

## 6. 推荐执行顺序

1. 先做 P1：T001 -> T004，先把“验收口径超前”问题纠正到可自动化验证。
2. 再做 P2：T005 -> T007，把 blueprint 要求的 e2e 补齐。
3. 再做 P3：T008 -> T011，收敛 blueprint 对象差异。
4. 再做 P5：T016 -> T019，优化 boundary 的可发现性与目录组织。
5. 最后做 P4：T012 -> T015，把 contracts 从“语义冻结”推进到“嵌入式/wire 约束显式化”。

---

## 7. 阻塞项与解阻条件

| 阻塞项 | 影响任务 | 解阻条件 |
|---|---|---|
| BLK-01：当前测试基础设施只适配单一 serialization 文件 | T001-T004 | 将 `tests/contract/CMakeLists.txt` 扩展为支持多 serialization executable 或统一编排 |
| BLK-02：多 Agent / recovery 链路尚无足够 mock fixture | T005-T007 | 在 `tests/mocks/` 增补对应 mock 和稳定样例对象 |
| BLK-03：blueprint 缺失对象职责尚未统一 | T008-T011 | 先通过评审固定“新增 / 替代 / 延后”三分法 |
| BLK-04：资源预算缺少目标平台基线数据 | T012-T014 | 先从 `desktop_full`/`edge_balanced` 提供首版对象预算阈值，再迭代到 ARM 实测值 |
| BLK-05：boundary 目录现有 include 路径在测试或未来消费方中被硬编码 | T016-T019 | 采用“新增索引/别名 -> 保留兼容转发头 -> 最后迁移”三阶段策略，避免一次性破坏 include 路径 |

---

## 8. 维护要求

1. 每完成一个任务，回写状态、日期、产出链接、验证结果。
2. 每新增对象，必须同步回写差异矩阵。
3. 每扩展 gate，必须提供至少一个负向验证命令和预期失败结果。
4. 若新增任务仍过大，必须继续向下拆成子任务，不得在评审现场口头补充。
