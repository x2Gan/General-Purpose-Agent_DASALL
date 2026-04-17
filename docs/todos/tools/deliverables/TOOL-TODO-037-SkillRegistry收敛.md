# TOOL-TODO-037 SkillRegistry 收敛

日期：2026-04-17  
任务：TOOL-TODO-037  
状态：已完成

## 1. 目标

1. 把 036 冻结的 normalized `SkillSpecAsset` 样本真正接入 tools 运行时代码，而不是继续让 `tools/src/skills/` 停留在 placeholder。
2. 落地 `register_asset()`、`match_intent()`、`revoke_source()`、`list_assets()` 四个内部入口，保证 skill 资产能以 source-scoped 方式被发布、匹配和撤销。
3. 保持 037 的边界清晰：SkillRegistry 只消费 normalized asset，不解析外部方言，不直接执行 workflow，也不代替 ToolRouteSelector / ToolPolicyGate 做执行裁定。

## 2. 实现落点

1. 新增 `tools/src/skills/SkillRegistry.h` 与 `tools/src/skills/SkillRegistry.cpp`：
   - 定义 `SkillSpecAsset`、`SkillMatchQuery`、`SkillMatchResult`、`SkillRegistrySnapshot`；
   - `register_asset()` 负责 source-scoped upsert 与 snapshot publish；
   - `match_intent()` 负责 intent token、tag 和 profile constraint 的 deterministic 匹配；
   - `revoke_source()` 负责按 source 批次撤销 skill 资产；
   - `list_assets()` 暴露稳定的 flattened 资产视图。
2. 更新 `tools/CMakeLists.txt`，把 `src/skills/SkillRegistry.cpp` 纳入 `dasall_tools`，替代原先的 skills placeholder 编译入口。
3. 更新 `tests/unit/tools/CMakeLists.txt`，新增：
   - `dasall_skill_registry_unit_test`
   - `dasall_skill_registry_priority_unit_test`

## 3. 关键设计结论

1. SkillRegistry 的输入固定为 normalized `SkillSpecAsset`；`SKILL.md` frontmatter、plugin bundle 目录或其他 external dialect 仍是 039 的 importer 职责，不在 037 内旁路解析。
2. registry 的 publish 语义对齐 009 的 ToolRegistry：写路径串行、读路径绑定不可变 snapshot，避免调用方拿到悬垂引用。
3. 匹配策略保持 deterministic：先按 intent overlap / exact phrase / tag overlap 评分，再在同分时按 `source_key + skill_id` 做稳定 tie-break；空匹配返回 `skill.match.none`，profile 不满足返回 `skill.match.profile_filtered`。
4. source revoke 不保护 internal source；SkillRegistry 本身只表达 source ownership 事实，是否重载 internal 资产由更上游调用者决定。

## 4. 测试覆盖

1. 新增 `tests/unit/tools/SkillRegistryTest.cpp`：
   - 验证 source-scoped register / upsert / revoke；
   - 验证 flattened list view 与 snapshot revision 更新；
   - 验证 runtime incident intent 可匹配到 internal canonical sample 语义。
2. 新增 `tests/unit/tools/SkillRegistryPriorityTest.cpp`：
   - 验证 specificity 更高的 incident skill 会优先于泛化 runtime skill；
   - 验证 profile mismatch 走 `skill.match.profile_filtered` fail-closed 路径。
3. 通过 `ctest -N` 验证 `SkillRegistryTest` 与 `SkillRegistryPriorityTest` 已被 tools unit discoverability 正式接入。

## 5. 验证

1. 构建：
   - `Build_CMakeTools`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `SkillRegistryTest`, `SkillRegistryPriorityTest`
3. discoverability：
   - `ctest --test-dir build/vscode-linux-ninja -N | rg "SkillRegistry(Test|PriorityTest)"`
4. 结果：
   - `dasall_tools` 与新增 unit targets 构建通过；
   - `SkillRegistryTest`、`SkillRegistryPriorityTest` 全部通过；
   - `ctest -N` 可发现两条新增 tools unit tests；
   - 历史 `DartConfiguration.tcl` 噪声仍存在，但不影响通过结论。

## 6. 对后续任务的影响

1. 038 可以直接复用 `SkillMatchResult` 与 `SkillSpecAsset`，实现 `SkillRuntime::instantiate()` 与 workflow bind，而不需要在 runtime 内临时再建一套 asset catalog。
2. 039 只需把 external dialect 与 plugin bundle 归一化到 `SkillSpecAsset`，即可直接注册到 SkillRegistry，不需要改动 registry 的匹配或 source revoke 语义。
3. 040 的 integration gate 已具备 registry 基座，后续只剩 runtime / importer / plugin unload 黑盒闭环接线。

## 7. 风险与后续

1. 当前匹配规则故意保持轻量，主要服务 deterministic unit/integration gate；若后续要引入更复杂的 ranking，必须保持 stable tie-break 和 fail-closed 语义不变。
2. SkillRegistry 目前不直接读取 `skills/specs/` 文件；036 的 canonical sample 仍由 039 importer 或更上游 loader 归一化后再注入 registry。
3. external importer 继续默认关闭；037 只落内部 registry，不改变 036 冻结的 module-local feature flag 边界。