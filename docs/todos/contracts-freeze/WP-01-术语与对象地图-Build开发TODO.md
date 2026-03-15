# WP-01 术语与对象地图 Build 开发 TODO

最近更新时间：2026-03-15  
阶段：contracts build 落地（WP-01 约束内）  
工作模式：Build-First with Design Constraints

## 1. 文档头

本文档将 WP-01 已冻结设计产物转为可落盘、可测试、可追溯的 Build 原子任务。  
范围仅限 WP-01：术语收敛、对象地图、边界规则、ADR 对齐约束的代码化与测试化。  
不改写 ADR-006/007/008 结论，不提前展开 WP-02/03/04/05 的字段细化或子域对象冻结。

## 2. 工作包目标（Build 视角）

1. 把 WP-01 的 Stable/Blocked/Deferred 对象边界落成可编译的 contracts 边界元数据与守卫函数。
2. 把 ADR-006/007/008 的关键禁入规则落成可自动执行的 contract tests。
3. 形成 Design -> Code -> Test -> Gate 的闭环，保证后续工作包在 CI 中被前置门禁约束。

## 3. Build 完成标准

1. 代码：WP-01 对应边界元数据与守卫代码已落盘到 contracts 和 tests/contract。
2. 测试：新增/更新的 contract tests 覆盖 ContextPacket、恢复语义、协同语义三类边界。
3. 门禁：本地与 CI 命令可直接执行，能阻断违反 WP-01 冻结结论的改动。
4. 追溯：每个 Build 任务都能映射到具体设计任务（WP01-T00x）和输入文档段落。

## 4. Design->Build 映射表

| Design 任务/交付物 | 冻结结论 | Build 承接任务 |
|---|---|---|
| WP01-T006 稳定对象标注版流图 | 14 个 Stable 节点进入 contracts 主清单 | WP01-B001、WP01-B002 |
| WP01-T007 内部对象边界清单 | 13 个 Blocked + 2 个 Deferred 不得在本阶段外溢 | WP01-B003、WP01-B004 |
| WP01-T008 边界说明 v1 | Stable/Blocked/Deferred 三层边界模型 | WP01-B001、WP01-B003 |
| WP01-T009 ContextPacket 约束核对单 | 禁入 final_messages/provider_payload/rendered_prompt | WP01-B004、WP01-B007 |
| WP01-T010 恢复语义核对单 | ReflectionDecision 仅建议语义；RecoveryOutcome 仅执行结果语义 | WP01-B005、WP01-B008 |
| WP01-T011 协同语义核对单 | MultiAgentRequest/Result/WorkerTask 严格分层 | WP01-B006、WP01-B009 |
| WP01-T012 评审纪要 + T013 M1 冻结包 | M1 Gate 与阻断条件生效 | WP01-B010、WP01-B011 |

## 5. 原子任务清单（核心）

