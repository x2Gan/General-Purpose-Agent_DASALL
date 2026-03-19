# DASALL 开发执行记录

## 使用说明

- 目的：用于在每次会话开始时快速回溯中断点，并继续推进实施计划。
- 追加规则：新记录追加在文件顶部（最新优先）。
- 记录最小字段：日期、阶段/任务、完成内容、关键产物、验证结果、下一步、风险/注意事项。

---

## 记录 #037

- 日期：2026-03-19
- 阶段：contracts 冻结（WP-05 双轨执行）
- 任务：WP05-T001 子域推进顺序与执行顺序守卫
- 状态：已完成

### 改动

1. 完成 WP05-T001-D 交付：
   - 新增 design 文档：
     - [docs/todos/contracts-freeze/deliverables/WP05-T001-子域推进顺序表.md](docs/todos/contracts-freeze/deliverables/WP05-T001-子域推进顺序表.md)
   - 固化四波 rollout：Wave1 `tool`；Wave2 `prompt + memory`；Wave3 `task + event`；Wave4 `llm`。
   - 明确允许并行、禁止并行、越权禁区和 Design->Build 映射。
2. 完成 WP05-T001-B 代码落地：
   - 新增 header-only 守卫：
     - [contracts/include/boundary/DomainRolloutGuards.h](contracts/include/boundary/DomainRolloutGuards.h)
   - 提供 `DomainSubdomain`、`DomainRolloutWave`、`DomainRolloutDecision`、`DomainRolloutSnapshot`、`evaluate_domain_rollout_start()` 和完成计数 helper。
