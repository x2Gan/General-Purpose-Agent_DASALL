# LLM-TODO-013 PromptAssetRepository 与 baseline Prompt 资产设计收敛

日期：2026-04-11
任务：LLM-TODO-013
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.6.1 已冻结 Prompt 包形态为 `manifest.yaml + system.md + task.md + few_shots/*.md + policy_notes.md`，并要求运行时消费的是由 PromptAssetRepository 解析后的结构化资产，而不是编译进二进制的字符串常量。
2. 同一设计文档的 6.6.2 明确了 Prompt 资产装载顺序必须为 baseline → deployment override → trusted runtime snapshot，并规定 overlay 校验失败时必须保留上一份 valid catalog，不得发布半成品状态。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.15.5 已把 PromptAssetRepository 的 owner 边界收敛为“资产装载 owner，不是运行态选择 owner”，因此 013 只负责 manifest 解析、Markdown 正文加载、content hash/source 校验和 immutable catalog 发布，不提前进入 PromptRegistry 选择逻辑。
4. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 013 的 Build 三件套冻结为 `PromptAssetRepository.*`、baseline Prompt 样例资产、`PromptAssetPackageParseTest` 与 `PromptSourceOverlayTest`，并以显式 `cmake/ctest` 命令作为本轮验收出口。
5. 本轮实现已落地 [llm/src/prompt/PromptAssetDescriptor.h](../../../../llm/src/prompt/PromptAssetDescriptor.h)、[llm/src/prompt/PromptAssetRepository.h](../../../../llm/src/prompt/PromptAssetRepository.h)、[llm/src/prompt/PromptAssetRepository.cpp](../../../../llm/src/prompt/PromptAssetRepository.cpp)、[llm/assets/prompts/planner/default/manifest.yaml](../../../../llm/assets/prompts/planner/default/manifest.yaml)、[llm/assets/prompts/planner/default/system.md](../../../../llm/assets/prompts/planner/default/system.md)、[llm/assets/prompts/planner/default/task.md](../../../../llm/assets/prompts/planner/default/task.md)，并补齐 [tests/unit/llm/PromptAssetPackageParseTest.cpp](../../../../tests/unit/llm/PromptAssetPackageParseTest.cpp) 与 [tests/unit/llm/PromptSourceOverlayTest.cpp](../../../../tests/unit/llm/PromptSourceOverlayTest.cpp)。

## 2. 外部参考

1. Langfuse 的 Prompt Version Control 把 prompt 管理收敛为“版本 + 标签 + 回滚”的外部资产体系，而不是将 prompt 文本固化进应用代码。013 采用外部目录包与 stable version 元数据，和这类行业实践一致。参考：https://langfuse.com/docs/prompt-management/features/prompt-version-control
2. C++ Core Guidelines 的 C.2 / C.8 强调值对象优先使用简单 `struct` 表达显式数据不变量。013 将 `PromptAssetDescriptor` 与 `PromptCatalog` 维持为简单聚合对象，并通过 `has_consistent_values()` 提供显式边界检查。参考：https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines

## 3. Design 结论