| ID | 状态 | 任务（动词开头） | 输入依据（必须可追溯） | 代码改动范围（目录/文件/接口） | 测试改动范围（unit/contract/用例名） | 验收命令（可直接执行） | 交付物（代码 + 文档证据） | 完成判定（必须可二值判定） |
|---|---|---|---|---|---|---|---|---|
| WP01-B001 | Not Started | 新增 WP01 对象边界名册与分类枚举 | 架构文档 3.8、计划文档第 7 节、WP01-T006/T007/T013 | contracts/include/dasall/contracts/ObjectBoundaryCatalog.h（新增） | tests/contract/smoke/ObjectBoundaryCatalogContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure | 代码：ObjectBoundaryCatalog.h；测试：ObjectBoundaryCatalogContractTest.cpp；证据：本任务 PR 说明引用 WP01-T006/T007/T013 | 仅当 14 个 Stable、13 个 Blocked、2 个 Deferred 全部可枚举且测试通过时为 Done，否则为 Not Done |
| WP01-B002 | Not Started | 补齐 Stable 对象的编译期标识与最小占位类型 | WP01-T003/T004/T006/T013，架构文档 7.4（contracts 独立依赖） | contracts/include/agent/、context/、observation/、checkpoint/、task/ 下新增 Opaque/Tag 头文件（仅命名与类型标识，不定字段） | tests/contract/smoke/StableTypePresenceContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R StableTypePresenceContractTest --output-on-failure | 代码：Stable 对象 tag 头文件集合；测试：StableTypePresenceContractTest.cpp；证据：对象名与 WP01-T006 一一对应表 | 仅当 14 个 Stable 名称均有可 include 的占位类型且无字段语义越界时为 Done |
| WP01-B003 | Not Started | 新增 Blocked/Deferred 外溢守卫接口 | WP01-T007/T008/T013，计划文档阶段 0/阶段 2 边界规则 | contracts/include/dasall/contracts/BoundaryGuards.h（新增）或扩展 CompatibilityGuards.h | tests/contract/smoke/BoundaryGuardsContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BoundaryGuardsContractTest --output-on-failure | 代码：边界守卫 API；测试：Blocked/Deferred 判定用例；证据：守卫 API 与 T007 清单映射 | 仅当 Blocked/Deferred 对象均被守卫拒绝进入 Stable 清单时为 Done |
| WP01-B004 | Not Started | 校验 ContextPacket 禁入字段守卫 | ADR-006、WP01-T009、WP01-T012 Gate 阻断条件 1 | contracts/include/dasall/contracts/ContextBoundaryGuards.h（新增） | tests/contract/smoke/ContextPacketBoundaryContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketBoundaryContractTest --output-on-failure | 代码：ContextPacket 字段名禁入检查；测试：final_messages/provider_payload/rendered_prompt 三个负例 + 合法正例 | 仅当三项禁入字段全部被测试阻断且合法字段不过度误杀时为 Done |
| WP01-B005 | Not Started | 校验恢复语义分层守卫 | ADR-007、WP01-T010、WP01-T012 Gate 阻断条件 2 | contracts/include/dasall/contracts/RecoveryBoundaryGuards.h（新增） | tests/contract/smoke/RecoveryBoundaryContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure | 代码：ReflectionDecision 与 RecoveryOutcome 字段职责守卫；测试：调度字段误入与归因字段误入负例 | 仅当 ReflectionDecision 禁止调度字段、RecoveryOutcome 禁止失败归因字段均被自动校验时为 Done |
| WP01-B006 | Not Started | 校验协同语义分层守卫 | ADR-008、WP01-T011、WP01-T012 Gate 阻断条件 3/4 | contracts/include/dasall/contracts/MultiAgentBoundaryGuards.h（新增） | tests/contract/smoke/MultiAgentBoundaryContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure | 代码：MultiAgentRequest/Result/WorkerTask 分层守卫；测试：复用 AgentRequest、结果替代 AgentResult、WorkerTask 承载全局态三类负例 | 仅当三类越权场景全部被测试阻断时为 Done |
| WP01-B007 | Not Started | 收敛 contracts 测试入口并接入 CMake | tests/CMakeLists.txt、tests/contract/CMakeLists.txt、WP01-T013 M1 Gate | tests/contract/CMakeLists.txt（新增测试目标和 add_test） | contract 标签下新增 4-6 个边界测试目标 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure | 代码：CMake 测试目标定义；测试：全部 WP01 新增 contract tests 已注册；证据：ctest -N 列表截图/日志 | 仅当新增测试全部被 ctest 发现且 label=contract 时为 Done |
| WP01-B008 | Not Started | 增加恢复语义回归组合测试 | WP01-T010、WP01-T012 中“建议权/执行权分层” | tests/contract/smoke/RecoveryBoundaryContractTest.cpp（扩展） | contract: ReflectionDecision/RecoveryOutcome 组合回归用例 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure | 代码：组合回归用例；证据：失败示例输入与期望阻断输出记录 | 仅当至少 1 组合法 + 3 组非法组合断言全部通过时为 Done |
| WP01-B009 | Not Started | 增加协同语义回归组合测试 | WP01-T011、WP01-T012 中“全局主控/协同子域分层” | tests/contract/smoke/MultiAgentBoundaryContractTest.cpp（扩展） | contract: MultiAgent 分层组合回归用例 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure | 代码：协同边界组合回归用例；证据：越权字段矩阵与断言结果 | 仅当 Request/Result/WorkerTask 三组对象的越权矩阵断言全通过时为 Done |
| WP01-B010 | Not Started | 固化 WP01 M1 本地与 CI 门禁脚本入口 | WP01-T013 第 6 节、WP01-T012 第 7 节 | scripts/ci/（新增 wp01_contract_gate.sh）与 tests/CMakeLists.txt（可选接入） | 使用现有 contract tests 作为 gate，不新增业务单测 | bash scripts/ci/wp01_contract_gate.sh | 代码：门禁脚本；证据：脚本输出包含 build + ctest 通过摘要 | 仅当脚本在干净环境返回 0 且任一边界回归失败时返回非 0 才为 Done |
| WP01-B011 | Blocked | 解阻 CMake 配置以恢复 contract tests 可执行性 | 工作日志记录 #007（CMake Tools 配置失败）、WP01-B001~B010 共同依赖 | CMakePresets.json（若缺失则新增）或 cmake/ 现有配置最小修复 | 不新增测试代码；恢复现有与新增 contract tests 的执行能力 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure | 代码：最小配置修复；证据：可复现的 configure/build/test 成功日志 | 仅当 configure/build/ctest 三步全部成功时解除 Blocked，否则保持 Blocked |
| WP01-B012 | Blocked | 冻结 Stable 对象字段级结构定义（跨 WP 依赖） | WP01-T013（仅对象级冻结）、计划文档阶段 1/2/3/4 字段冻结顺序 | 目标文件待 WP-02/03/04 提供字段规则后确定；当前不允许在 WP-01 内定义字段 | tests/contract 针对字段级 schema 的测试待后续 WP 提供 | 无（Blocked） | 证据：阻塞说明与依赖清单（WP-02/03/04 对应 deliverable） | 仅当 WP-02/03/04 对应字段规则冻结完成后，任务状态才能从 Blocked 变为 Not Started |

