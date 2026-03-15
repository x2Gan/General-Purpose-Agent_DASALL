# WP-02 横切基础对象 Build 开发 TODO

最近更新时间：2026-03-15  
阶段：contracts build 落地（WP-02）  
工作模式：Build-First with Design Constraints

## 1. 文档头

本文档将 WP-02 已冻结设计产物转换为可实现、可验证、可追溯的 Build 原子任务。  
范围严格限定为横切基础对象：error、budget、id/time、event 封套、枚举兼容与迁移守卫。  
不改写 ADR-006、ADR-007、ADR-008 结论，不提前进入 WP-03 主链路对象字段冻结。

## 2. 工作包目标（Build 视角）

1. 将 WP02-T004 至 WP02-T012 的冻结规则落盘为 contracts 头文件、校验器和兼容辅助层。
2. 将 M2 Gate 关键门禁（秒到毫秒迁移、枚举降级、EventEnvelope 头部白名单）落盘为 contract tests。
3. 建立可在本地与 CI 自动执行的 WP-02 契约门禁，阻断横切语义被子域重复定义。

## 3. Build 完成标准

1. 代码落盘：error、checkpoint、event、common metadata 四类横切对象头文件已存在且可编译。
2. 校验落盘：字段合法性、边界白名单、兼容迁移规则可由程序判定，不依赖人工主观解释。
3. 测试落盘：contract tests 覆盖 ResultCode 分类、ErrorInfo 必填、BudgetSnapshot 口径、ID/Time 规则、EventEnvelope 头部、枚举降级。
4. 门禁可执行：构建与 ctest 命令可直接执行，失败时能定位到具体规则违规。

## 4. Design->Build 映射表

| Design 任务/交付物 | 冻结结论 | Build 承接任务 |
|---|---|---|
| WP02-T001 横切范围表 | error/event/budget/id/time 五类横切语义收口 | WP02-B001、WP02-B008 |
| WP02-T002 兼容性规则 v1 | 新增优于修改、废弃优于删除、语义重解释视为 breaking | WP02-B009、WP02-B014 |
| WP02-T003 字段演进规则表 | 类型/可选/多值/枚举默认值演进门禁 | WP02-B002、WP02-B013 |
| WP02-T004 ResultCode 分类表 | validation/policy/tool/provider/runtime 五类失败域 | WP02-B003 |
| WP02-T005 ErrorInfo 字段清单 | failure_type/retryable/safe_to_replan/details/source_ref 必填 | WP02-B004 |
| WP02-T006 ErrorSource 引用约定 | source_ref 统一引用 observation/tool_call/worker_task/checkpoint | WP02-B005 |
| WP02-T007 RuntimeBudget 字段清单 | max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count | WP02-B006 |
| WP02-T008 BudgetSnapshot 规则 | current/max/remaining/reject_reason 统一口径 | WP02-B007 |
| WP02-T009 标识元数据规范 | request/session/trace/task/lease 与 parent_task_id 传播关系 | WP02-B008 |
| WP02-T010 TimeDeadline 规范 | created_at/deadline_at/timeout_ms/ttl 区分与优先级 | WP02-B009 |
| WP02-T011 EventEnvelope 头部定义 | 头部仅通用字段，模块私有字段仅在 payload | WP02-B010 |
| WP02-T012 枚举规范 | 枚举保留 Unspecified，unknown 降级，deprecate 生命周期 | WP02-B011 |
| WP02-T013 ReviewChecklist v1 | 可执行评审清单与 gate 条目 | WP02-B013、WP02-B014 |
| WP02-T014 评审纪要 | D1-D8 决议与 L1/L2 遗留项闭环要求 | WP02-B009、WP02-B011、WP02-B014 |
| WP02-T015 M2 冻结包 + B1/B2 | M2 Gate 生效，timeout 迁移与枚举降级证据链要求 | WP02-B009、WP02-B011、WP02-B014 |

## 5. 原子任务清单（核心）

