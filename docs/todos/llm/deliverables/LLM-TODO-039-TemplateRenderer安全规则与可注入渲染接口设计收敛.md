# LLM-TODO-039 TemplateRenderer 安全规则与可注入渲染接口设计收敛

日期：2026-04-11
任务：LLM-TODO-039
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.1a 已冻结 v1 模板语法为 `simple_var`，只允许 `{{variable_name}}`，变量名限定为 `[a-zA-Z0-9_]` 且长度不超过 64 字符，并明确禁止嵌套渲染、代码执行、网络请求和文件系统读取。
2. 同一设计文档还要求：未匹配变量保留原文并生成 warning；变量值中的 `{{` / `}}` 必须转义为字面文本；单个变量值渲染后长度默认不得超过 100K 字符；渲染器必须抽象为可注入接口，便于后续 PromptComposer mock。
3. [llm/include/prompt/IPromptComposer.h](../../../../llm/include/prompt/IPromptComposer.h) 与 [llm/include/prompt/PromptComposerConfig.h](../../../../llm/include/prompt/PromptComposerConfig.h) 已在 008 冻结 PromptComposer 只消费 shared compose contracts 与 `template_engine` 配置，因此 039 只需要提供 module-local 渲染器接口，不应回改 shared `PromptComposeRequest` / `PromptComposeResult`。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 039 的完成判定冻结为“渲染过程只执行一轮替换、不支持代码执行或外部读取，且特殊字符转义行为可断言”，验收出口固定为 `TemplateRendererTest`。
5. 旧指引 [docs/todos/llm/DASALL_llm子系统TODO落地实施步骤指引.md](../DASALL_llm子系统TODO落地实施步骤指引.md) 也把 039 收敛为“仅变量替换，不引入外部模板库”，并要求覆盖正常替换、未定义变量处理、注入攻击向量与转义规则。
6. 039 最终落地了 [llm/src/prompt/TemplateRenderer.h](../../../../llm/src/prompt/TemplateRenderer.h)、[llm/src/prompt/TemplateRenderer.cpp](../../../../llm/src/prompt/TemplateRenderer.cpp)、[tests/unit/llm/TemplateRendererTest.cpp](../../../../tests/unit/llm/TemplateRendererTest.cpp)，并更新了 [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt)。

## 2. 外部参考

1. Mustache 手册将 Mustache 定义为 “logic-less templates”，其变量语法同样使用 `{{name}}`，而更强的 section、partial、lambda 等能力会显著扩大模板解释面。039 参考这一经验，但刻意只保留最小的变量替换子集，不实现 section、partial、lambda、delimiter 切换等扩展，以控制 v1 的安全与审计成本。参考：https://mustache.github.io/mustache.5.html
2. OWASP Injection Prevention Cheat Sheet 强调解释器类场景应优先采用 allow-list 输入校验，并在必须拼接用户数据时做上下文转义。039 直接据此把变量名限定为白名单字符集，并对值中的 `{{` / `}}` 做字面化转义，从而避免把未验证输入重新送回模板解释路径。参考：https://cheatsheetseries.owasp.org/cheatsheets/Injection_Prevention_Cheat_Sheet.html

## 3. Design 结论

1. 039 在 [llm/src/prompt/TemplateRenderer.h](../../../../llm/src/prompt/TemplateRenderer.h) 内定义 module-local 的 `ITemplateRenderer`、`TemplateRendererConfig`、`TemplateRenderResult` 和 `TemplateVariables`，作为未来 PromptComposer 的内部依赖注入点，不改 shared contracts，也不扩张 llm public include 面。
2. `TemplateRendererConfig` 只冻结两个初始化输入：`template_engine = simple_var` 与 `max_variable_length = 100K`。如果传入未知引擎或零长度上限，则 `init()` 直接返回 `false`，避免在运行时默默降级到未审计行为。
3. 渲染算法固定为单轮扫描替换：只识别 `{{variable_name}}`，变量名必须满足白名单与长度限制；未匹配变量保留占位原文，并生成 `unmatched_variable:<name>` warning。
4. 对值中的模板分隔符，039 采用反斜杠字面化策略：`{{` 转为 `\{\{`，`}}` 转为 `\}\}`。这既满足“特殊字符转义可断言”，又能保证后续不会再次形成可被同一 simple_var 解析器匹配的双大括号片段。
5. 为了符合设计中的“100K 字符”约束，039 的长度限制按 UTF-8 码点计数而不是按字节截断；超长值会在合法码点边界截断，并生成 `value_truncated:<name>` warning。
6. 嵌套渲染拒绝不是把整个 render 失败，而是通过 `nested_render_rejected` 标志和 `nested_render_rejected:<name>` warning 记录治理事实，同时输出已经字面化后的文本；这样 017 可以直接把 warning 接到 `PromptComposeResult.composition_warnings`，而不需要重新设计错误面。
7. 039 同时保留了对不受支持模板标签的 fail-closed 处理：不合法标签保持原文并生成 `unsupported_template_tag:<tag>` warning，不解释、不执行、不展开。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落地可注入的模板渲染内部接口与结果对象 | [llm/src/prompt/TemplateRenderer.h](../../../../llm/src/prompt/TemplateRenderer.h) |
| 实现 simple_var 单轮替换、字面化转义与 UTF-8 截断 | [llm/src/prompt/TemplateRenderer.cpp](../../../../llm/src/prompt/TemplateRenderer.cpp) |
| 覆盖正常替换、缺失变量、嵌套拒绝、长度截断与特殊字符转义 | [tests/unit/llm/TemplateRendererTest.cpp](../../../../tests/unit/llm/TemplateRendererTest.cpp) |
| 将渲染器实现和单测接入 llm / unit 聚合目标 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `TemplateRenderer` 的安全 simple_var 渲染、warning 产出、嵌套拒绝与可注入接口。
2. 测试目标：`TemplateRendererTest` 覆盖正常替换、未匹配变量 warning、嵌套渲染拒绝、超长值截断和特殊字符转义五类行为。
3. 验收动作：
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `RunCtest_CMakeTools` 运行 `TemplateRendererTest`

## 6. 风险与回退

1. 当前字面化策略使用反斜杠转义，适合 plain-text Prompt 资产，但不试图兼容 HTML/JSON/Markdown 的上下文专用 escaping；若未来 017 发现具体输出上下文需要不同展示策略，应保持 `ITemplateRenderer` 接口稳定，仅替换内部 escape policy。
2. 039 仍只支持 `simple_var`，不支持 dotted name、section、partial、lambda 和 delimiter 切换；这不是能力缺陷，而是 v1 的刻意收敛。若后续确需引入更强模板能力，应走新的安全评审任务，而不是在 017 中偷偷扩语法。