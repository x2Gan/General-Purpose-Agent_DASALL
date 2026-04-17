# TOOL-TODO-038 SkillRuntime 收敛

日期：2026-04-17  
任务：TOOL-TODO-038  
状态：已完成

## 1. 目标

1. 把 037 产出的 `SkillMatchResult` 真正接入 tools runtime，使 skill 能从“可匹配资产”推进到“可实例化执行计划”。
2. 落地 `instantiate()`、`bind_workflow_template()`、`build_tool_allowlist()`、`release_instance()` 四个内部入口，保证 `SkillRuntime` 只做实例化与 workflow plan 绑定，不越权执行 workflow。
3. 保持 038 的边界清晰：只消费 normalized `SkillSpecAsset` 与 internal workflow sample，不提前把 external dialect parsing 或 plugin bundle import 混进同一轮提交。

## 2. 实现落点

1. 新增 `tools/src/skills/SkillRuntime.h` 与 `tools/src/skills/SkillRuntime.cpp`：
   - 定义 `SkillInstance` 与 `SkillInstantiateResult`；
   - `instantiate()` 基于 `SkillMatchResult` 与 `ToolPolicyView` 生成实例、输出 deterministic reason code，并维护 active instance 表；
   - `build_tool_allowlist()` 依据 profile constraints、required domains 与 `allowed_tool_domains` 做 fail-closed 收敛；
   - `bind_workflow_template()` 读取 036 的 canonical workflow YAML，把 skill workflow 绑定成 `WorkflowPlan` 与 `ToolIR` step 集合；
   - `release_instance()` 支持按实例 id 撤销运行时句柄。
2. 更新 `tools/CMakeLists.txt`，把 `src/skills/SkillRuntime.cpp` 接入 `dasall_tools`。
3. 新增 `tests/unit/tools/SkillRuntimeInstantiateTest.cpp`：
   - 验证实例化成功后能产出 `SkillInstance` 与 `WorkflowPlan`；
   - 验证 allowlist 会按 policy domain 收敛；
   - 验证 policy 拒绝时保留 fallback strategy 与 denied tool 列表。
4. 更新 `tests/unit/tools/CMakeLists.txt`，新增 `dasall_skill_runtime_instantiate_unit_test`。

## 3. 关键设计结论

1. `SkillRuntime` 仍然只消费 normalized asset，不负责解析 `SKILL.md` frontmatter 或 plugin bundle；039 继续承担 external dialect / plugin bundle importer 职责。
2. workflow template 绑定采用 module-local 轻量 YAML 读取，只支持 036 已冻结的 canonical sample shape；缺失 template 或字段不完整时直接 fail-closed，不临时拼计划。
3. policy 收敛策略保持显式：`required_domains` 不满足直接返回空 allowlist；若 `allowed_tools` 被 policy 裁掉任一项，`instantiate()` 返回 `skill.runtime.policy_denied` 并保留 fallback strategy。
4. `SkillRuntime` 只生成 `WorkflowPlan` 与实例句柄，不替代 `WorkflowEngine` 执行 step，也不旁路 `ToolRouteSelector` / `ToolPolicyGate` 的后续责任。

## 4. 测试覆盖

1. 新增 `tests/unit/tools/SkillRuntimeInstantiateTest.cpp`：
   - 验证 internal runtime incident sample 可完成实例化与 workflow bind；
   - 验证 allowlist 能过滤被 policy 禁止的 domain；
   - 验证 policy denial 场景下 fallback strategy 与 denied tool 名称可自动断言。
2. 复跑 `SkillRegistryTest` 与 `SkillRegistryPriorityTest`，确认 038 没有破坏 037 的 registry 契约。
3. 通过 `ListTests_CMakeTools` 验证 `SkillRuntimeInstantiateTest` 已被 tools unit discoverability 接入。

## 5. 验证

1. 构建：
   - `Build_CMakeTools`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `SkillRegistryTest`, `SkillRegistryPriorityTest`, `SkillRuntimeInstantiateTest`
3. discoverability：
   - `ListTests_CMakeTools`
4. 结果：
   - `dasall_tools` 与新增 unit target 构建通过，新增 `SkillRuntime` 代码保持 warning-clean；
   - `SkillRegistryTest`、`SkillRegistryPriorityTest`、`SkillRuntimeInstantiateTest` 全部通过；
   - `ListTests_CMakeTools` 可发现 `SkillRuntimeInstantiateTest`；
   - 历史 `DartConfiguration.tcl` 噪声仍存在，但不影响通过结论。

## 6. 对后续任务的影响

1. 039 可以直接把 external dialect / plugin bundle 归一化成 `SkillSpecAsset`，再复用现有 `SkillRuntime` 做实例化，不需要另建第二套 runtime 绑定逻辑。
2. 040 的 `ToolSkillRuntimeIntegrationTest` 现在已有 runtime 基座，后续重点只剩 importer 接线和 integration 黑盒验证。
3. `SkillRuntime` 已经固定了 fail-closed 边界，039/040 不应再把“缺失 workflow template 也可临时执行”之类的隐式兼容带回系统。

## 7. 风险与后续

1. 当前 workflow YAML 绑定器故意保持轻量，只覆盖 036 冻结的 canonical sample shape；若后续需要更复杂的 DAG 元数据，应继续在 module-local parser 层演进，而不是扩 shared contracts。
2. `SkillRuntime` 当前只消费文件引用，不负责资产目录扫描；039 引入 importer 时必须继续维持“解析/归一化”和“实例化/绑定”分层。
3. external importer 继续默认关闭；038 只落内部 runtime，不改变 036 冻结的 feature flag 边界。