| ID | 状态 | 任务（动词开头） | 输入依据（必须可追溯） | 代码改动范围（目录/文件/接口） | 测试改动范围（unit/contract/用例名） | 验收命令（可直接执行） | 交付物（代码 + 文档证据） | 完成判定（必须可二值判定） |
|---|---|---|---|---|---|---|---|---|
| WP02-B001 | Not Started | 新增横切基础对象总入口头文件 | WP02-T001、计划文档阶段 1、架构文档 7.2 | contracts/include/dasall/contracts/CrossCuttingContracts.h（新增） | tests/contract/smoke/CrossCuttingContractsSmokeTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CrossCuttingContractsSmokeTest --output-on-failure | 代码：CrossCuttingContracts.h；测试：CrossCuttingContractsSmokeTest.cpp；证据：头文件可统一 include 五类对象 | 仅当头文件聚合 error/event/checkpoint/id-time/enum 入口且测试通过时为 Done |
| WP02-B002 | Not Started | 新增字段演进兼容判定辅助器 | WP02-T002、WP02-T003、WP02-T013 | contracts/include/dasall/contracts/FieldEvolutionGuards.h（新增） | tests/contract/smoke/FieldEvolutionGuardsContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R FieldEvolutionGuardsContractTest --output-on-failure | 代码：FieldEvolutionGuards.h；测试：字段类型/可选/多值判定用例 | 仅当 non-breaking、review-required、breaking 判定可程序化复现且断言全通过时为 Done |
| WP02-B003 | Not Started | 新增 ResultCode 分类与判定枚举 | WP02-T004、架构文档 Tool 与 Runtime 边界、WP02-T014 D1/D2 | contracts/include/error/ResultCode.h（新增） | tests/contract/error/ResultCodeContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ResultCodeContractTest --output-on-failure | 代码：ResultCode.h；测试：五大分类判定与边界负例 | 仅当 validation/policy/tool/provider/runtime 五类均可稳定判定并通过测试时为 Done |
| WP02-B004 | Not Started | 新增 ErrorInfo 与最小校验器 | WP02-T005、ADR-007、WP02-T013 C2/C4 | contracts/include/error/ErrorInfo.h（新增），contracts/include/error/ErrorInfoGuards.h（新增） | tests/contract/error/ErrorInfoContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorInfoContractTest --output-on-failure | 代码：ErrorInfo.h 与校验器；测试：五个必填字段与语义边界断言 | 仅当 failure_type/retryable/safe_to_replan/details/source_ref 缺一即失败且合法样例通过时为 Done |
| WP02-B005 | Not Started | 新增 ErrorSource 结构与引用校验器 | WP02-T006、ADR-007、WP02-T013 C3 | contracts/include/error/ErrorSourceRef.h（新增），contracts/include/error/ErrorSourceGuards.h（新增） | tests/contract/error/ErrorSourceContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorSourceContractTest --output-on-failure | 代码：ErrorSourceRef.h 与 guards；测试：primary 唯一、四类 ref_type、ref_id 非空 | 仅当四类引用 observation/tool_call/worker_task/checkpoint 全覆盖且非法输入被拦截时为 Done |
| WP02-B006 | Not Started | 新增 RuntimeBudget 契约对象与阈值校验器 | WP02-T007、架构文档超限防护项、WP02-T013 D1 | contracts/include/checkpoint/RuntimeBudget.h（新增），contracts/include/checkpoint/RuntimeBudgetGuards.h（新增） | tests/contract/checkpoint/RuntimeBudgetContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RuntimeBudgetContractTest --output-on-failure | 代码：RuntimeBudget.h 与 guards；测试：五维预算字段必填与单位口径 | 仅当 max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count 全部可校验且测试通过时为 Done |
| WP02-B007 | Not Started | 新增 BudgetSnapshot 契约对象与一致性校验器 | WP02-T008、WP02-T014 D4、WP02-T013 D2-D4 | contracts/include/checkpoint/BudgetSnapshot.h（新增），contracts/include/checkpoint/BudgetSnapshotGuards.h（新增） | tests/contract/checkpoint/BudgetSnapshotContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BudgetSnapshotContractTest --output-on-failure | 代码：BudgetSnapshot.h 与 guards；测试：remaining=max-current、reject_reason 触发条件 | 仅当 remaining 不一致和 reject_reason 误填均被拦截且合法快照通过时为 Done |
| WP02-B008 | Not Started | 新增统一标识元数据对象与传播校验器 | WP02-T009、ADR-008、WP02-T014 D5 | contracts/include/dasall/contracts/IdentityMetadata.h（新增） | tests/contract/smoke/IdentityMetadataContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R IdentityMetadataContractTest --output-on-failure | 代码：IdentityMetadata.h；测试：request/session/trace/task/lease 与 parent_task_id 关系 | 仅当五类 ID 与 parent_task_id 关系规则均可校验且测试通过时为 Done |
| WP02-B009 | In Progress | 收敛时间语义迁移与 TimeDeadline 校验器 | WP02-T010、WP02-T015-B1、WP02-T014 D6、现有 CompatibilityGuards.h | contracts/include/dasall/contracts/CompatibilityGuards.h（扩展），contracts/include/dasall/contracts/TimeDeadlineGuards.h（新增） | tests/contract/smoke/CompatibilityContractTest.cpp（扩展），tests/contract/smoke/TimeDeadlineContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|TimeDeadlineContractTest" --output-on-failure | 代码：timeout_seconds 到 timeout_ms 兼容与 deadline 优先守卫；测试：冲突输入与换算路径断言 | 仅当 timeout_seconds 仅兼容读取、双字段冲突可失败、deadline_at 优先规则可自动验证时为 Done |
| WP02-B010 | Not Started | 新增 EventEnvelope 头部对象与白名单校验器 | WP02-T011、WP02-T014 D7、WP02-T015 M2 阻断条件 | contracts/include/event/EventEnvelope.h（新增），contracts/include/event/EventEnvelopeGuards.h（新增） | tests/contract/event/EventEnvelopeContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventEnvelopeContractTest --output-on-failure | 代码：EventEnvelope.h 与头部白名单 guard；测试：模块私有字段上浮头部负例 | 仅当头部仅允许通用字段且 payload 分层规则测试全通过时为 Done |
| WP02-B011 | In Progress | 补齐枚举降级与弃用生命周期守卫 | WP02-T012、WP02-T015-B2、WP02-T014 D8、现有 CompatibilityGuards.h | contracts/include/dasall/contracts/CompatibilityGuards.h（扩展），contracts/include/dasall/contracts/EnumLifecycleGuards.h（新增） | tests/contract/smoke/CompatibilityContractTest.cpp（扩展），tests/contract/smoke/EnumLifecycleContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|EnumLifecycleContractTest" --output-on-failure | 代码：unknown 到 Unspecified 降级守卫与 deprecate 校验；测试：已知值保留、未知值降级、删除 Unspecified 阻断 | 仅当 unknown->Unspecified 稳定可测且 Unspecified 删除动作被门禁拦截时为 Done |
| WP02-B012 | Not Started | 收敛 contract 测试编排并接入 CMake | tests/contract/CMakeLists.txt、WP02-T013、WP02-T015 | tests/contract/CMakeLists.txt（更新测试目标） | 合同测试分组：error/checkpoint/event/smoke 四组 | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure | 代码：CMake 测试目标编排；测试：ctest 可发现新增用例 | 仅当新增测试被 ctest 发现且 label=contract 正确设置时为 Done |
| WP02-B013 | Not Started | 新增 M2 Checklist 自动校验入口 | WP02-T013、WP02-T014、WP02-T015 | contracts/include/dasall/contracts/M2ChecklistGuards.h（新增） | tests/contract/smoke/M2ChecklistContractTest.cpp（新增） | cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R M2ChecklistContractTest --output-on-failure | 代码：M2 Checklist 对应的程序化断言入口；测试：A-F 六组门禁最小样例 | 仅当 Checklist 核心条目可程序化判定并测试通过时为 Done |
| WP02-B014 | Blocked | 新增 WP-02 CI 门禁脚本并接入流水线 | WP02-T015 M2 Gate、WP02-T013、工作日志记录 #007（CMake 配置阻塞） | scripts/ci/wp02_contract_gate.sh（新增），tests/CMakeLists.txt（可选挂接 gate target） | 复用 WP02 全部 contract tests | bash scripts/ci/wp02_contract_gate.sh | 代码：CI gate 脚本；证据：脚本输出包含 configure/build/ctest 摘要与失败明细 | 仅当脚本在可配置环境返回 0 且任一门禁失败时返回非 0 时为 Done；若 CMake 仍不可配置保持 Blocked |

