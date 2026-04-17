# TOOL-TODO-039 ExternalSkillImporter 与 PluginSkillBundleImporter 收敛

日期：2026-04-17  
任务：TOOL-TODO-039  
状态：已完成

## 1. 目标

1. 把 036 冻结的 external dialect fixture 与 plugin skill bundle 入口真正接入 tools runtime，使 skill 资产不再只停留在 checked-in sample，而能被 importer 归一化为 `SkillSpecAsset`。
2. 落地 `import_directory()`、`parse_frontmatter()`、`normalize_assets()`、`emit_diagnostics()`，并补齐 plugin bundle 对 normalized internal asset 与 external dialect 的统一导入口。
3. 保持 039 的边界清晰：importer 只做解析、归一化和 quarantine，不直接注册到 registry、不执行 workflow，也不把 feature flag 推进到 profile schema。

## 2. 实现落点

1. 新增 `tools/src/skills/SkillImportSupport.h` 与 `tools/src/skills/SkillImportSupport.cpp`：
   - 提供轻量 YAML/key-value 解析、路径解析、asset ref 归一化、identifier/token 工具；
   - 只服务 039 的 module-local importer，不扩 shared contracts。
2. 新增 `tools/src/skills/ExternalSkillImporter.h` 与 `tools/src/skills/ExternalSkillImporter.cpp`：
   - 定义 `SkillImportDiagnostic`、`ParsedSkillFrontmatter`、`SkillImporterOptions`、`SkillImportResult`；
   - `import_directory()` 在 feature flag 打开时扫描 `SKILL.md` 入口并归一化外部 skill；
   - `parse_frontmatter()` 解析 `SKILL.md` YAML frontmatter；
   - `normalize_assets()` 把 GitHub-style / Claude-style sample 映射到内部 `SkillSpecAsset`；
   - `emit_diagnostics()` 输出 deterministic diagnostics 顺序。
3. 新增 `tools/src/skills/PluginSkillBundleImporter.h` 与 `tools/src/skills/PluginSkillBundleImporter.cpp`：
   - 对 normalized internal bundle 读取 `.skill.yaml`；
   - 对 external dialect bundle 委托 `ExternalSkillImporter`；
   - 保持 plugin `source_key` 透传，并把 bundle root 下的错误输入 quarantine。
4. 更新 `tools/CMakeLists.txt`，把三份 039 新增源文件接入 `dasall_tools`。
5. 新增 `tests/unit/tools/ExternalSkillImporterTest.cpp` 与 `tests/unit/tools/PluginSkillBundleImporterTest.cpp`，并更新 `tests/unit/tools/CMakeLists.txt` 注册新 unit targets。

## 3. 关键设计结论

1. external importer feature flag 仍保持 module-local `external_skill_import_enabled`，默认关闭；039 不新增 `RuntimePolicySnapshot`、`BuildProfileManifest` 或 profile YAML schema。
2. GitHub-style 与 Claude-style sample 都先通过 `SKILL.md` frontmatter + supporting workflow/eval fixture 归一化，再变成 `SkillSpecAsset`；SkillRegistry 继续只接受 normalized asset。
3. quarantine 语义保持 fail-closed：frontmatter 缺失/格式错误、workflow/eval 文件缺失、normalized asset 不完整都只输出 diagnostics，不隐式兼容导入。
4. `PluginSkillBundleImporter` 只解决“plugin bundle -> normalized asset”这一层：内部 bundle 读取 `.skill.yaml`，外部方言 bundle 委托 `ExternalSkillImporter`；真正 register/revoke 仍留给 040 的 integration 闭环验证。

## 4. 测试覆盖

1. 新增 `tests/unit/tools/ExternalSkillImporterTest.cpp`：
   - 验证 feature flag 关闭时拒绝导入；
   - 验证 GitHub-style sample 与 Claude-style sample 都能归一化成 `SkillSpecAsset`；
   - 验证 malformed frontmatter 与 missing workflow 会走 quarantine 诊断。
2. 新增 `tests/unit/tools/PluginSkillBundleImporterTest.cpp`：
   - 验证 normalized internal bundle 可导入 canonical `.skill.yaml`；
   - 验证 external dialect bundle 会委托 `ExternalSkillImporter` 并保留 plugin `source_key`。
3. 复跑 `SkillRegistryTest`、`SkillRegistryPriorityTest`、`SkillRuntimeInstantiateTest`，确认 039 没有破坏 037/038 的 runtime 基线。
4. 通过 `ListTests_CMakeTools` 验证 `ExternalSkillImporterTest`、`PluginSkillBundleImporterTest` 已被 tools unit discoverability 接入。

## 5. 验证

1. 构建：
   - `Build_CMakeTools`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `ExternalSkillImporterTest`, `PluginSkillBundleImporterTest`, `SkillRegistryTest`, `SkillRegistryPriorityTest`, `SkillRuntimeInstantiateTest`
3. discoverability：
   - `ListTests_CMakeTools`
4. 结果：
   - `dasall_tools` 与新增 unit targets 构建通过；
   - `ExternalSkillImporterTest`、`PluginSkillBundleImporterTest`、`SkillRegistryTest`、`SkillRegistryPriorityTest`、`SkillRuntimeInstantiateTest` 全部通过；
   - `ListTests_CMakeTools` 可发现两条新增 importer unit tests；
   - 历史 `DartConfiguration.tcl` 噪声仍存在，但不影响通过结论。

## 6. 对后续任务的影响

1. 040 现在已经具备 `SkillRegistry`、`SkillRuntime`、`ExternalSkillImporter`、`PluginSkillBundleImporter` 四块运行时基座，剩余工作收敛为 integration 黑盒接线与 source-scoped revoke 验证。
2. plugin skill bundle 现在能统一收敛到 `SkillSpecAsset`，后续 integration 只需要验证 load/unload、feature flag 与 registry/runtime 的协同，不需要再补 importer 解析逻辑。
3. external dialect 仍在 feature flag 后，避免把 039 的 sample fixture 误写成产品级兼容承诺。

## 7. 风险与后续

1. 当前 importer parser 故意保持轻量，只覆盖 036 冻结的 sample key 子集；若后续要支持更多 frontmatter 字段，应继续在 module-local parser 层演进，而不是扩 shared contracts。
2. `PluginSkillBundleImporter` 目前只覆盖 normalized internal bundle 与 external dialect bundle 两条最小路径；更复杂的 plugin 资产布局由 040 integration 先验证 source lifecycle，再决定是否扩展。
3. `DartConfiguration.tcl` 噪声仍是 CMake Tools 的历史问题，当前不影响 unit 通过结论，但 040 做 integration gate 时仍需继续注明。