3. 新增 smoke contract test 并接入：
   - [tests/contract/smoke/DomainRolloutContractTest.cpp](tests/contract/smoke/DomainRolloutContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `DomainRolloutContractTest`。
4. 回写任务状态：
   - [docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md](docs/todos/contracts-freeze/WP-05-子域细化与ContractTestsTODO.md) 将 WP05-T001-D/B 更新为 Done，并补充验收证据。

### 测试

1. 聚合验收：
   - `cmake --build build-ci --target dasall_contract_tests`
   - 结果：通过；CMake 自动重生成后，61/61 contract tests passed，新增 `DomainRolloutContractTest` 被纳入 `contract;smoke` 标签。
2. 指定测试验收：
   - `ctest --test-dir build-ci -R DomainRolloutContractTest --output-on-failure`
   - 结果：通过；1/1 test passed。
3. 负例覆盖由新增测试内联验证：
   - `prompt` 在 `tool` 未完成时被阻断。
   - `prompt` 在 `task` 已启动的跨波次场景下被阻断。
   - `llm` 在 `event` 未完成时被阻断。
   - 已完成子域重复启动被阻断。

### 结果

1. WP05-T001-D/B 已完成，后续 T002-T010 可基于统一 rollout guard 继续推进。
2. WP05 当前推荐顺序已从“文档建议”收敛为可执行的 compile-time/contracts 守卫。

### 下一步

1. 按顺序推进 WP05-T002-D/B（ToolRequest 职责边界与契约对象）。

### 风险

1. 当前 rollout wave 属于 WP05 的首版节奏守卫；若后续评审决定扩大或收缩并行窗口，需要同步修订设计文档和 `DomainRolloutGuards.h`，避免文档与守卫漂移。
2. CMake Tools 在当前 VS Code 环境仍无法成功配置项目，构建验收暂时依赖仓库既有 `build-ci` 目录上的命令链路。

## 记录 #036

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：针对评审问题组织修复与完善（代码 + 测试 + 文档收敛）
- 状态：已完成

### 改动

1. 修复 Critical（头文件 helper 重定义）：
   - 新增公共 helper 头：[contracts/include/boundary/GuardCommon.h](contracts/include/boundary/GuardCommon.h)
   - 去重并改为复用：
     - [contracts/include/boundary/IdentityMetadata.h](contracts/include/boundary/IdentityMetadata.h)
     - [contracts/include/event/EventEnvelopeGuards.h](contracts/include/event/EventEnvelopeGuards.h)
     - [contracts/include/error/ErrorInfoGuards.h](contracts/include/error/ErrorInfoGuards.h)
     - [contracts/include/error/ErrorSourceGuards.h](contracts/include/error/ErrorSourceGuards.h)
2. 修复 Major（timeout 迁移溢出）：
   - [contracts/include/boundary/CompatibilityGuards.h](contracts/include/boundary/CompatibilityGuards.h)
   - 新增 `timeout_seconds -> timeout_ms` 上界校验，溢出时失败返回。
3. 修复 Major（BudgetSnapshot 大数转换风险）：
   - [contracts/include/checkpoint/BudgetSnapshotGuards.h](contracts/include/checkpoint/BudgetSnapshotGuards.h)
   - 改为安全 remaining 计算路径，超可表示范围时返回 `remaining computation overflow`。
4. 补充测试：
   - [tests/contract/smoke/CompatibilityContractTest.cpp](tests/contract/smoke/CompatibilityContractTest.cpp)
     - 新增 `test_timeout_seconds_overflow_is_rejected`。
   - [tests/contract/checkpoint/BudgetSnapshotContractTest.cpp](tests/contract/checkpoint/BudgetSnapshotContractTest.cpp)
     - 新增 `test_remaining_computation_overflow_is_rejected`。
5. 文档完善收敛：
   - [docs/todos/contracts-freeze/deliverables/WP02-T013-ReviewChecklist-v1.md](docs/todos/contracts-freeze/deliverables/WP02-T013-ReviewChecklist-v1.md) 状态更新为 Done。
   - [docs/todos/contracts-freeze/deliverables/WP02-T014-评审纪要.md](docs/todos/contracts-freeze/deliverables/WP02-T014-评审纪要.md) 评审范围扩展到 T001-T013 并补 D0 决议。
   - [docs/todos/contracts-freeze/WP-02-横切基础对象TODO.md](docs/todos/contracts-freeze/WP-02-横切基础对象TODO.md) 状态统一收敛为 Done。
   - [docs/todos/contracts-freeze/deliverables/WP02-T015-M2冻结包.md](docs/todos/contracts-freeze/deliverables/WP02-T015-M2冻结包.md) 冻结资产清单补全至 T015 自包含。
   - [docs/todos/contracts-freeze/deliverables/WP02-评审覆盖矩阵与代码审计报告-2026-03-16.md](docs/todos/contracts-freeze/deliverables/WP02-评审覆盖矩阵与代码审计报告-2026-03-16.md) 追加修复执行记录与修复后结论。

### 测试

1. 组合 include 编译复验：
   - `c++ -std=c++17 -Icontracts/include -c /tmp/dup_check.cpp -o /tmp/dup_check.o`
   - 结果：通过（无重定义错误）。
2. 门禁复验：
   - `bash scripts/ci/wp02_contract_gate.sh`
   - 结果：返回 0；contract tests 20/20 通过；关键门禁测试 5/5 通过。

### 结果

1. 评审报告中的 1 个 Critical + 2 个 Major 代码问题已修复并通过验收。
2. WP-02 相关评审/冻结文档状态完成一轮一致性收敛。
3. 审计结论从 `Changes Requested` 收敛为“可合并（在保持现有 gate 前提下）”。

### 下一步

1. 若继续推进，建议执行一次提交前整体验证（含 gate + 关键单测）并按“代码修复/文档收敛”拆分提交。

### 风险

1. 当前工作区仍有较多未提交历史改动；提交前需按变更意图分组，避免把不相关改动混入同一提交。

## 记录 #035

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：评审遗留项 L1/L2 闭环复验与文档一致性修复
- 状态：已完成

### 改动

1. 闭环复核评审遗留项：
   - L1（`timeout_seconds` -> `timeout_ms` 迁移一致性）对应实现与测试已在 `CompatibilityGuards` / `TimeDeadlineGuards` 落盘。
   - L2（unknown 枚举值降级证据）对应实现与测试已在 `EnumLifecycleGuards` 落盘。
2. 修正文档状态一致性：
   - `WP-02-横切基础对象-Build开发TODO.md` 的 Quality Gate 从“B014 Blocked”修正为“无 Blocked”。
   - `WP02-T014-评审纪要.md` 从 In Review 更新为 Done，并将 L1/L2 标注为 Closed。

### 测试

1. 执行门禁命令：
   - `bash scripts/ci/wp02_contract_gate.sh`
2. 结果：
   - 返回 0。
   - 关键门禁测试 5/5 通过：CompatibilityContractTest、TimeDeadlineContractTest、EventEnvelopeContractTest、EnumLifecycleContractTest、M2ChecklistContractTest。
   - 全量 contract 标签测试 20/20 通过。

### 结果

1. 评审遗留项 L1/L2 已形成“实现 + 测试 + gate”闭环证据。
2. WP-02 评审与 Build 文档状态一致，可作为后续冻结发布输入。

### 下一步

1. 进入 T015 发布准备时，复用本记录与 T014 纪要作为审计证据。

### 风险

1. 当前环境下 CMake Tools 扩展未能完成项目配置，暂以脚本门禁结果作为执行证据；后续建议补充一次 CMake Tools 侧复验。

## 记录 #034

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B014 新增 WP-02 CI 门禁脚本并接入流水线
- 状态：已完成

### 改动

1. 新增 WP-02 gate 脚本：
   - [scripts/ci/wp02_contract_gate.sh](scripts/ci/wp02_contract_gate.sh)
   - 脚本流程：configure -> build `dasall_contract_tests` -> 注册校验(`ctest -N -L contract`) -> 执行关键 WP02 测试 -> 执行全量 contract 标签测试。
2. 新增可配置 required tests 列表：
   - 默认门禁测试：CompatibilityContractTest、TimeDeadlineContractTest、EventEnvelopeContractTest、EnumLifecycleContractTest、M2ChecklistContractTest。
   - 支持 `WP02_GATE_REQUIRED_TESTS` 覆盖，便于 CI 场景注入与诊断。
3. 门禁失败语义落盘：
   - 注册缺失时脚本非 0 退出并打印缺失测试名。

### 测试

1. 执行验收命令（B014 原样）：
   - `bash scripts/ci/wp02_contract_gate.sh`
2. 结果：
   - 返回 0。
   - 输出包含 configure/build/registration/ctest 摘要。
   - 全量 contract 标签测试 20/20 通过。
3. 负例校验：
   - `WP02_GATE_REQUIRED_TESTS=DefinitelyMissingContractTest bash scripts/ci/wp02_contract_gate.sh`
   - 返回 `NEGATIVE_RC=1`，并输出缺失注册测试名，符合“门禁失败非 0”要求。

### 结果

1. WP02-B014 达成 Done 判定：脚本在可配置环境返回 0，且门禁失败场景稳定返回非 0。

### 下一步

1. WP-02 核心原子任务 B001-B014 已完成，下一步建议转入收尾复核（同步 CI 流水线调用并执行一次端到端 dry-run）。

### 风险

1. 当前脚本默认 generator 为 Ninja；若 CI 机型无 Ninja，需要在流水线设置 `CMAKE_GENERATOR`。
2. 脚本复用了 contract 标签全集执行，后续若测试规模显著增长，可考虑拆分为“关键门禁 + 全量夜跑”两级策略。

## 记录 #033

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B013 新增 M2 Checklist 自动校验入口
- 状态：已完成

### 改动

1. 新增 M2 Checklist 守卫头文件：
   - [contracts/include/boundary/M2ChecklistGuards.h](contracts/include/boundary/M2ChecklistGuards.h)
   - 定义 `M2ChecklistInputs`、`M2ChecklistResult`，并提供 `validate_m2_checklist(...)`。
2. 新增 A-F 六组门禁程序化判定：
   - 约束为“六组全部通过才通过”，并输出 `first_failed_gate` 便于定位。
3. 新增合同测试并接入 smoke 组：
   - [tests/contract/smoke/M2ChecklistContractTest.cpp](tests/contract/smoke/M2ChecklistContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `M2ChecklistContractTest`。

### 测试

1. 执行验收命令（B013 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R M2ChecklistContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 20/20 通过（含新增测试）。
   - `M2ChecklistContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：A-F 六组全部通过时 checklist 通过。
   - 负例：C 组失败时 checklist 阻断，且返回 first_failed_gate=C。

### 结果

1. WP02-B013 达成 Done 判定：Checklist 核心条目可程序化判定并通过测试。

### 下一步

1. 按顺序推进 WP02-B014（WP-02 CI 门禁脚本接入）。

### 风险

1. 当前 A-F 由布尔输入表示，若后续要承载更细粒度失败原因，需要在不破坏现有 API 的前提下扩展结果结构。
2. 目前 checklist 只做“聚合判定”，不替代各单项守卫；后续若单项守卫语义变化，需要同步维护 checklist 输入映射。

## 记录 #032

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B012 收敛 contract 测试编排并接入 CMake
- 状态：已完成

### 改动

1. 更新 contract 测试统一注册入口：
   - `tests/contract/CMakeLists.txt`
   - 将 `dasall_register_contract_test(...)` 扩展为四参数形式（可接收 group_label）。
2. 收敛四组 contract 测试编排：
   - 显式按 smoke/error/checkpoint/event 四组注册测试。
   - 每个测试统一打上 `contract` 与组标签（如 `contract;smoke`）。
3. 保持既有 contract tests 目标不变，仅增强可发现性与分组可观测性。

### 测试

1. 执行验收命令（B012 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 19/19 通过。
   - label 汇总显示：smoke=13、error=3、checkpoint=2、event=1。
3. 负例发现校验：
   - `ctest --test-dir build-ci -N -R DefinitelyMissingContractTest`
   - 输出 `Total Tests: 0`，验证未注册测试不会被误发现。

### 结果

1. WP02-B012 达成 Done 判定：新增/既有测试均可被 ctest 发现，且 label=contract 与四组分层正确生效。

### 下一步

1. 按顺序推进 WP02-B013（新增 M2 Checklist 自动校验入口）。

### 风险

1. 当前分组标签由 CMake 注册参数维护，后续新增测试若遗漏组标签，会影响分组统计但不影响 contract 主标签执行。
2. 若未来希望按组单独门禁（例如 `ctest -L event`），需在 CI 脚本中同步加入分组命令，避免本地与 CI 行为漂移。

## 记录 #031

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B011 补齐枚举降级与弃用生命周期守卫
- 状态：已完成

### 改动

1. 扩展枚举兼容辅助：
   - `contracts/include/boundary/CompatibilityGuards.h`
   - 新增 `has_unspecified_enum_sentinel(...)`，用于检测未知值降级路径是否具备 Unspecified 哨兵。
2. 新增枚举生命周期守卫：
   - `contracts/include/boundary/EnumLifecycleGuards.h`
   - 提供 `validate_enum_lifecycle_descriptor(...)` 与 `normalize_enum_with_lifecycle(...)`，实现：
     - 已知值保留；
     - 未知值降级到 Unspecified；
     - 删除 Unspecified 哨兵直接阻断；
     - deprecated 值必须属于 known_values。
3. 扩展/新增合同测试并接入：
   - `tests/contract/smoke/CompatibilityContractTest.cpp`（扩展）：新增 “缺失 Unspecified 哨兵可检测” 负例。
   - `tests/contract/smoke/EnumLifecycleContractTest.cpp`（新增）：
     - 正例：已知值保留；
     - 正例：未知值降级到 Unspecified；
     - 负例：删除 Unspecified 哨兵阻断。
   - `tests/contract/CMakeLists.txt` 注册 `EnumLifecycleContractTest`。

### 测试

1. 执行验收命令（B011 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|EnumLifecycleContractTest" --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 19/19 通过（含新增测试）。
   - `CompatibilityContractTest` 与 `EnumLifecycleContractTest` 2/2 通过。
3. 覆盖摘要：
   - 已知值保留。
   - 未知值降级到 Unspecified。
   - 删除 Unspecified 哨兵被门禁阻断。

### 结果

1. WP02-B011 达成 Done 判定：unknown->Unspecified 稳定可测，且 Unspecified 删除动作被拦截。

### 下一步

1. 按顺序推进 WP02-B012（收敛 contract 测试编排并接入 CMake）。

### 风险

1. 当前生命周期描述符基于整数枚举值集合，若后续引入字符串枚举编码，需要新增编码层映射而非改写现有守卫语义。
2. deprecated 值当前保留可读路径并通过标志位暴露，若后续需要“强阻断 deprecated 输入”，应通过新门禁开关实现，避免改变已落地兼容行为。

## 记录 #030

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B010 新增 EventEnvelope 头部对象与白名单校验器
- 状态：已完成

### 改动

1. 新增 EventEnvelope 契约对象：
   - [contracts/include/event/EventEnvelope.h](contracts/include/event/EventEnvelope.h)
   - 定义 `EventEnvelopeHeader` 与 `EventEnvelope`，头部仅承载公共元数据，模块私有信息保留在 payload。
2. 新增 EventEnvelope 白名单守卫：
   - [contracts/include/event/EventEnvelopeGuards.h](contracts/include/event/EventEnvelopeGuards.h)
   - 提供 `validate_event_envelope(...)`，校验：
     - 公共头字段必填（event_id/event_type/event_version/occurred_at_ms/request_id/trace_id）；
     - payload 载体必填（payload_type/payload_json）；
     - 头部键必须在白名单中，阻断模块私有字段上浮头部。
3. 新增 event 合同测试并接入：
   - [tests/contract/event/EventEnvelopeContractTest.cpp](tests/contract/event/EventEnvelopeContractTest.cpp)
   - [tests/contract/CMakeLists.txt](tests/contract/CMakeLists.txt) 注册 `EventEnvelopeContractTest`。

### 测试

1. 执行验收命令（B010 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R EventEnvelopeContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 18/18 通过（含新增测试）。
   - `EventEnvelopeContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：头部仅公共字段、payload 承载私有数据时通过。
   - 负例：头部上浮私有字段 `worker_internal_state` 被拒绝。

### 结果

1. WP02-B010 达成 Done 判定：头部仅允许通用字段，payload 分层规则可自动验证。

### 下一步

1. 按顺序推进 WP02-B011（枚举降级与弃用生命周期守卫）。

### 风险

1. 当前白名单基于 header_keys 文本校验，若后续事件编解码层字段命名存在别名，需要增加别名映射层以避免误判。
2. 当前仅校验“禁止私有字段上浮头部”，后续若需要检查 payload 结构完整性，应在后续任务新增 payload 级守卫，避免扩大本任务职责。

## 记录 #029

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B009 收敛时间语义迁移与 TimeDeadline 校验器
- 状态：已完成

### 改动

1. 扩展时间兼容守卫：
   - `contracts/include/boundary/CompatibilityGuards.h`
   - 在 `TimeoutNormalizationResult` 中新增 `used_deadline_priority`，并在 `deadline_at_ms` 存在时标记 deadline 优先路径。
2. 新增 TimeDeadline 校验器：
   - `contracts/include/boundary/TimeDeadlineGuards.h`
   - 提供 `validate_time_deadline_fields(...)`：
     - 复用 timeout 归一化；
     - 保障 `timeout_seconds` 仅兼容迁移读取；
     - 当 `created_at_ms + timeout_ms` 可与 `deadline_at_ms` 同时推导时，冲突即失败。
3. 扩展/新增合同测试并接入：
   - `tests/contract/smoke/CompatibilityContractTest.cpp`（扩展）：
     - 新增 `timeout_ms` 与 `timeout_seconds` 双字段冲突负例；
     - 增加 deadline 优先路径断言。
   - `tests/contract/smoke/TimeDeadlineContractTest.cpp`（新增）：
     - 正例：deadline 与 timeout 一致时通过；
     - 负例：deadline 与 timeout 冲突时失败。
   - `tests/contract/CMakeLists.txt`：
     - compatibility 测试名对齐为 `CompatibilityContractTest`；
     - 注册 `TimeDeadlineContractTest`。

### 测试

1. 执行验收命令（B009 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R "CompatibilityContractTest|TimeDeadlineContractTest" --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 17/17 通过（含新增测试）。
   - `CompatibilityContractTest` 与 `TimeDeadlineContractTest` 2/2 通过。
3. 覆盖摘要：
   - 正例：`timeout_seconds -> timeout_ms` 迁移路径可用，deadline 优先路径可验证。
   - 负例：`timeout_ms` 与 `timeout_seconds` 不一致冲突被拒绝。
   - 负例：`deadline_at_ms` 与 `created_at_ms + timeout_ms` 冲突被拒绝。

### 结果

1. WP02-B009 达成 Done 判定：`timeout_seconds` 仅兼容读取、双字段冲突可失败、`deadline_at` 优先规则可自动验证。

### 下一步

1. 按顺序推进 WP02-B010（EventEnvelope 头部对象与白名单校验器）。

### 风险

1. 当前冲突判定依赖 `created_at_ms` 可用；若上游出现缺失 `created_at_ms` 但同时提供 deadline 与 timeout 的输入，系统会按“deadline 优先”通过，后续若要强约束需在新任务中显式冻结。
2. compatibility 测试名已与 B009 验收命令对齐；若外部脚本仍依赖旧测试名，需要同步更新脚本以避免误报漏测。

## 记录 #028

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B008 新增统一标识元数据对象与传播校验器
- 状态：已完成

### 改动

1. 新增统一标识元数据对象与传播校验器：
   - `contracts/include/boundary/IdentityMetadata.h`
   - 定义 `IdentityMetadata`，统一承载 request/session/trace/task/lease 五类 ID 与 `parent_task_id`。
   - 提供 `validate_identity_metadata(...)`，校验五类 ID 必填、child task 必须携带 `parent_task_id`、root task 禁止携带 `parent_task_id`、以及 `parent_task_id != task_id`。
2. 新增 smoke 合同测试并接入：
   - `tests/contract/smoke/IdentityMetadataContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `IdentityMetadataContractTest`。

### 测试

1. 执行验收命令（B008 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R IdentityMetadataContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 16/16 通过（含新增测试）。
   - `IdentityMetadataContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：child task 场景下五类 ID 齐全且 parent_task_id 合法时通过。
   - 负例：child task 缺失 `parent_task_id` 被拒绝。
   - 负例：`parent_task_id` 与 `task_id` 自引用相等被拒绝。

### 结果

1. WP02-B008 达成 Done 判定：五类 ID 与 `parent_task_id` 传播关系可程序化校验且测试通过。

### 下一步

1. 按顺序继续推进 WP02-B009（收敛时间语义迁移与 TimeDeadline 校验器）。

### 风险

1. 当前传播校验依赖 `is_child_task` 语义开关，若后续系统改为通过任务拓扑自动推断父子关系，需要新增兼容入口而非改写现有字段语义。
2. 目前仅约束 parent 直接引用关系，若后续引入多级链路完整性校验（祖先追溯），应新增独立守卫，避免放大当前最小契约责任。

## 记录 #027

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B007 新增 BudgetSnapshot 契约对象与一致性校验器
- 状态：已完成

### 改动

1. 新增 BudgetSnapshot 契约对象：
   - `contracts/include/checkpoint/BudgetSnapshot.h`
   - 定义 `BudgetType`、`BudgetSnapshotEntry`、`BudgetSnapshot`，覆盖 current/max/remaining/reject_reason 统一表达。
2. 新增一致性校验器：
   - `contracts/include/checkpoint/BudgetSnapshotGuards.h`
   - 提供 `validate_budget_snapshot(...)`，校验：
     - remaining 必须等于 max-current；
     - reject_reason 仅在 remaining<0 时填写；
     - 同一快照中 budget_type 唯一。
3. 新增 checkpoint 合同测试并接入：
   - `tests/contract/checkpoint/BudgetSnapshotContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `BudgetSnapshotContractTest`。

### 测试

1. 执行验收命令（B007 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BudgetSnapshotContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 15/15 通过（含新增测试）。
   - `BudgetSnapshotContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：合法快照通过（含非超限和超限条目）。
   - 负例：remaining 与 max-current 不一致被拒绝。
   - 负例：未超限却填写 reject_reason 被拒绝。

### 结果

1. WP02-B007 达成 Done 判定：remaining 不一致和 reject_reason 误填可被稳定拦截，合法快照通过。

### 下一步

1. 按顺序推进 WP02-B008（统一标识元数据对象与传播校验器）。

### 风险

1. 当前 `remaining` 使用有符号值表达超限（可负值）；若后续输出通道限制为无符号，需要新增兼容映射字段，避免改写当前语义。
2. 目前只做单快照一致性约束，后续若引入连续快照趋势判断，应新增规则而非更改现有判定口径。

## 记录 #026

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B006 新增 RuntimeBudget 契约对象与阈值校验器
- 状态：已完成

### 改动

1. 新增 RuntimeBudget 契约对象：
   - `contracts/include/checkpoint/RuntimeBudget.h`
   - 冻结五维预算字段：max_tokens、max_turns、max_tool_calls、max_latency_ms、max_replan_count。
2. 新增 RuntimeBudget 校验器：
   - `contracts/include/checkpoint/RuntimeBudgetGuards.h`
   - 提供 `validate_runtime_budget(...)`，校验五维必填与正阈值约束。
3. 新增 checkpoint 合同测试并接入：
   - `tests/contract/checkpoint/RuntimeBudgetContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `RuntimeBudgetContractTest`。

### 测试

1. 执行验收命令（B006 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RuntimeBudgetContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 14/14 通过（含新增测试）。
   - `RuntimeBudgetContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五维字段齐全且均为正值时通过。
   - 负例：缺失 `max_turns` 被拒绝。
   - 负例：`max_latency_ms=0`（ms 口径无效阈值）被拒绝。

### 结果

1. WP02-B006 达成 Done 判定：max_tokens/max_turns/max_tool_calls/max_latency_ms/max_replan_count 均可校验且测试通过。

### 下一步

1. 按顺序推进 WP02-B007（BudgetSnapshot 契约对象与一致性校验器）。

### 风险

1. 当前守卫将五维阈值统一约束为 >0；若后续存在“某维允许 0 表示禁用”的策略，需通过新增策略字段承载，避免改写既有字段语义。
2. 历史实现若仍使用 `max_rounds` 命名，后续集成需要兼容映射层以避免命名切换带来的 breaking 风险。

## 记录 #025

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B005 新增 ErrorSource 结构与引用校验器
- 状态：已完成

### 改动

1. 新增 ErrorSource 引用结构：
   - `contracts/include/error/ErrorSourceRef.h`
   - 定义 `ErrorSourceRefEntry` 与 `ErrorSourceRefSet`，支持 primary + related 语义。
2. 新增 ErrorSource 校验器：
   - `contracts/include/error/ErrorSourceGuards.h`
   - 提供 `validate_error_source_refs(...)`，校验 primary 唯一、四类 ref_type、ref_id 非空。
3. 新增 error 合同测试并接入：
   - `tests/contract/error/ErrorSourceContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ErrorSourceContractTest`。

### 测试

1. 执行验收命令（B005 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorSourceContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 13/13 通过（含新增测试）。
   - `ErrorSourceContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：四类引用 observation/tool_call/worker_task/checkpoint 全覆盖且单 primary 通过。
   - 负例：multiple primary 被拒绝。
   - 负例：空 ref_id 被拒绝。

### 结果

1. WP02-B005 达成 Done 判定：四类引用全覆盖且非法输入可被稳定拦截。

### 下一步

1. 按顺序推进 WP02-B006（RuntimeBudget 契约对象与阈值校验器）。

### 风险

1. 当前模型允许 related 列表无序，若后续审计链路要求严格时序，需要在不破坏现有结构前提下新增序号或时间戳字段。
2. `ErrorInfo` 仍保留 B004 最小 `source_ref` 表达，后续若对接 B005 结构化集合，需通过兼容层渐进迁移，避免直接替换造成 breaking。

## 记录 #024

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B004 新增 ErrorInfo 与最小校验器
- 状态：已完成

### 改动

1. 新增 ErrorInfo 契约对象：
   - `contracts/include/error/ErrorInfo.h`
   - 定义五个必填顶层字段对应承载：failure_type、retryable、safe_to_replan、details、source_ref。
2. 新增最小校验器：
   - `contracts/include/error/ErrorInfoGuards.h`
   - 提供 `validate_error_info_required_fields(...)` 与 `is_supported_error_source_ref_type(...)`。
3. 新增 error 合同测试并接入：
   - `tests/contract/error/ErrorInfoContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ErrorInfoContractTest`。

### 测试

1. 执行验收命令（B004 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ErrorInfoContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 12/12 通过（含新增测试）。
   - `ErrorInfoContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五个必填字段齐全时通过。
   - 负例：缺失 `failure_type` 被拒绝。
   - 负例：`source_ref.ref_type` 非法取值被拒绝。

### 结果

1. WP02-B004 达成 Done 判定：failure_type/retryable/safe_to_replan/details/source_ref 缺一即失败，合法样例通过。

### 下一步

1. 按顺序推进 WP02-B005（ErrorSource 结构与引用校验器）。

### 风险

1. 当前 `source_ref` 仅实现最小键约束，B005 若引入更强引用结构需保持向后兼容，避免语义重解释。
2. `retryable` 与 `safe_to_replan` 当前只表达候选语义，后续实现层若把它们当作“已执行动作”会偏离 ADR-007，需要在集成层加门禁。

## 记录 #023

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B003 新增 ResultCode 分类与判定枚举
- 状态：已完成

### 改动

1. 新增 ResultCode 分类头文件：
   - `contracts/include/error/ResultCode.h`
   - 定义五类一级域：validation/policy/tool/provider/runtime。
2. 新增分类判定辅助能力：
   - `classify_result_code_segment(...)` 按编码段判定分类。
   - `classify_result_code(...)` 对枚举值执行分类。
   - `classify_result_code_value(...)` 对 raw code 执行 gate 友好判定（含 unknown 拒绝）。
3. 新增 error 目录合同测试并接入：
   - `tests/contract/error/ResultCodeContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ResultCodeContractTest`

### 测试

1. 执行验收命令（B003 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ResultCodeContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 11/11 通过（含新增测试）。
   - `ResultCodeContractTest` 1/1 通过。
3. 覆盖摘要：
   - 正例：五类枚举样例稳定映射到 validation/policy/tool/provider/runtime。
   - 边界例：3999 归 tool、4000 归 provider。
   - 负例：7000（越界码）被拒绝并判定为 unknown。

### 结果

1. WP02-B003 达成 Done 判定：五类失败域判定可程序化复现且边界负例通过。

### 下一步

1. 按顺序推进 WP02-B004（ErrorInfo 与最小校验器）。

### 风险

1. 当前实现采用分段分类，后续扩展具体码值时需保持段边界稳定，避免跨段重解释导致 breaking 风险。
2. 若未来新增一级分类，将触发兼容性重大变更，应走专门评审，不应在当前段内硬塞。

## 记录 #022

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B002 新增字段演进兼容判定辅助器
- 状态：已完成

### 改动

1. 新增字段演进兼容判定头文件：
   - `contracts/include/boundary/FieldEvolutionGuards.h`
   - 提供 `FieldEvolutionDecision`（non-breaking/review-required/breaking）与 `FieldEvolutionResult`。
2. 新增三类字段演进判定辅助器：
   - `classify_type_evolution(...)`（B1）
   - `classify_optionality_evolution(...)`（B2）
   - `classify_cardinality_evolution(...)`（B3）
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/FieldEvolutionGuardsContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `FieldEvolutionGuardsContractTest`

### 测试

1. 执行验收命令（B002 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R FieldEvolutionGuardsContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 10/10 通过（含新增测试）。
   - `FieldEvolutionGuardsContractTest` 1/1 通过。
3. 覆盖摘要：
   - non-breaking：类型并行新增字段且保留旧语义。
   - review-required：单值扩多值但缺少消费兼容证据。
   - breaking：既有可选字段改为强制。

### 结果

1. WP02-B002 达成 Done 判定：non-breaking/review-required/breaking 三类判定可程序化复现，断言全通过。

### 下一步

1. 按顺序推进 WP02-B003（ResultCode 分类与判定枚举）。

### 风险

1. 当前判定器是字段属性层规则，若后续引入“对象职责边界变化”场景，需由上层 checklist（A3/A5）补充门禁，避免误判为字段级变更。
2. `single->multi` 的 non-breaking 依赖“消费方兼容证据”输入，若证据口径不统一，可能导致 review-required 漏判；后续可在 B013 统一证据模板。

## 记录 #021

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-02 Build）
- 任务：WP02-B001 新增横切基础对象总入口头文件
- 状态：已完成

### 改动

1. 新增横切基础对象聚合入口头文件：
   - `contracts/include/boundary/CrossCuttingContracts.h`
   - 统一暴露五类入口：error/event/checkpoint/id-time/enum。
2. 新增 WP02-B001 对应 smoke 合同测试：
   - `tests/contract/smoke/CrossCuttingContractsSmokeTest.cpp`
   - 正例：聚合头可统一访问 error/event/checkpoint/time 入口并完成时间归一化。
   - 负例：未知枚举值通过聚合入口降级到 `Unspecified`。
3. 更新 contract 测试注册：
   - `tests/contract/CMakeLists.txt`
   - 新增 `CrossCuttingContractsSmokeTest` 注册，纳入 `dasall_contract_tests` 聚合链路。

### 测试

1. 执行验收命令（B001 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R CrossCuttingContractsSmokeTest --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 9/9 通过（含新增测试）。
   - `CrossCuttingContractsSmokeTest` 1/1 通过。

### 结果

1. WP02-B001 达成 Done 判定：聚合头已覆盖 error/event/checkpoint/id-time/enum 五类入口，且测试链路可执行并通过。

### 下一步

1. 按 WP-02 执行顺序推进 WP02-B002（字段演进兼容判定辅助器）。

### 风险

1. 当前 event 入口为阶段性 marker（字段 schema 仍待 WP02-B010），后续落地 EventEnvelope 时需保持聚合入口 API 稳定。
2. 枚举降级路径复用了 CompatibilityGuards，若后续引入生命周期守卫，需要在 WP02-B011 增补组合负例防回退。

## 记录 #020

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B007 收敛 contracts 测试入口并接入 CMake
- 状态：已完成

### 改动

1. 收敛 contract 测试注册入口：
   - `tests/contract/CMakeLists.txt`
   - 新增 `dasall_register_contract_test(...)` 统一封装 `add_executable`、`add_test`、`LABELS=contract`。
2. 收敛 contract 聚合目标依赖：
   - `tests/CMakeLists.txt`
   - `dasall_contract_tests` 改为依赖 `DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS` 统一列表，避免分散手工维护。
3. 增加注册空列表防护（负向守卫）：
   - 当收敛列表为空时，配置阶段 `FATAL_ERROR`，阻断“脚本通过但测试未注册”风险。

### 测试

1. 执行验收命令（B007 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - build 成功。
   - contract 标签测试 8/8 通过。
3. 发现性正反校验（B007 证据补充）：
   - 正例：`ctest --test-dir build-ci -N -L contract` -> `Total Tests: 8`，包含 WP01 边界测试。
   - 负例：`ctest --test-dir build-ci -N -R DefinitelyMissingContractTest` -> `Total Tests: 0`。

### 结果

1. WP01-B007 达成 Done 判定：contract 测试入口已收敛，且 ctest 可发现性与标签接入可验证。

### 下一步

1. 若后续新增边界回归测试，同步更新门禁脚本 required tests 列表并复验 gate。

### 风险

1. 统一注册函数若被绕过（直接新增 add_test 且漏 label），可能导致 gate 漏检；需在评审中强制走注册函数。
2. 当前空列表防护在 configure 阶段触发，若未来存在按 profile 裁剪测试的需求，需要同步定义白名单策略。

## 记录 #019

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B009 增加协同语义回归组合测试
- 状态：已完成

### 改动

1. 扩展协同语义 contract 测试：
   - `tests/contract/smoke/MultiAgentBoundaryContractTest.cpp`
2. 新增组合回归矩阵用例 `test_multi_agent_semantics_combination_regression_matrix`：
   - 合法组合（3 组）：
     - MultiAgentRequest: `goal_fragment`（允许）
     - MultiAgentResult: `merged_result`（允许）
     - WorkerTask: `lease_id`（允许）
   - 非法组合（3 组）：
     - MultiAgentRequest: `agent_request`（拒绝）
     - MultiAgentResult: `agent_result`（拒绝）
     - WorkerTask: `global_fsm_state`（拒绝）
3. 断言强化：
   - 对越权矩阵中每组样本同时断言 `allowed`、`decision`、`reason`，确保分层阻断行为可追溯。

### 测试

1. 执行验收命令（B009 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `MultiAgentBoundaryContractTest` 1/1 通过。
3. 覆盖说明：
   - 满足 B009 完成判定：Request/Result/WorkerTask 三组对象的越权矩阵断言全通过。

### 结果

1. WP01-B009 达成 Done 判定：协同语义“全局主控/协同子域分层”在组合场景下具备可执行回归保护。

### 下一步

1. 按顺序推进 WP01-B007（收敛 contracts 测试入口并接入 CMake，补齐 ctest 发现性证据）。

### 风险

1. 当前越权矩阵仍以字段名边界为主，若后续出现语义别名字段，需要补充矩阵覆盖。
2. reason 断言为精确字符串匹配，若后续守卫文案规范调整，需要同步更新断言预期。

## 记录 #018

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B008 增加恢复语义回归组合测试
- 状态：已完成

### 改动

1. 扩展恢复语义 contract 测试：
   - `tests/contract/smoke/RecoveryBoundaryContractTest.cpp`
2. 新增组合回归矩阵用例 `test_recovery_semantics_combination_regression_matrix`：
   - 合法组合（1 组）：
     - ReflectionDecision: `decision_kind`（允许）
     - RecoveryOutcome: `executed_action`（允许）
   - 非法组合（3 组）：
     - ReflectionDecision: `retry_after_ms`（拒绝）
     - ReflectionDecision: `backoff_strategy`（拒绝）
     - RecoveryOutcome: `failure_root_cause`（拒绝）
3. 断言强化：
   - 对每组组合同时断言 `allowed`、`decision`、`reason`，保证阻断行为与归一化原因文本可追溯。

### 测试

1. 执行验收命令（B008 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `RecoveryBoundaryContractTest` 1/1 通过。
3. 覆盖说明：
   - 满足 B008 完成判定：至少 1 组合法 + 3 组非法组合断言全部通过。

### 结果

1. WP01-B008 达成 Done 判定：恢复语义“建议权/执行权分层”在组合场景下具备可执行回归保护。

### 下一步

1. 按顺序推进 WP01-B009（协同语义回归组合测试）。

### 风险

1. 当前组合回归覆盖的是字段名边界语义；若后续引入语义等价别名字段，需同步补充矩阵样本。
2. 目前 reason 断言为精确字符串匹配，若未来规范化文案调整，需同步更新测试预期。

## 记录 #017

- 日期：2026-03-16
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B010 固化 WP01 M1 本地与 CI 门禁脚本入口
- 状态：已完成

### 改动

1. 新增 WP01 门禁脚本：
   - `scripts/ci/wp01_contract_gate.sh`
2. 脚本职责（对齐 WP01-T013 M1 Gate）：
   - 执行 configure：`cmake -S <root> -B <build-ci>`。
   - 执行 build：`cmake --build <build-ci> --target dasall_contract_tests`。
   - 执行注册校验：`ctest -N -L contract` 并强制检查关键边界测试注册存在（ContextPacketBoundaryContractTest / RecoveryBoundaryContractTest / MultiAgentBoundaryContractTest）。
   - 执行 gate：`ctest --test-dir <build-ci> -L contract --output-on-failure`。
3. 新增失败闭锁机制：
   - 任一关键 contract 测试未注册时，脚本输出 missing 项并返回非 0。
   - 支持通过环境变量 `WP01_GATE_REQUIRED_TESTS` 覆盖必需测试名列表，用于 CI 场景定制与负路径验证。

### 测试

1. 执行验收命令（B010 原样）：
   - `bash scripts/ci/wp01_contract_gate.sh`
2. 结果：
   - configure 成功。
   - build 成功。
   - 注册校验通过。
   - contract label 测试 8/8 通过。
3. 负路径验证（失败闭锁）：
   - 命令：`WP01_GATE_REQUIRED_TESTS=DefinitelyMissingContractTest bash scripts/ci/wp01_contract_gate.sh`
   - 结果：脚本返回 `NEGATIVE_RC=1`，并输出 missing required contract test registration。

### 结果

1. WP01-B010 达成 Done 判定：脚本在正常路径返回 0，并能在边界回归缺失注册时返回非 0。

### 下一步

1. 按顺序推进 WP01-B008（恢复语义回归组合测试）。

### 风险

1. 当前关键测试注册检查聚焦 WP01 三类边界核心用例，若后续新增强制边界测试，需同步更新 `WP01_GATE_REQUIRED_TESTS` 默认列表。
2. 在不同 CTest 版本下 `ctest -N` 输出格式可能存在细微差异，若格式变化导致解析误判，需要补充更稳健的解析规则。

## 记录 #016

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B006 校验协同语义分层守卫
- 状态：已完成

### 改动

1. 新增协同语义边界守卫头文件：
   - `contracts/include/boundary/MultiAgentBoundaryGuards.h`
   - 提供 `MultiAgentBoundaryDecision`、`MultiAgentBoundaryResult`、
     `kMultiAgentRequestForbiddenFields`、`kMultiAgentResultForbiddenFields`、
     `kWorkerTaskGlobalStateForbiddenFields`、
     `evaluate_multi_agent_request_field_boundary`、
     `evaluate_multi_agent_result_field_boundary`、
     `evaluate_worker_task_field_boundary`。
2. 守卫规则来源：
   - 对齐 ADR-008 与 WP01-T011，落实三类越权阻断：
     - MultiAgentRequest 不得复用 AgentRequest 语义。
     - MultiAgentResult 不得替代 AgentResult 语义。
     - WorkerTask 不得承载全局 Session/FSM 状态语义。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/MultiAgentBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `MultiAgentBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_multi_agent_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B006 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R MultiAgentBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `MultiAgentBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 8/8 通过。
3. 正负例覆盖：
   - 正例：`goal_fragment`、`merged_result`、`lease_id` 允许通过守卫。
   - 负例：`agent_request`、`agent_result`、`global_fsm_state` 均被守卫拒绝。

### 结果

1. WP01-B006 达成 Done 判定：三类协同语义越权场景全部被自动校验阻断。

### 下一步

1. 按执行顺序推进 WP01-B007（收敛 contracts 测试入口并接入 CMake）。

### 风险

1. 当前策略为字段名边界守卫，若后续引入语义等价别名字段，需要补充规则与回归用例。
2. 若后续通过嵌套结构隐式承载全局态，需要在 WP01-B009 组合回归阶段加强覆盖。

## 记录 #015

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B005 校验恢复语义分层守卫
- 状态：已完成

### 改动

1. 新增恢复语义边界守卫头文件：
   - `contracts/include/boundary/RecoveryBoundaryGuards.h`
   - 提供 `RecoveryBoundaryDecision`、`RecoveryBoundaryResult`、
     `kReflectionSchedulingForbiddenFields`、`kRecoveryAttributionForbiddenFields`、
     `evaluate_reflection_decision_field_boundary`、`evaluate_recovery_outcome_field_boundary`。
2. 守卫规则来源：
   - 对齐 ADR-007 与 WP01-T010，明确 ReflectionDecision 禁入运行时调度字段，RecoveryOutcome 禁入失败归因语义字段。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/RecoveryBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `RecoveryBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_recovery_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B005 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R RecoveryBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `RecoveryBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 7/7 通过。
3. 正负例覆盖：
   - 正例：`decision_kind` 可进入 ReflectionDecision；`executed_action` 可进入 RecoveryOutcome。
   - 负例：`retry_after_ms` 在 ReflectionDecision 被拒绝；`failure_root_cause` 在 RecoveryOutcome 被拒绝。

### 结果

1. WP01-B005 达成 Done 判定：ReflectionDecision 的调度字段误入与 RecoveryOutcome 的归因字段误入均被守卫阻断。

### 下一步

1. 按执行顺序推进 WP01-B006（协同语义分层守卫）。

### 风险

1. 当前为字段名显式黑名单策略，若后续出现语义等价别名字段，需要补充规则与回归用例。
2. 若后续将复杂归因对象以嵌套字段形式注入 RecoveryOutcome，需要在 WP01-B008 回归阶段强化防护。

## 记录 #014

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B004 校验 ContextPacket 禁入字段守卫
- 状态：已完成

### 改动

1. 新增 ContextPacket 边界守卫头文件：
   - `contracts/include/boundary/ContextBoundaryGuards.h`
   - 提供 `ContextBoundaryDecision`（AllowField/RejectForbiddenField）、`ContextBoundaryResult`、`kForbiddenContextFields`、`evaluate_context_field_boundary`、`is_allowed_context_field`。
2. 守卫规则来源：
   - 对齐 ADR-006 与 WP01-T009，仅做字段名禁入校验，拒绝 `final_messages`、`provider_payload`、`rendered_prompt`，不扩张到字段级 schema 设计。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/ContextPacketBoundaryContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `ContextPacketBoundaryContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_context_packet_boundary_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B004 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R ContextPacketBoundaryContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `ContextPacketBoundaryContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 6/6 通过。
3. 正负例覆盖：
   - 正例：`recent_history` 允许通过守卫。
   - 负例：`final_messages`、`provider_payload`、`rendered_prompt` 均被守卫拒绝。

### 结果

1. WP01-B004 达成 Done 判定：三项禁入字段全部被阻断，合法字段未被误杀。

### 下一步

1. 按执行顺序推进 WP01-B005（恢复语义分层守卫）。

### 风险

1. 当前实现是字段名精确匹配守卫，若后续引入别名或大小写变体策略，需要在不改变 ADR 结论前提下补充统一规范与测试。
2. 若后续把 provider 或消息层字段通过嵌套对象间接引入 ContextPacket，需要在 WP01-B007/B008 门禁中继续强化覆盖。

## 记录 #013

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B003 新增 Blocked/Deferred 外溢守卫接口
- 状态：已完成

### 改动

1. 新增边界守卫头文件：
   - `contracts/include/boundary/BoundaryGuards.h`
   - 提供 `BoundaryGuardDecision`（AllowStable/RejectBlocked/RejectDeferred）、`BoundaryGuardResult`、`evaluate_stable_boundary`、`can_enter_stable_boundary`。
2. 守卫逻辑来源：
   - 直接复用 `ObjectBoundaryCatalog` 的 Stable/Blocked/Deferred 分类，不新增字段级判定规则。
3. 新增 contract 测试并接入：
   - `tests/contract/smoke/BoundaryGuardsContractTest.cpp`
   - `tests/contract/CMakeLists.txt` 注册 `BoundaryGuardsContractTest`
   - `tests/CMakeLists.txt` 将 `dasall_contract_boundary_guards_test` 纳入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B003 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R BoundaryGuardsContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `BoundaryGuardsContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路 contract tests 5/5 通过。
3. 正负例覆盖：
   - 正例：Stable 对象 `AgentRequest` 被允许进入 Stable 边界。
   - 负例：Blocked 对象 `MemoryEvidence` 被拒绝，Deferred 对象 `ToolRequest` 被拒绝。

### 结果

1. WP01-B003 达成 Done 判定：Blocked/Deferred 对象均被守卫拒绝进入 Stable 清单。

### 下一步

1. 按执行顺序推进 WP01-B004（ContextPacket 禁入字段守卫）。

### 风险

1. 当前守卫仅覆盖对象级边界，若后续误把字段级语义塞入该守卫，会造成 WP 边界越界。
2. Deferred 对象在 WP-05 复审后可能调整判定，需保证守卫与冻结结论同步演进。

## 记录 #012

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B002 补齐 Stable 对象编译期标识与最小占位类型
- 状态：已完成

### 改动

1. 新增 14 个 Stable 对象 Tag 头文件（仅命名与类型标识，不定义字段语义）：
   - agent: `AgentRequestTag.h`、`GoalContractTag.h`、`ActionDecisionTag.h`、`AgentResultTag.h`、`MultiAgentRequestTag.h`、`MultiAgentResultTag.h`
   - context: `ContextPacketTag.h`
   - observation: `ObservationTag.h`、`ObservationDigestTag.h`、`ErrorInfoTag.h`
   - checkpoint: `CheckpointTag.h`、`ReflectionDecisionTag.h`、`RecoveryOutcomeTag.h`
   - task: `WorkerTaskTag.h`
2. 新增 contract 测试：
   - `tests/contract/smoke/StableTypePresenceContractTest.cpp`
   - 覆盖正例：14 个 Stable 占位类型可 include 且为空类型，且与 Stable 名册一致。
   - 覆盖负例：`MemoryEvidence`（Blocked）与 `ToolRequest`（Deferred）不得被判定为 Stable。
3. 更新测试接入：
   - `tests/contract/CMakeLists.txt` 新增 `StableTypePresenceContractTest`。
   - `tests/CMakeLists.txt` 将 `dasall_contract_stable_type_presence_test` 加入 `dasall_contract_tests` 依赖。

### 测试

1. 执行验收命令（B002 原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -R StableTypePresenceContractTest --output-on-failure`
2. 结果：
   - build 成功。
   - `StableTypePresenceContractTest` 1/1 通过。
   - `dasall_contract_tests` 聚合链路中 contract tests 4/4 通过。

### 结果

1. WP01-B002 达成 Done 判定：14 个 Stable 名称均具备可 include 的占位类型，且未引入字段语义。

### 下一步

1. 按执行顺序推进 WP01-B003（Blocked/Deferred 外溢守卫接口）。

### 风险

1. 当前仅完成对象级 Tag，占位层与后续守卫层之间仍可能出现“名称一致但行为未绑定”的漂移风险。
2. 若后续任务误在 Tag 头文件中添加字段，可能跨入 WP-02/03/04 范围并引入 breaking 风险；需继续以 contract tests 约束“空类型”不变式。

## 记录 #011

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B001 新增对象边界名册与分类枚举（复验闭环）
- 状态：已完成

### 改动

1. 沿用已落盘代码与测试产物完成复验闭环：
   - `contracts/include/boundary/ObjectBoundaryCatalog.h`
   - `tests/contract/smoke/ObjectBoundaryCatalogContractTest.cpp`
2. 依赖 WP01-B011 的 CTest 兼容修复后，恢复 B001 验收命令可执行性。

### 测试

1. 执行验收命令（B001 定义原样）：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果：
   - contract tests 3/3 通过：
     - `dasall_contract_smoke_test`
     - `dasall_contract_compatibility_test`
     - `dasall_contract_object_boundary_catalog_test`

### 结果

1. WP01-B001 从 Blocked 更新为 Done。
2. 满足 B001 完成判定：14 个 Stable、13 个 Blocked、2 个 Deferred 可枚举且测试通过。

### 下一步

1. 按执行顺序推进 WP01-B002（Stable 对象编译期标识与最小占位类型）。

### 风险

1. 当前 contract 用例数量仍偏少，后续若新增边界守卫规则需同步扩展回归测试，防止边界枚举与守卫实现漂移。

## 记录 #010

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B011 解阻 CMake 配置并恢复 contract tests 可执行性
- 状态：已完成

### 改动

1. 新增 CTest 兼容入口文件：
   - `CTestTestfile.cmake`
   - 作用：适配当前环境 CTest 3.16 不支持 `--test-dir` 的行为差异，确保在仓库根目录执行 `ctest --test-dir build-ci` 时仍可回溯到 `build-ci` 的测试图。
2. 保持最小修复边界：
   - 未改写 ADR 结论。
   - 未扩张到 WP-02/WP-03 任务范围。
   - 未新增业务语义代码，仅修复测试发现路径。

### 测试

1. 验收命令（任务定义原样执行）：
   - `cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 正例结果：
   - configure 成功。
   - build 成功。
   - ctest 执行 contract tests 3/3 通过（`dasall_contract_smoke_test`、`dasall_contract_compatibility_test`、`dasall_contract_object_boundary_catalog_test`）。
3. 负例验证：
   - 修复前（记录 #009 证据）同命令尾部会出现 `No tests were found!!!`，导致验收链不可闭环。
   - 修复后同命令可稳定发现并执行 contract tests，负例场景已消失。

### 结果

1. WP01-B011 解阻完成，状态可从 Blocked 更新为 Done。
2. B001~B010 的公共前置“contract tests 可执行”已恢复。

### 下一步

1. 回到 WP01-B001，基于已解阻环境复核并更新其状态证据。
2. 按执行顺序推进 WP01-B002（Stable 对象编译期标识与最小占位类型）。

### 风险

1. 本次采用 CTest 兼容入口文件属于“工具链兼容补丁”，若后续升级到支持 `--test-dir` 的 CTest 版本，需要确认该入口不会造成重复发现或路径歧义。
2. 若后续改变默认构建目录名称（非 `build-ci`），需同步更新该兼容入口或改为由统一脚本注入。

## 记录 #009

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-01 Build）
- 任务：WP01-B001 新增对象边界名册与分类枚举
- 状态：Blocked

### 改动

1. 新增对象边界名册头文件：
   - `contracts/include/boundary/ObjectBoundaryCatalog.h`
   - 落盘 Stable/Blocked/Deferred 三层分类与 29 个对象名册（14/13/2）。
2. 新增契约测试：
   - `tests/contract/smoke/ObjectBoundaryCatalogContractTest.cpp`
   - 覆盖正例（计数与 Stable 命名）和负例（Blocked 不可误判 Stable、Deferred 不可误判 Blocked）。
3. 更新测试注册：
   - `tests/contract/CMakeLists.txt` 新增 `dasall_contract_object_boundary_catalog_test`。
   - `tests/CMakeLists.txt` 更新 `dasall_contract_tests` 依赖，确保聚合目标会构建新增测试可执行文件。

### 测试

1. 执行验收命令：
   - `cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci -L contract --output-on-failure`
2. 结果摘要：
   - `dasall_contract_tests` 内部执行的 contract tests 为 3/3 通过（含新增 `dasall_contract_object_boundary_catalog_test`）。
   - 随后的独立 `ctest --test-dir build-ci -L contract` 在当前环境输出 `No tests were found!!!`。

### 结果

1. 代码与测试实现完成，且新增测试可编译并可在聚合目标内通过。
2. 由于验收链尾部命令在当前环境无法发现测试，按 Build TODO 规则将 WP01-B001 标记为 Blocked。

### 下一步

1. 先解阻 `ctest --test-dir build-ci` 可发现测试的问题（建议纳入 WP01-B011 解阻链处理）。
2. 解阻后复跑 WP01-B001 验收命令并将状态从 Blocked 更新为 Done。

### 风险

1. 若忽略该环境差异直接标记 Done，会导致“同一验收命令在不同环境结果不一致”的门禁漂移。
2. 本次为保证验收可执行性触及 `tests/CMakeLists.txt` 聚合依赖，存在轻微跨任务边界风险，后续需在 WP01-B007 统一收敛测试编排。

## 记录 #008

- 日期：2026-03-15
- 阶段：contracts 冻结（WP-02 收束 + WP-03 启动）
- 任务：修正“仅 Design 输出”偏差，补齐 Build 落地基线与执行约束
- 状态：进行中

### 完成内容

1. 明确并记录决策偏差：
   - 识别出“按强 design 约束推进时，任务可在文档层通过但缺少 build 落盘证据”的过程问题。
   - 形成统一结论：后续任务采用“Design 先行 + 分批 Build 验证”模式，禁止全量设计后一次性回补实现。
2. 新设计并落地两份 Build TODO 相关文档：
   - 完成 B1 build 向文档：`WP02-T015-B1-timeout迁移清单.md`（迁移映射、冲突判定、弃用窗口、回退策略）。
   - 完成 B2 build 向文档：`WP02-T015-B2-枚举降级契约测试基线.md`（unknown->Unspecified 证据基线）。
3. 完成 Build 落盘与验证闭环：
   - 新增兼容辅助代码与契约测试：`CompatibilityGuards.h`、`CompatibilityContractTest.cpp`。
   - 清理历史 `build-ci` 缓存路径冲突后，完成构建与 contract tests 执行。
   - `dasall_contract_compatibility_test` 执行通过，B2 由 In Review 转 Closed。
4. 完成冻结状态同步：
   - WP02-T015 M2 冻结包从 CONDITIONAL FREEZE 收束为 FROZEN。
   - WP-02 看板 T015 状态更新为 Done。
   - WP03-T001 解除 Blocked 并转 In Review（前置依赖闭环）。
5. 新增流程模板资产：
   - 在 `docs/development/` 新增 Build TODO 生成提示词模板，用于后续任务强制输出“代码+测试+验收命令”三件套。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-B1-timeout迁移清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
- `/home/gangan/DASALL-Agent/contracts/include/boundary/CompatibilityGuards.h`
- `/home/gangan/DASALL-Agent/tests/contract/smoke/CompatibilityContractTest.cpp`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-M2冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/WP-02-横切基础对象TODO.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP03-T001-主链路对象依赖表.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/WP-03-主链路对象TODO.md`
- `/home/gangan/DASALL-Agent/docs/development/Build开发任务TODO生成提示词模板.md`

### 验证结果

1. `bash scripts/ci/build.sh` 通过（修复历史 cache 路径冲突后）。
2. `bash scripts/ci/contract_tests.sh` 通过，`dasall_contract_compatibility_test` 通过并留档。
3. 相关更新文档、头文件、测试文件均通过文件级错误检查（No errors found）。
4. WP02-T015 与 WP03-T001 状态同步一致，无“文档结论与看板状态”漂移。

### 中断恢复点（下次会话从这里继续）

- WP-02 已冻结完成（M2=FROZEN，T015=Done）。
- WP-03 已解除前置阻塞，当前从 T002/T003 继续推进“Design+Build 并行落地”。
- 建议优先顺序：
  - `docs/todos/contracts-freeze/WP-03-主链路对象TODO.md`
  - `docs/todos/contracts-freeze/deliverables/WP03-T002-AgentRequest语义说明.md`
  - `docs/todos/contracts-freeze/deliverables/WP03-T003-AgentRequest字段表.md`
  - `tests/contract/smoke/`（同步新增 WP-03 契约测试）

### 风险/注意事项

- 若后续再次只产出 design 文档而不落盘 build 证据，WP-03/WP-04 将累计实现债务并放大返工成本。
- 需将“代码+测试+验收命令”作为应有 build 任务的硬门槛，未满足不得标记 Done。
- 新增 build 任务应继续遵守 M2 Gate，不得回退横切语义冻结结论。

## 记录 #007

- 日期：2026-03-14
- 阶段：contracts 冻结（WP-02 横切基础对象）
- 任务：收束 WP02 横切基础对象冻结，发布 M2 冻结包并补齐 B1/B2 阻塞处置资产
- 状态：进行中

### 完成内容

1. 完成 WP-02 冻结发布收束：
   - 形成 WP02-T015 M2 冻结包，汇总横切错误、预算、标识、时间、事件封套、枚举规则与 M2 Gate 门禁。
   - 更新 WP-02 TODO，将 T015 挂接到正式交付物并置为 In Review。
2. 完成 B1 设计闭环：
   - 识别 `timeout_seconds -> timeout_ms` 属于设计阶段的兼容性迁移问题，而非实现返工问题。
   - 落地 B1 迁移清单，明确字段映射、冲突判定、弃用窗口和回退策略。
3. 完成 B2 基线补齐：
   - 落地枚举 unknown -> Unspecified 降级契约测试基线文档。
   - 在 contracts/include 下新增最小兼容辅助头，在 tests/contract 下新增 compatibility contract test 与 CMake 接入。
4. 完成冻结包状态校正：
   - 将 B1 标记为 Closed。
   - 将 B2 保持为 In Review，等待 contract test 实际执行通过后再关闭。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T014-评审纪要.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-M2冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-B1-timeout迁移清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/WP-02-横切基础对象TODO.md`
- `/home/gangan/DASALL-Agent/contracts/include/boundary/CompatibilityGuards.h`
- `/home/gangan/DASALL-Agent/tests/contract/smoke/CompatibilityContractTest.cpp`
- `/home/gangan/DASALL-Agent/tests/contract/CMakeLists.txt`

### 验证结果

1. 新增与更新的文档、头文件、测试文件均通过文件级错误检查（No errors found）。
2. 已确认 `contracts/` 当前仍无正式接口/数据结构实现，新增代码仅为兼容辅助层与契约测试基线。
3. 已确认 `tests/contract/` 除 smoke 基线外新增 compatibility contract test 入口。
4. CMake Tools 当前无法完成项目配置，导致 build/ctest 无法执行；因此 B2 不能标记为 Closed。

### 中断恢复点（下次会话从这里继续）

- WP-02 已基本收束：M2 冻结包已发布，B1 已关闭，B2 待执行验证。
- 下一任务建议：先修复当前工作区 CMake 配置问题并执行 `dasall_contract_compatibility_test`，通过后关闭 B2。
- 之后进入 WP-03 主链路对象的首个原子任务。
- 建议优先顺序：
  - `docs/todos/contracts-freeze/deliverables/WP02-T015-M2冻结包.md`
  - `docs/todos/contracts-freeze/deliverables/WP02-T015-B1-timeout迁移清单.md`
  - `docs/todos/contracts-freeze/deliverables/WP02-T015-B2-枚举降级契约测试基线.md`
  - `tests/contract/smoke/CompatibilityContractTest.cpp`

### 风险/注意事项

- 当前最大阻塞不是语义设计，而是 CMake 配置失败；在测试未实际跑通前，B2 只能保持 In Review。
- `timeout_seconds` 的问题是设计阶段主动暴露的兼容性风险，不代表已有大规模实现返工，但后续实现必须严格遵守迁移清单。
- unknown 枚举值降级必须集中走兼容辅助层，避免各子域自行定义 fallback 逻辑。

## 记录 #006

- 日期：2026-03-14
- 阶段：contracts 冻结（WP-01 术语与对象地图）
- 任务：完成 WP01-T002 至 WP01-T013，发布 M1 冻结包
- 状态：已完成

### 完成内容

1. 完成术语基线收束：
   - 术语归并、定义、消费者分层完成并形成稳定主名称集合。
2. 完成对象地图收束：
   - 顶层对象流图、稳定对象标注、内部/禁止外溢对象清单完成。
3. 完成边界规则收束：
   - 发布 contracts 边界说明 v1，固化 Stable/Blocked/Deferred 三层模型。
4. 完成 ADR 对齐核对：
   - ADR-006（ContextPacket 禁入字段）
   - ADR-007（建议权与执行权分层）
   - ADR-008（全局主控与协同子域分层）
5. 完成整体评审与冻结发布：
   - 形成 WP01-T012 评审纪要（有条件通过）
   - 发布 WP01-T013 M1 冻结包并将 T013 状态更新为 Completed。

### 关键产物

- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T003-术语定义表-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T004-术语消费者矩阵.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T005-顶层对象流图-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T006-稳定对象标注版流图.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T007-内部对象边界清单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T008-contracts边界说明-v1.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T009-ContextPacket约束核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T010-恢复语义核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T011-协同语义核对单.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T012-整体骨架评审纪要.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/deliverables/WP01-T013-M1冻结包.md`
- `/home/gangan/DASALL-Agent/docs/todos/contracts-freeze/WP-01-术语与对象地图TODO.md`

### 验证结果

1. WP01-T009、T010、T011 核对单均完成并通过一致性检查。
2. WP01-T012 形成“可进入 WP-02”的评审结论与门禁条件。
3. WP01-T013 冻结包发布完成，T013 已标记为 Completed。
4. 本轮新增与更新文档均通过文件级错误检查（No errors found）。

### 中断恢复点（下次会话从这里继续）

- WP-01 已闭环完成（T013 Completed）
- 下一任务建议：进入 WP-02 横切基础对象，优先冻结入口/结果/标识元数据与错误域基线
- 建议优先顺序：
  - `docs/todos/contracts-freeze/WP-02-横切基础对象TODO.md`
  - `contracts/include/agent/`
  - `contracts/include/error/`
  - `contracts/include/context/`

### 风险/注意事项

- Deferred 对象 `ToolRequest`、`ToolResult` 在 WP-05 前仍为阶段性不外溢，避免被误判为永久禁止或提前冻结。
- 文档中若出现 `Orchestrator` 简称，需明确区分 `AgentOrchestrator` 与 `MultiAgentCoordinator`，避免主控权误读。
- 学习材料中的 ContextPacket 历史示例与 ADR-006 存在旧口径偏差，不作为冻结依据，但需在文档治理任务中纠偏。

## 记录 #005

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立编码规范、命名规范、分支与提交流程
- 状态：已完成

### 完成内容

1. 新建工程协作规范文档：
   - `/home/gangan/DASALL-OS/docs/development/DASALL_工程协作与编码规范.md`
   - 内容覆盖编码规范、命名规范、分支策略、提交格式、PR 要求、阶段 A/B 特殊约束
2. 新建基础格式控制文件：
   - `/home/gangan/DASALL-OS/.editorconfig`
   - `/home/gangan/DASALL-OS/.clang-format`
3. 新建提交与 PR 模板：
   - `/home/gangan/DASALL-OS/.gitmessage.txt`
   - `/home/gangan/DASALL-OS/.github/pull_request_template.md`
4. 固化协作约定：
   - 分支命名规则：`feature/`、`fix/`、`refactor/`、`docs/`、`test/`、`chore/`、`release/`
   - 提交格式：`type(scope): summary`
   - PR 模板要求包含阶段/任务、影响范围、验证方式、风险与回滚点

### 关键产物

- `/home/gangan/DASALL-OS/docs/development/DASALL_工程协作与编码规范.md`
- `/home/gangan/DASALL-OS/.editorconfig`
- `/home/gangan/DASALL-OS/.clang-format`
- `/home/gangan/DASALL-OS/.gitmessage.txt`
- `/home/gangan/DASALL-OS/.github/pull_request_template.md`

### 验证结果

1. 规范文档已落地，可直接作为阶段 A 之后的统一协作基线。
2. `.editorconfig`、`.clang-format`、提交模板、PR 模板均已创建，可被后续 IDE、格式化工具和代码评审流程直接使用。

### 中断恢复点（下次会话从这里继续）

- 阶段 A 已全部完成
- 下一任务建议：进入阶段 B，开始 `contracts/` 契约层冻结与契约测试
- 建议优先顺序：
  - `contracts/include/agent/`
  - `contracts/include/error/`
  - `contracts/include/context/`
  - `tests/contract/`

### 对后续有用的信息

- 当前协作约定已形成“文档 + 模板 + 基础格式配置”三层结构，不要再分散定义第二套规范。
- 命名规则已经固定：类型 PascalCase，函数/变量 lower_snake_case，成员变量以 `_` 结尾，常量 `kPascalCase`。
- 在 contracts 冻结前，优先保持接口、命名、目录结构稳定，不要过早引入风格分歧或临时命名。

## 记录 #004

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：初始化 tests 目录结构与公共 Mock 框架
- 状态：已完成

### 完成内容

1. 将 tests 根入口升级为分层结构：
   - 更新 `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
   - 接入 `mocks/`、`unit/`、`contract/` 子目录
   - 保留 `unit` / `contract` 标签约定，并改为真实测试可执行程序
2. 建立公共测试支持库：
   - 新建 `/home/gangan/DASALL-OS/tests/mocks/CMakeLists.txt`
   - 提供 `dasall_test_support` 供后续单元测试和契约测试复用
3. 建立首批公共 Mock 头文件：
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockLLMAdapter.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockTool.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockExecutionService.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/MockMemoryStore.h`
   - `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/support/TestAssertions.h`
4. 初始化 unit/contract 测试目录入口：
   - 新建 `/home/gangan/DASALL-OS/tests/unit/CMakeLists.txt`
   - 新建各子目录 CMakeLists（runtime/cognition/llm/tools/memory/knowledge）
   - 新建 `/home/gangan/DASALL-OS/tests/contract/CMakeLists.txt`
5. 新增首批真实测试程序：
   - `/home/gangan/DASALL-OS/tests/unit/runtime/RuntimeSmokeTest.cpp`
   - `/home/gangan/DASALL-OS/tests/contract/smoke/ContractSmokeTest.cpp`

### 关键产物

- `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/mocks/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/unit/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/contract/CMakeLists.txt`
- `/home/gangan/DASALL-OS/tests/mocks/include/dasall/tests/mocks/`
- `/home/gangan/DASALL-OS/tests/unit/runtime/RuntimeSmokeTest.cpp`
- `/home/gangan/DASALL-OS/tests/contract/smoke/ContractSmokeTest.cpp`

### 验证结果

1. 重新执行 `scripts/ci/build.sh` 通过。
2. `scripts/ci/unit_tests.sh` 通过，真实单测程序 `dasall_runtime_smoke_test` 运行通过。
3. `scripts/ci/contract_tests.sh` 通过，真实契约测试程序 `dasall_contract_smoke_test` 运行通过。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 5 项
- 任务内容：建立编码规范、命名规范、分支与提交流程
- 建议先落地：
  - `/home/gangan/DASALL-OS/docs/`
  - `/home/gangan/DASALL-OS/.github/`
  - 或 `/home/gangan/DASALL-OS/docs/worklog/` 中追加工程约定文档引用

### 对后续有用的信息

- 当前 `tests/mocks` 是“测试脚手架层”，故意不依赖未来生产接口，避免在 `contracts/` 冻结前反复返工。
- 等阶段 B 冻结 `IXxx` 接口后，可以将 `MockLLMAdapter`、`MockExecutionService`、`MockMemoryStore` 逐步替换为真正继承生产接口的 mock。
- 当前已有稳定标签约定：`unit`、`contract`；CI 与本地脚本都依赖该约定。

## 记录 #003

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立基础 CI 流水线（编译、单测、契约测试、静态检查）
- 状态：已完成

### 完成内容

1. 建立本地与 CI 复用脚本：
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/build.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/unit_tests.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/contract_tests.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/static_check.sh`
   - 新建 `/home/gangan/DASALL-OS/scripts/ci/ci_local.sh`
2. 建立 GitHub Actions 工作流：
   - 新建 `/home/gangan/DASALL-OS/.github/workflows/ci.yml`
   - 流程顺序：Build -> Unit tests -> Contract tests -> Static checks
3. 完善测试标签与目标：
   - 更新 `/home/gangan/DASALL-OS/tests/CMakeLists.txt`
   - 增加 `dasall_unit_smoke`（label: unit）
   - 增加 `dasall_contract_smoke`（label: contract）
4. CI 稳定性修正：
   - CI 脚本默认使用独立构建目录 `build-ci`，避免与手工构建目录 generator 冲突
   - 将 `ctest` 改为在构建目录内执行，兼容本地工具链

### 关键产物

- `/home/gangan/DASALL-OS/.github/workflows/ci.yml`
- `/home/gangan/DASALL-OS/scripts/ci/build.sh`
- `/home/gangan/DASALL-OS/scripts/ci/unit_tests.sh`
- `/home/gangan/DASALL-OS/scripts/ci/contract_tests.sh`
- `/home/gangan/DASALL-OS/scripts/ci/static_check.sh`
- `/home/gangan/DASALL-OS/scripts/ci/ci_local.sh`
- `/home/gangan/DASALL-OS/tests/CMakeLists.txt`

### 验证结果

1. 本地执行 `build.sh` 通过，编译成功。
2. 本地执行 `unit_tests.sh` 通过，`unit` 标签测试 1 项通过。
3. 本地执行 `contract_tests.sh` 通过，`contract` 标签测试 1 项通过。
4. 本地执行 `static_check.sh` 成功退出；由于本机未安装 `cppcheck`/`clang-tidy`，当前为跳过状态。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 4 项
- 任务内容：初始化 `tests/` 目录结构与公共 Mock 框架（从 smoke 升级到可复用测试基座）
- 建议先落地：
  - `/home/gangan/DASALL-OS/tests/mocks/`
  - `/home/gangan/DASALL-OS/tests/unit/`
  - `/home/gangan/DASALL-OS/tests/contract/`

### 对后续有用的信息

- 统一本地 CI 入口为：`bash scripts/ci/ci_local.sh`。
- 若需在本地启用静态检查，安装依赖：`clang-tidy` 与 `cppcheck`。
- 当前单测/契约测试是 smoke 基线，后续可替换为 GoogleTest 并保留 `unit`/`contract` 标签约定。

## 记录 #002

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：建立统一编译选项、第三方依赖接入策略（submodule + 本地 cache + FetchContent）
- 状态：已完成

### 完成内容

1. 新增统一编译选项模块：
   - 新建 `/home/gangan/DASALL-OS/cmake/DASALLOptions.cmake`
   - 定义 `dasall_build_options` 与 `dasall_apply_common_options()`
   - 按 `CMAKE_SYSTEM_PROCESSOR` 自动区分 x86/ARM/Generic，并注入架构宏
   - 统一 GCC/Clang 编译与链接选项，支持 Linux x86 与 ARM 交叉场景
2. 新增第三方依赖解析策略模块：
   - 新建 `/home/gangan/DASALL-OS/cmake/DASALLThirdParty.cmake`
   - 实现统一依赖解析函数 `dasall_resolve_dependency()`
   - 解析优先级：submodule > 本地 cache > FetchContent（严格按要求）
3. 接入根工程与模块：
   - 根 CMake 引入上述两个模块并输出依赖策略信息
   - 各模块与 apps 目标统一接入 `dasall_build_options`
   - 修复模块 include 路径错误（`/include` -> `${CMAKE_CURRENT_SOURCE_DIR}/include`）
4. 建立本地 cache 落地点与说明：
   - 新建 `/home/gangan/DASALL-OS/third_party/.cache/`
   - 新建 `/home/gangan/DASALL-OS/third_party/README.md`

### 关键产物

- `/home/gangan/DASALL-OS/cmake/DASALLOptions.cmake`
- `/home/gangan/DASALL-OS/cmake/DASALLThirdParty.cmake`
- `/home/gangan/DASALL-OS/CMakeLists.txt`
- `/home/gangan/DASALL-OS/third_party/.cache/.gitkeep`
- `/home/gangan/DASALL-OS/third_party/README.md`

### 验证结果

1. 重新执行 CMake 配置通过，成功生成 build 系统。
2. 配置日志显示策略生效：`submodule > local cache > FetchContent`。
3. 本地 cache 在源码目录 `third_party/.cache` 下，常规编译清理不会删除该目录。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 3 项
- 任务内容：建立基础 CI 流水线（编译、单测、契约测试、静态检查）
- 建议先落地：
  - `/home/gangan/DASALL-OS/.github/workflows/`（若使用 GitHub Actions）
  - 或 `/home/gangan/DASALL-OS/scripts/ci/`

### 对后续有用的信息

- 依赖默认不会在 configure 阶段自动联网拉取，`DASALL_BOOTSTRAP_THIRD_PARTY` 默认 OFF。
- 如需严格离线构建，建议设定：`-DDASALL_ALLOW_FETCHCONTENT=OFF`。
- 统一编译选项已集中到 cmake 模块，后续新增 target 需调用 `dasall_apply_common_options()`。

## 记录 #001

- 日期：2026-03-12
- 阶段：阶段 A（工程基线与开发骨架）
- 任务：创建顶层目录骨架与各模块 CMakeLists.txt
- 状态：已完成

### 完成内容

1. 创建工程顶层目录骨架：
   - apps, contracts, runtime, cognition, llm, tools, memory, knowledge, services, multi_agent, platform, infra, profiles, skills, tests, third_party, cmake, scripts, sysroots, debian
2. 为核心模块创建 CMakeLists：
   - 根 CMakeLists
   - 各子模块 CMakeLists
   - apps 子模块及占位 main.cpp
3. 创建 profiles 初始文件：
   - 每个 profile 包含 profile.cmake 与 runtime_policy.yaml

### 关键产物

- 根构建文件：/home/gangan/DASALL-OS/CMakeLists.txt
- 模块构建文件：/home/gangan/DASALL-OS/*/CMakeLists.txt
- 执行指引：/home/gangan/DASALL-OS/docs/todos/DASALL_工程落地实现步骤指引.md

### 验证结果

1. 已完成 CMake 配置验证：build 目录成功生成。
2. 本机 CMake 为 3.16.3，根工程最低版本已设为 3.16，配置通过。

### 中断恢复点（下次会话从这里继续）

- 下一任务：阶段 A 第 2 项
- 任务内容：建立统一编译选项、第三方依赖接入策略（submodule 或 FetchContent）
- 建议落地点：
  - /home/gangan/DASALL-OS/cmake/
  - /home/gangan/DASALL-OS/third_party/
  - /home/gangan/DASALL-OS/CMakeLists.txt

### 对后续有用的信息

- 当前骨架已可配置，但尚未建立统一 warning、sanitizer、build type 策略。
- tests 目录为占位，后续需引入 GoogleTest 并替换 placeholder 测试目标。
- 当前 apps 为占位可执行，后续应改为依赖真实 runtime 接口与装配层。