### 5.1 任务依赖

1. WP02-B001 是入口依赖，WP02-B003 至 WP02-B011 依赖其统一 include 结构。
2. WP02-B004 依赖 WP02-B003 的 ResultCode 分类枚举。
3. WP02-B005 依赖 WP02-B004 的 ErrorInfo.source_ref 字段定义。
4. WP02-B007 依赖 WP02-B006 的 RuntimeBudget 维度定义。
5. WP02-B012 依赖 WP02-B003 至 WP02-B011 的测试文件落盘。
6. WP02-B013 依赖 WP02-B012 完成测试编排。
7. WP02-B014 依赖 CMake 可配置与 WP02-B012 可执行。

### 5.2 每任务风险与回退（摘要）

1. WP02-B003 风险：tool/provider 边界混淆。回退：按 WP02-T014 D1 固定判定规则并补负例。
2. WP02-B004 风险：retryable 被误当已执行。回退：在 guard 中仅表达候选语义。
3. WP02-B007 风险：remaining 手工赋值漂移。回退：强制使用 max-current 计算。
4. WP02-B009 风险：deadline 与 timeout 双写冲突。回退：执行判定固定 deadline_at 优先。
5. WP02-B010 风险：模块私有字段上浮头部。回退：头部白名单拒绝并迁回 payload。
6. WP02-B011 风险：未知枚举未降级。回退：统一走 CompatibilityGuards 中央降级路径。
7. WP02-B014 风险：CI 与本地命令不一致。回退：脚本仅封装文档定义命令并输出完整日志。

