# CLCFG-TODO-005 冻结 ToolSkillPage 的 P0/P1/P2 capability 边界与未决问题处置

状态：Done
日期：2026-05-08
来源 TODO：docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md

## 1. 任务边界

1. 本任务只冻结 `ToolSkillPage` 的 `ToolSkillPageMode` 模式矩阵、P0/P1/P2 能力边界，以及 external importer / plugin activation / operator-facing deployment surface 的未决问题处置。
2. 本任务不提前实现 `ConfigCapabilityResolver::tool_skill_capability()`、`ToolSkillPage` UI、`ToolConfigAdapter` 写路径或任何 tools runtime 代码，只为 CLCFG-TODO-017 与后续 P2 build 提供不可再漂移的设计输入。
3. 本任务必须保持 `config` 只消费 tools 的投影视图，不把 `config` 扩张成 plugin/skill 的 deployment owner。

## 2. 当前冲突与需要收敛的口径

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 在本任务开始前只写了“ToolSkillPage hidden or read-only”的原则，但还没有把 P0/P1/P2 边界、可见模式和开放前提写成可直接交付实现的模式矩阵。
2. `docs/todos/tools/DASALL_tools子系统专项TODO.md` 已完成 `SkillRegistry`、`SkillRuntime`、`ExternalSkillImporter`、`PluginSkillBundleImporter` 与相关 integration，但这些 internal/runtime 能力并不自动等于 CLI `config` 可以提供 editable operator controls。
3. 若不把 `bundle/source/allowlist/profile constraints/revoke path/audit result` 的显示前提和 external importer 的默认关闭策略写成单一结论，后续 `ToolSkillPage` 很容易把 internal runtime 误渲染成“安装默认即可开关”的 operator surface。

## 3. 冻结结论

### 3.1 ToolSkillPageMode 模式矩阵

1. `ToolSkillPageMode` 固定为三态：`hidden`、`summary_only`、`editable`。
2. P0：如果 tools/skills 只具备 internal runtime，或虽有 active capability 但 operator-facing deployment config owner 尚未冻结，则本页默认 `hidden`；只有当系统已检测到 active bundle/plugin/skill capability 时，才允许降级为 `summary_only`。
3. P1：LLM secret onboarding 不扩张 tools surface；本页继续维持 `hidden` 或 `summary_only`，不得出现 bundle/source/importer 开关，也不得暗示“默认安装的 skills/tools 可任意开关”。
4. P2：只有在 tools owner 冻结了 operator-facing deployment config surface、plugin extension bridge 或等价扩展面可稳定探测，并且 `bundle/source/allowlist/profile constraints/revoke path/audit result` 已能稳定投影到 summary 与 action plan 时，本页才允许切到 `editable`。

### 3.2 operator-facing surface 的 owner 边界

1. `config` 只读取 `ToolConfigAdapter`、`ToolRegistry`、`SkillRegistry`、`SkillRuntime` 的投影视图，不直接写 active plugin set，不绕过 `PluginExtensionBridge`、`SkillRegistry` 或 `ToolPolicyGate`。
2. `summary_only` 模式若出现，必须只显示 bundle/source、tool allowlist、profile constraints、revoke / audit 摘要，不出现 enable/disable 或 importer toggle。
3. revoke / disable 路径必须保持 source-scoped、可审计，且仍由 tools owner 链路执行，不由 CLI `config` 直接替代。

### 3.3 未决问题处置

1. external skill importer 继续保持 explicit opt-in / feature flag，不作为默认安装能力，也不因 importer 代码已落盘就自动进入 P0/P1 页面。
2. generic MCP ready 与 runtime production caller adapter 仍属于 tools/runtime 后续事项；它们不构成 P0/P1 开放 editable tools 页面的方法论依据。
3. 已存在的 plugin / skill runtime integration 证据只能证明 internal/runtime 链路可用，不能被解释为 operator-facing deployment owner 已冻结。

## 4. 对后续 Build 的直接约束

1. CLCFG-TODO-017 的任意实现都必须先由 `ConfigCapabilityResolver::tool_skill_capability()` 判定 `hidden` / `summary_only` / `editable`，再决定页面是否展示交互控件。
2. P0/P1 build 若展示 ToolSkillPage，只能展示 summary-only 只读摘要，不能在 UI 中出现 enable/disable toggle、importer 默认开关或未经 tools owner 定义的第二套 allowlist 写入面。
3. P2 build 若开放编辑，必须同时覆盖 bundle/source/allowlist/profile constraints/revoke path/audit result 的显示与 source-scoped revoke 行为，且 external importer 仍需额外 feature flag / consent。
4. focused tests 应围绕 page mode gating、summary-only 只读约束、P2 editable 前提和 external importer default-off 行为编写，而不是围绕“internal runtime 已存在，所以 UI 一定可编辑”的假设编写。

## 5. 验证口径

1. 设计验收使用以下命令：

   `rg -n "ToolSkillPage|capability-gated|allowlist|PluginSkillBundleImporter|SkillRegistry|SkillRuntime" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-005-toolskill-capability边界冻结.md`

2. 通过标准：
   - `ToolSkillPageMode`、P0/P1/P2 边界与 editable 前提在主设计、tools owner TODO 与 deliverable 中形成唯一口径。
   - P0/P1 不再把 internal runtime / plugin activation 伪装成 editable operator surface。
   - external importer default-off、source-scoped revoke 和 capability-gated 约束都已写成后续实现必须遵守的固定前提。