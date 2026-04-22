# RT-TODO-024 Contracts 字段级与 Checkpoint Replay 兼容设计收敛

## 本地证据

1. `docs/architecture/DASALL_runtime子系统详细设计.md` 6.20 已把 `rt.schema_version`、`rt.fsm_state_enum_version`、`rt.budget_schema_version` 固定为 runtime checkpoint 版本兼容性的唯一保留 tag。
2. 同文档 9.3 已明确 contract 验证面覆盖 `RuntimeBudget`、`Checkpoint`、`RecoveryRequest`、`RecoveryOutcome` 等对象，024 不应重定义这些对象，而是把 runtime-owned replay 兼容验证建立在既有字段级 contract guard 之上。
3. 同文档 9.4 已明确 resume / replay 允许使用 runtime-owned checkpoint fixture 做 regression，但这不自动等于真实持久化 round-trip ready。
4. `runtime/src/checkpoint/CheckpointManager.cpp` 当前直接在 `validate(...)` / `load(...)` / `make_resume_plan(...)` 上决定版本校验、pending_action 校验与 terminal-state reject，是 024 最直接的控制面。
5. `tests/contract/checkpoint/RuntimeBudgetContractTest.cpp`、`CheckpointFieldContractTest.cpp`、`ReflectionDecisionContractTest.cpp`、`RecoveryRequestContractTest.cpp`、`RecoveryOutcomeContractTest.cpp` 已存在且可运行，说明 024 的缺口不是 shared contract guard 缺失，而是 runtime-owned golden fixture replay 证据还没有落盘。

## 外部参考

1. Temporal durable execution / replay 文档强调：确定性 replay 必须围绕固定的历史记录/持久化事实重复执行并得到一致结论。024 借用的是“同一 golden fixture 重复加载必须得到稳定 ResumePlan / reject 语义”的原则，而不是引入 Temporal 风格工作流运行时。

## 设计结论

1. 024 复用现有 contract tests 作为字段级验证面，不扩 shared contracts，也不新造 runtime-owned contract object。
2. 024 的新增 Build 面只包含三类内容：
   - `tests/fixtures/runtime/checkpoints/` 下的 golden checkpoint fixtures；
   - `tests/integration/agent_loop/RuntimeCheckpointReplayCompatibilityTest.cpp`；
   - `tests/integration/agent_loop/CMakeLists.txt` 与 `tests/integration/CMakeLists.txt` 的 discoverability 接线。
3. integration test 必须直接打在 `CheckpointManager::validate/load/make_resume_plan`：
   - 同一合法 fixture 重复加载时得到稳定 `ResumePlan`；
   - schema version mismatch 显式拒绝并给出 `RT_E_412_RESUME_REJECTED`；
   - waiting checkpoint 缺失 `pending_action` 显式拒绝；
   - terminal checkpoint 保持字段合法但不生成 `ResumePlan`。
4. fixture 采用简单 `key=value` 文本格式，避免引入额外 JSON/YAML 解析依赖；重点是持久化事实稳定，而不是序列化框架本身。

## 边界与职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| 既有 contract tests | 证明 shared contract 字段边界未回退 | 不证明 runtime-owned replay 语义 |
| Golden checkpoint fixtures | 固化 replay 输入事实 | 不承担真实存储 round-trip |
| `RuntimeCheckpointReplayCompatibilityTest` | 证明 fixture -> `CheckpointManager` 的稳定 ResumePlan / reject 语义 | 不宣称真端口持久化 ready |

## 数据与接口说明

1. fixture 字段最小集：`checkpoint_id`、`state`、`step_id`、`working_memory_snapshot`、可选 `pending_action`、保留 tag。
2. 版本 tag 只允许通过 `tag.rt.schema_version`、`tag.rt.fsm_state_enum_version`、`tag.rt.budget_schema_version` 注入。
3. integration test 使用 `CheckpointManager::seed_for_test(...)` + `load(...)` / `make_resume_plan(...)` 进行 runtime-owned replay regression。

## 流程

1. 读取 golden fixture 并构造成 `contracts::Checkpoint`。
2. 对合法 fixture：`validate(...) -> load(...) -> make_resume_plan(...)`，并断言重复加载后的 `ResumePlan` 稳定。
3. 对 schema mismatch fixture：`load(...)` 直接返回 `RT_E_412_RESUME_REJECTED`，`make_resume_plan(...)` 不可恢复。
4. 对 waiting 缺失 `pending_action` fixture：`validate(...)` 返回 `MissingPendingAction`，`make_resume_plan(...)` 不可恢复。
5. 对 terminal fixture：字段合法但 `make_resume_plan(...)` 返回 `UnsupportedCheckpointState`。

## 文件范围

| 设计项 | 文件 |
|---|---|
| golden fixtures | `tests/fixtures/runtime/checkpoints/*.fixture` |
| replay compatibility integration | `tests/integration/agent_loop/RuntimeCheckpointReplayCompatibilityTest.cpp` |
| integration discoverability | `tests/integration/agent_loop/CMakeLists.txt`、`tests/integration/CMakeLists.txt` |

## Design -> Build 映射

1. 代码目标：`tests/fixtures/runtime/checkpoints/`、`tests/integration/agent_loop/RuntimeCheckpointReplayCompatibilityTest.cpp`
2. 测试目标：`RuntimeBudgetContractTest`、`CheckpointFieldContractTest`、`ReflectionDecisionContractTest`、`RecoveryRequestContractTest`、`RecoveryOutcomeContractTest`、`RuntimeCheckpointReplayCompatibilityTest`
3. 验收命令：`cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_contract_tests dasall_runtime_checkpoint_replay_compatibility_integration_test && ctest --test-dir build-ci -R "^(RuntimeBudgetContractTest|CheckpointFieldContractTest|ReflectionDecisionContractTest|RecoveryRequestContractTest|RecoveryOutcomeContractTest|RuntimeCheckpointReplayCompatibilityTest)$" --output-on-failure`

## D 原子项

| ID | 设计目标 | 完成判定 | 结果 |
|---|---|---|---|
| D1 | 复用既有 contract tests | 不重复造 shared contract guard | PASS |
| D2 | 固定 golden fixture 输入面 | fixture 足以表达合法、schema mismatch、missing pending_action、terminal 四类场景 | PASS |
| D3 | 固定 replay compatibility 判别点 | `CheckpointManager::validate/load/make_resume_plan` 上能做二值断言 | PASS |
| D4 | 固定 Build 三件套 | 代码目标、测试目标、验收命令齐备 | PASS |

## D Gate

1. 024 设计交付物已落盘。
2. 024 不扩 shared ABI，不把 runtime-owned fixture 结果外推为真持久化 ready。
3. replay 兼容验证已被约束到 `CheckpointManager` 当前真实控制面。

结论：D Gate = PASS，可进入 RT-TODO-024 Build。