## 6. 执行顺序建议

1. 先做基础对象链：WP02-B001 -> WP02-B002 -> WP02-B003 -> WP02-B004 -> WP02-B005。
2. 再做预算与标识时间链：WP02-B006 -> WP02-B007 -> WP02-B008 -> WP02-B009。
3. 然后做事件与枚举链：WP02-B010 -> WP02-B011。
4. 最后做门禁链：WP02-B012 -> WP02-B013 -> WP02-B014。
5. 若 WP02-B014 受 CMake 阻塞，可先完成 B001-B013 的代码与测试落盘，再集中解阻。

## 7. 阻塞项与解阻条件

1. BLK-01：CMake 配置失败导致 contract tests 无法执行。  
解阻条件：cmake -S . -B build-ci -G Ninja 成功，且能执行 ctest -L contract。
2. BLK-02：当前 WP-02 目录下缺少已落盘 contracts 头文件，新增任务初期可能只完成部分模块。  
解阻条件：至少完成 WP02-B001、B003、B004、B006、B010 五个对象头文件后再做全量 gate。
3. BLK-03：若下游对象尝试在 WP-02 阶段引入新的横切一级语义类别。  
解阻条件：按 WP02-T001/T015 退回评审并在 T013/T014 形成决议后再推进。

## 8. 验收与门禁（CI/本地）

本地建议执行序列：

1. cmake -S . -B build-ci -G Ninja
2. cmake --build build-ci --target dasall_contract_tests
3. ctest --test-dir build-ci -L contract --output-on-failure
4. ctest --test-dir build-ci -R ResultCodeContractTest --output-on-failure
5. ctest --test-dir build-ci -R ErrorInfoContractTest --output-on-failure
6. ctest --test-dir build-ci -R RuntimeBudgetContractTest --output-on-failure
7. ctest --test-dir build-ci -R EventEnvelopeContractTest --output-on-failure
8. ctest --test-dir build-ci -R EnumLifecycleContractTest --output-on-failure

CI 门禁建议：

1. 强制执行 wp02_contract_gate.sh。
2. 命中以下任一规则直接失败：
   - 缺少 ErrorInfo 必填字段。
   - RuntimeBudget 五维预算任一缺失。
   - BudgetSnapshot remaining 与 max-current 不一致。
   - EventEnvelope 头部出现模块私有字段。
   - 枚举移除 Unspecified 或未知值未降级。
   - timeout_seconds 新写入或 deadline_at 优先级被回退。

## 9. 风险与回退策略

1. 风险：为赶进度把设计规则直接硬编码到业务层，破坏 contracts 横切边界。  
回退：规则统一收敛到 contracts/include 下 guards，业务层只调用。
2. 风险：新增字段时未做兼容判定，形成隐式 breaking。  
回退：所有字段变更先过 FieldEvolutionGuards，再允许合并。
3. 风险：测试仅覆盖正例，无法阻断边界回退。  
回退：每条规则至少保留一个负例断言并接入 contract 标签。
4. 风险：门禁脚本与文档漂移。  
回退：每次门禁规则变更同步更新 WP-02 Build TODO 与脚本，缺一阻断。

## 10. Quality Gate（必须回答）

1. 是否每个任务都包含 代码 + 测试 + 验收命令 三件套？  
答：是。包括 Blocked 任务在内均给出代码目标、测试目标与验收命令。

2. 是否每个任务都能追溯到输入文档？  
答：是。每个任务输入依据均关联架构/计划/总 TODO/WP-02 TODO/交付物/工作日志。

3. 是否存在可能 breaking change 的任务？若有，是否标记评审门禁？  
答：有。WP02-B002、WP02-B003、WP02-B009、WP02-B011 可能引发兼容性变化，已通过 WP02-B013 与 WP02-B014 设置门禁。

4. 是否存在 Blocked 任务？若有，是否给出明确解阻条件？  
答：有。WP02-B014 为 Blocked，已给出可执行解阻条件。

5. 该 TODO 是否可直接进入开发执行，而不需要再次补充口头说明？  
答：是。任务已具备状态、顺序、依赖、回退、命令与二值完成判定，可直接执行。