## 6. 执行顺序建议

1. 先做解阻链：WP01-B011（若未解阻，后续只能做静态代码编写，无法完成验收）。
2. 再做骨架链：WP01-B001 -> WP01-B002 -> WP01-B003。
3. 然后做 ADR 边界链：WP01-B004 -> WP01-B005 -> WP01-B006。
4. 最后做门禁链：WP01-B007 -> WP01-B008 -> WP01-B009 -> WP01-B010。
5. WP01-B012 保持 Blocked，直到 WP-02/03/04 字段规则冻结完成。

## 7. 阻塞项与解阻条件

1. BLK-01：CMake 配置失败导致 contract tests 不可执行。  
解阻条件：完成 WP01-B011，`cmake configure + build + ctest` 三连通过。
2. BLK-02：WP-01 仅冻结对象级边界，未冻结字段级 schema。  
解阻条件：WP-02/03/04 对应字段规范交付并评审通过后，方可执行 WP01-B012。
3. BLK-03：当前 tests/contract 仅 smoke 基线，边界回归覆盖不足。  
解阻条件：WP01-B004/B005/B006/B008/B009 全部通过并接入 WP01-B007。

## 8. 验收与门禁（CI/本地）

本地建议命令序列：

1. `cmake -S . -B build-ci -G Ninja`
2. `cmake --build build-ci --target dasall_contract_tests`
3. `ctest --test-dir build-ci -L contract --output-on-failure`
4. `ctest --test-dir build-ci -R ContextPacketBoundaryContractTest --output-on-failure`
5. `ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure`
6. `ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure`

CI 门禁建议：

1. PR 必跑 `dasall_contract_tests`。
2. 命中以下任一特征直接失败：
	- ContextPacket 出现 final_messages/provider_payload/rendered_prompt。
	- ReflectionDecision 承载调度字段。
	- RecoveryOutcome 承载失败归因语义。
	- MultiAgentRequest 复用 AgentRequest 或 MultiAgentResult 承担 AgentResult 语义。
	- WorkerTask 承载全局 Session/FSM 主态语义。

## 9. 风险与回退策略

1. 风险：为追求“可运行”而提前定义字段细节，跨入 WP-02/03/04 范围。  
回退：仅保留对象标识与边界守卫，撤回字段级定义到后续 WP。
2. 风险：守卫规则过严导致误杀合法输入。  
回退：在 WP01-B008/B009 增加合法正例，调整为最小必要约束而非全字段强耦合。
3. 风险：门禁脚本与 CMake 目标脱节，出现“脚本通过但测试未注册”。  
回退：在 WP01-B007 增加 `ctest -N` 校验步骤并强制比对测试清单。
4. 风险：历史学习文档旧口径反向污染代码。  
回退：所有边界裁定只接受 ADR + WP01-T009/T010/T011 + WP01-T013 作为权威来源。

## 10. Quality Gate（必须回答）

1. 是否每个任务都包含“代码 + 测试 + 验收命令”三件套？  
答：是。对 Blocked 任务（WP01-B012）显式标注“当前无可执行验收命令”的原因与解阻条件。

2. 是否每个任务都能追溯到输入文档？  
答：是。每个任务“输入依据”均绑定架构/计划/WP01 交付物/工作日志。

3. 是否存在可能 breaking change 的任务？若有，是否标记评审门禁？  
答：有。WP01-B002（对象命名占位类型）、WP01-B003~B006（守卫语义）可能影响后续实现；已通过 WP01-B010 和第 8 节门禁做强制评审阻断。

4. 是否存在 Blocked 任务？若有，是否给出明确解阻条件？  
答：有。WP01-B011、WP01-B012 为 Blocked，已在第 7 节给出可执行解阻条件。

5. 该 TODO 是否可直接进入开发执行，而不需要再次补充口头说明？  
答：是。任务已具备顺序、依赖、代码范围、测试范围、命令、完成判定和回退策略，可直接执行。