1. Prompt 资产必须保持外置目录包形态；本轮基线样例以 `llm/assets/prompts/planner/default/` 落盘，并通过 `manifest.yaml` 提供 shared `PromptSpec` / `PromptRelease` 的稳定映射字段，通过 `system.md` / `task.md` 承载作者态正文。
2. `PromptAssetDescriptor` 负责承接 shared contracts 之外的 module-local 元数据，包括 `package_id`、`schema_version`、`min_loader_version`、`source_layer`、`source_uri`、`content_hash`、`scene_id`、`persona_id`、`profile_tags` 和 `is_default_release`。这保证 scene/persona/profile 选择信息继续留在 llm 内部，不反向污染 shared prompt contracts。
3. `PromptAssetRepository` 只做三件事：按 `PromptAssetSourceConfig` 顺序装载三层资产、解析 manifest/Markdown 构建 immutable `PromptCatalog`、在 reload 失败时保留上一份 valid snapshot。它不做 stage/task/language/model_family 的运行态选择，也不负责消息装配。
4. 本轮的 content hash 由 manifest 与正文文件内容组合后计算得到，用于稳定识别“同一 release 内容是否发生变化”；这满足 013 对 hash/source 校验的最小实现要求，同时为后续 trusted snapshot 审计留出入口。
5. overlay 采用“同一 `prompt_id + version` 被高优先级层完整替换”的规则：snapshot 覆盖 deployment，deployment 覆盖 baseline。若高优先级层的包损坏、缺失必填字段或 `schema_version/min_loader_version` 不兼容，则整轮 reload 失败并继续保留上一份已发布 catalog。
6. baseline Prompt 样例使用 `trusted_source: profiles`、`stage: planning`、`eval_status: stable`，并保留 `scene_id` / `persona_id` / `profile_tags` 等 module-local 选择信息，为后续 015 的 PromptRegistry 选择逻辑提供最小可消费目录。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 Prompt 资产 module-local 描述符 | [llm/src/prompt/PromptAssetDescriptor.h](../../../../llm/src/prompt/PromptAssetDescriptor.h) |
| 实现 Prompt 资产仓储、overlay 与 snapshot 发布 | [llm/src/prompt/PromptAssetRepository.h](../../../../llm/src/prompt/PromptAssetRepository.h)、[llm/src/prompt/PromptAssetRepository.cpp](../../../../llm/src/prompt/PromptAssetRepository.cpp) |
| 补齐内部 YAML key/list 解析支撑 | [llm/src/asset/KeyValueYamlParser.h](../../../../llm/src/asset/KeyValueYamlParser.h) |
| 提供 baseline Prompt 资产样例 | [llm/assets/prompts/planner/default/manifest.yaml](../../../../llm/assets/prompts/planner/default/manifest.yaml)、[llm/assets/prompts/planner/default/system.md](../../../../llm/assets/prompts/planner/default/system.md)、[llm/assets/prompts/planner/default/task.md](../../../../llm/assets/prompts/planner/default/task.md) |
| 补齐 Prompt 包解析与 overlay 回归测试 | [tests/unit/llm/PromptAssetPackageParseTest.cpp](../../../../tests/unit/llm/PromptAssetPackageParseTest.cpp)、[tests/unit/llm/PromptSourceOverlayTest.cpp](../../../../tests/unit/llm/PromptSourceOverlayTest.cpp) |
| 将新实现接入 llm / unit 构建图 | [llm/CMakeLists.txt](../../../../llm/CMakeLists.txt)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |

## 5. Build 三件套

1. 代码目标：实现 `PromptAssetRepository`、内部 `PromptAssetDescriptor` 与 baseline Prompt 资产样例，使 llm 首次具备可解析、可 overlay、可回退的 Prompt 资产目录。
2. 测试目标：`PromptAssetPackageParseTest` 覆盖 manifest 解析、Markdown 正文加载、content hash 更新与缺失字段拒绝；`PromptSourceOverlayTest` 覆盖 baseline/deployment/snapshot 三层优先级和坏包回退。
3. 验收动作：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R PromptAssetPackageParseTest --output-on-failure`
   - `ctest --test-dir build-ci -R PromptSourceOverlayTest --output-on-failure`

## 6. 风险与回退

1. 本轮只支持 key/list 形式的最小 manifest 解析，不承担模板引擎、安全渲染或复杂外部 snapshot 签名校验；这些能力继续留给 039、017 和后续 trusted snapshot 治理任务。
2. `PromptCatalog` 当前只按 `prompt_id + version` 做 overlay 替换，不做 scene/persona/profile 的运行态选择；若后续 015 需要更多索引辅助，也应在 Registry 侧消费 descriptor 元数据，而不是把选择逻辑塞回 Repository。
3. `few_shot_refs` 与 `policy_notes.md` 在本轮只做存在性与基础读取，不做装配期渲染。若后续 Composer 需要读取 few-shot 正文，应由 017 在不越过 PromptAssetRepository owner 边界的前提下补消费逻辑。