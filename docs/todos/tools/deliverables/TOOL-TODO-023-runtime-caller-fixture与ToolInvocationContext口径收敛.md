# TOOL-TODO-023 runtime caller fixture 与 ToolInvocationContext 口径收敛

日期：2026-04-16  
任务：TOOL-TODO-023  
状态：D Gate PASS

## 1. 本地证据

1. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TODO-023 要求补齐 `ToolInvocationContext` 的 caller / confirmation / trace / profile 输入口径，并给出 runtime -> tools 的最小 caller fixture 约束。
2. docs/architecture/DASALL_tools子系统详细设计.md 6.5.1 已把 `ToolInvocationContext` 冻结为 module-local public invoke context，而 8.3、11.1 继续把“runtime 尚无稳定 ToolManager caller surface”列为 blocker，说明缺的不是 shared contract，而是 fixture 口径成表。
3. TOOL-TODO-002 已冻结 `ToolInvocationContext` 字段集合，TOOL-TODO-012 已证明 `BuildProfileManifest` 是 ToolConfigAdapter lane enablement 的既有来源，TOOL-TODO-015a 又进一步暴露出它不应被塞进 public context，而应由 ToolManager 本地依赖注入持有。

## 2. Design 结论

1. tools 侧最小 caller fixture 固定为三段式：`ToolRequest`、`ToolInvocationContext`、ToolManager 本地依赖。`ToolInvocationContext` 只承载 invoke-scoped caller/session/profile snapshot/trace/confirmation facts。
2. `BuildProfileManifest`、capability snapshot、route health、executor/projector/audit hooks 都属于 ToolManager fixture / dependency injection 侧输入，不进入 `ToolInvocationContext`，避免把 config/build/execution supporting object 混进 runtime invoke context。
3. `ToolInvocationContext.caller_domain` 明确表示调用来源域，而不是最终执行通道。当前 PolicyGate 所需的 `builtin` / `workflow` / `mcp` requested domain 由 ToolManager 在验证后基于 `ToolIR.route` / descriptor category 映射，作为 module-local `ToolAdmissionRequest` 输入。
4. 该 fixture 只用于 tests/mocks 和 design gate，证明 tools 侧闭环可验证；它不等价于 runtime 生产主链已经稳定接线，因此 runtime production caller 仍保持 out-of-scope / blocked 结论。

## 3. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| ToolInvocationContext caller 口径澄清 | docs/architecture/DASALL_tools子系统详细设计.md 6.5.1 |
| runtime caller fixture 最小口径成表 | docs/architecture/DASALL_tools子系统详细设计.md 6.5.1.1、8.3、11.1 |
| TODO blocker / executable state 回写 | docs/todos/tools/DASALL_tools子系统专项TODO.md |

## 4. Build 三件套

1. 代码目标：无；本任务只回写 architecture/TODO 设计证据，不修改 production 代码。
2. 测试目标：通过文档检索确认 `ToolInvocationContext`、`caller_domain`、`BuildProfileManifest` 与 `TOOL-BLK-002` 的口径已在 architecture/TODO 中一致收敛。
3. 验收命令：
   - `rg -n "ToolInvocationContext|caller_domain|BuildProfileManifest|TOOL-BLK-002|fixture" docs/architecture/DASALL_tools子系统详细设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md`

## 5. 风险与回退

1. 当前 requested execution domain 仍由 ToolManager module-local 映射生成；如果后续 runtime owner 冻结了更正式的 admission/requested-domain supporting object，应迁移到正式上游输入，而不是长期依赖临时映射。
2. 023 只解阻 tools-side fixture 和 design gate，不代表 runtime 生产 caller 已完成；后续若文档把这两者混写，需要立即回退到“tests/design gate only”的表述。