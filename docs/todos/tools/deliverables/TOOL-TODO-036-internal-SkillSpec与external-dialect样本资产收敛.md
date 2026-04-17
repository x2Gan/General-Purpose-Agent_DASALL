# TOOL-TODO-036 internal SkillSpec 与 external dialect 样本资产收敛

日期：2026-04-17  
任务：TOOL-TODO-036  
状态：已完成

## 1. 本地证据

1. `docs/architecture/DASALL_tools子系统详细设计.md` 6.12.5 已冻结 `SkillSpecAsset`、`SkillRegistry`、`SkillRuntime`、`ExternalSkillImporter` 的职责边界，但在本轮开始前工作区没有 `skills/specs/`、`skills/workflows/`、`skills/evals/` 等真实资产目录。
2. `tools/src/skills/placeholder.cpp` 仍是占位实现，说明 037~040 只能先依赖 deterministic 资产样本推进，而不能假设已有 parser / registry / runtime。
3. 仓内 `.github/skills/project-implementation-cycle/SKILL.md` 与 `.github/skills/git-task-submit/SKILL.md` 已证明当前仓库管理的外部 skill 入口是 `SKILL.md + YAML frontmatter`，可作为 GitHub-style sample 的本地方言证据。
4. `llm/src/asset/KeyValueYamlParser.h` 已提供 key-value + list 风格的轻量 YAML 解析器，因此 internal normalized sample 应优先收敛为该解析面可稳定消费的字段形态，而不是提前引入复杂嵌套 schema。

## 2. 外部参考

1. Claude Code 官方 skills 文档 `https://code.claude.com/docs/en/skills` 明确要求 skill 目录以 `SKILL.md` 作为入口，并通过 YAML frontmatter + supporting files 组织技能说明、引用文件和工具权限。
2. 同一官方文档明确 skill 可放在 `.claude/skills/<skill-name>/SKILL.md`，并允许目录内附带 `reference.md`、`examples.md`、`scripts/` 等 supporting files；这为 DASALL 冻结 Claude-style external dialect fixture 的目录形态提供了直接依据。

## 3. 主结论

1. internal normalized 资产根固定为 `skills/specs/`、`skills/workflows/`、`skills/evals/`，并新增 canonical sample：
   - `skills/specs/runtime-incident-triage.skill.yaml`
   - `skills/workflows/runtime-incident-triage.workflow.yaml`
   - `skills/evals/runtime-incident-triage.eval.yaml`
2. external dialect fixture 固定为：
   - `skills/external_dialects/claude/runtime-incident/`
   - `skills/external_dialects/github/runtime-incident/`
   两者都以 `SKILL.md` 为入口，并通过同目录 `workflow.yaml`、`eval.yaml`、`reference.md` 提供 supporting refs。
3. external importer 的开关在 v1 保持 module-local `external_skill_import_enabled`，默认关闭；本轮不新增 profile YAML、`RuntimePolicySnapshot` 或 `BuildProfileManifest` schema。
4. GitHub-style sample 只冻结仓内 `SKILL.md` 风格与 importer-local sample keys，不承诺兼容任意外部 GitHub skill 方言；Claude-style sample 对齐官方目录布局，但其中 `workflow`、`eval_suite` 仅作为 DASALL importer fixture 扩展键。
5. 036 的交付目标是解开 `TOOL-BLK-005`，为 037~040 提供 deterministic input assets，而不是把 external importer 或 plugin skill bundle 写成当前已 ready 的产品事实。

## 4. Design -> Build 映射

| 设计项 | Build 落点 | 验收出口 |
|---|---|---|
| internal normalized `SkillSpecAsset` sample | `tools/src/skills/SkillRegistry.cpp`、`SkillRuntime.cpp` | `SkillRegistryTest.cpp`、`ToolSkillRuntimeIntegrationTest.cpp` |
| external Claude-style sample | `tools/src/skills/ExternalSkillImporter.cpp` | `ExternalSkillImporterTest.cpp`、`ToolPluginSkillBundleIntegrationTest.cpp` |
| external GitHub-style sample | `tools/src/skills/ExternalSkillImporter.cpp` | `ExternalSkillImporterTest.cpp`、`ToolPluginSkillBundleIntegrationTest.cpp` |
| module-local importer feature flag | `ExternalSkillImporter` / integration fixtures | `ExternalSkillImporterTest.cpp`、`ToolPluginSkillBundleIntegrationTest.cpp` |

## 5. D Gate 结果

1. Design Gate：PASS。
2. 进入 037~040 的条件已满足：
   - internal / external 样本目录已落盘；
   - sample shape 与 feature flag 边界已在 architecture 中冻结；
   - GitHub-style / Claude-style 两类 external dialect fixture 已具备可追溯入口；
   - 未新增 profile schema，也未把 importer 兼容误写为产品承诺。

## 6. 验证

1. 文档一致性：
   - `rg -n "SkillSpecAsset|ExternalSkillImporter|skills/specs|ToolSkillRuntimeIntegrationTest|ToolPluginSkillBundleIntegrationTest" docs/architecture/DASALL_tools子系统详细设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md`
2. 资产落盘：
   - `test -d skills/specs && test -d skills/workflows && test -d skills/evals`
   - `test -f skills/specs/runtime-incident-triage.skill.yaml`
   - `test -f skills/external_dialects/claude/runtime-incident/SKILL.md`
   - `test -f skills/external_dialects/github/runtime-incident/SKILL.md`

## 7. 风险与后续

1. 当前 external dialect sample 仍是 importer fixture，不是完整公开兼容声明；039 必须把 unknown field、missing asset 和 feature flag 关闭场景显式 quarantine。
2. internal sample 目前刻意收敛为轻量 YAML 解析面；若 038 需要更复杂 workflow schema，应先通过 module-local parser 扩展，而不是回头扩 shared contract。
3. plugin skill bundle integration 仍待 040 验证 source-scoped revoke；036 只负责冻结资产形状，不直接替代运行时验证。