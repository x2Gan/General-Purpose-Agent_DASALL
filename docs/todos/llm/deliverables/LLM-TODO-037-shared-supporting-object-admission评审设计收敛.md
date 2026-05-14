# LLM-TODO-037 shared supporting object admission 评审设计收敛

## 1. 任务边界

1. 本轮只评审 shared ModelRoute、shared PromptPolicyDecision、shared StreamHandle 是否进入 contracts admission，不新增 shared 头文件，不移动 llm module-local 头文件。
2. 评审对象继续对应现有 module-local 文件：`llm/include/route/ResolvedModelRoute.h`、`llm/include/prompt/PromptPolicyDecision.h`、`llm/include/stream/StreamSessionRef.h`。
3. LLM-TODO-036 已冻结 streaming lifecycle 设计并保持实现后置，因此 037 不再以“生命周期语义未知”为 blocker；037 的 owner 是跨模块消费者矩阵、兼容窗口、contract gate 与 Go/No-Go 结论。
4. 当前 shared contract 锚点保持为 `contracts/include/llm/LLMRequest.h`、`contracts/include/llm/LLMResponse.h`、`contracts/include/prompt/PromptComposeRequest.h`、`contracts/include/prompt/PromptComposeResult.h`。

## 2. 本地证据

1. `ResolvedModelRoute` 的真实生产 owner 是 `llm/src/route/ModelRouter.cpp`，直接消费者是 `llm/src/LLMManager.cpp` 与 llm unit tests；未发现 runtime、cognition、apps、tools、services 直接 include `route/ResolvedModelRoute.h`。
2. `PromptPolicyDecision` 的真实生产 owner 是 `llm/src/prompt/PromptPolicy.cpp`，直接消费者是 `PromptPipeline`、`LLMManagerResult` 与 llm tests；runtime / cognition 消费的是 `LLMManagerResult` 中的 `PromptPolicyDisposition` 摘要，而不是完整 `PromptPolicyDecision` 对象。
3. `StreamSessionRef` 当前只被 adapter skeleton、mock、surface/lifecycle tests 和 streaming SPI 占位使用；`LLMManager::stream_generate()` 仍 fail-closed，未形成 runtime/cognition/apps 侧稳定 stream handle consumer。
4. 精确 include 搜索只发现 llm 内部与 tests 直接 include 三个 module-local 头文件；未发现跨模块把这些头文件当作 shared contracts 使用。
5. contracts 侧现有 `LLMRequest / LLMResponse / PromptComposeRequest / PromptComposeResult` contract tests 已覆盖当前 shared handoff，不依赖三个候选 supporting object。

## 3. 外部参考

1. Google AIP-180 Backwards compatibility 将兼容性分为 source / wire / semantic 三类，并强调旧客户端在同一 major version 下必须继续编译、运行并保持可预期语义；已有组件不能在同一版本内被移除、重命名或跨文件移动，因为这会破坏 include/import 与生成代码路径。
2. Protocol Buffers proto3 updating guidance 说明字段编号一旦投入使用不能更改或复用；删除字段必须 reserve；即便 wire-safe 的新增字段，也可能因默认值、枚举新值、oneof 等在应用代码层面形成 breakage。
3. 对 DASALL C++ shared contracts 的对应启发是：shared admission 不应只看“对象字段很小、迁移容易”，还必须证明消费者、默认行为、语义解释、头文件路径、contract test 与迁移窗口已经稳定。

## 4. 消费者矩阵

| 候选 shared object | 当前真实 owner | 当前真实消费者 | 跨模块消费者证据 | shared 替代锚点 | 结论 |
|---|---|---|---|---|---|
| shared ModelRoute / `ResolvedModelRoute` | `ModelRouter` | `LLMManager` route candidate 展开、ModelRouter unit tests、InterfaceSurfaceTest | 未发现 runtime/cognition/apps/tools/services 直接消费；外部只需要最终 `LLMManagerResult.resolved_route` 字符串和 attempted route trace | `LLMRequest.model_route` 保持 provider-neutral handoff 字符串；`LLMManagerResult.resolved_route` 仍是 llm module-local result 字段 | No-Go：当前不升格 shared |
| shared PromptPolicyDecision | `PromptPolicy` | `PromptPipeline`、`LLMManagerResult`、PromptPolicy / Pipeline / Manager tests | runtime/cognition 通过 `LLMManagerResult` 间接看到 `PromptPolicyDisposition` 摘要；未直接消费完整治理对象、redactions、tool visibility patch 或 governed messages | `PromptComposeResult` 继续表达装配产物；`LLMManagerResult.governance_disposition` 继续向 Runtime 暴露最小回流信号 | No-Go：完整对象不升格；disposition 摘要继续 module-local |
| shared StreamHandle / `StreamSessionRef` | adapter skeleton / future stream registry | OpenAI-compatible、Ollama、Local adapter placeholder，MockLLMAdapter，StreamSessionLifecycleTest | `stream_generate()` 当前 fail-closed；未发现 runtime/cognition/apps/tools/services 侧稳定 stream session owner 或 cancel consumer | `LLMRequest.request_mode = Streaming` 仍只是 shared request mode；真实 lifecycle handle 后置 | No-Go：当前不升格 shared |

## 5. Go/No-Go 评审结论

1. `shared ModelRoute`：No-Go。当前跨模块只需要 request-side route hint 与 result-side resolved route 字符串；`ResolvedModelRoute` 仍携带 ModelRouter 内部 fallback / streaming flag 解释，不具备 shared ABI 稳定性。
2. `shared PromptPolicyDecision`：No-Go。完整治理对象包含 governed messages、redactions、tool_visibility_patch 与 reason，仍是 PromptPipeline 内部治理产物；Runtime 只需要 `governance_disposition + safe_to_replan` 的最小回流信号。
3. `shared StreamHandle`：No-Go。036 已冻结生命周期设计，但真实 registry、observer terminal callback、transport cancel、overflow reporting 与 integration smoke 都尚未落地，不能把 placeholder `StreamSessionRef` 误升格为 shared handle。
4. 037 的完成定义是“完成 admission 评审并给出清晰 No-Go”，不是“允许写 contracts 头文件”。`LLM-BLK-006` 因评审结论、消费者矩阵与兼容窗口已形成而解阻。

## 6. 未来迁移窗口

1. Phase 0：保持当前状态，三个候选对象继续 module-local；contract gate 只验证现有 shared handoff 不回退。
2. Phase 1：若出现两个以上非测试子系统直接需要同一对象，先新增 contracts 设计交付物和 consumer matrix，不移动既有 llm 头文件。
3. Phase 2：在 contracts 新对象通过 review 后，采用 side-by-side 适配窗口：llm 继续导出 module-local 对象，同时提供显式投影函数或 adapter，把新 shared 对象与旧 module-local 对象双向转换。
4. Phase 3：至少经过两个连续 Gate 轮次且 runtime / cognition / access 等真实消费者完成迁移后，才能 deprecate module-local 对象；删除或重命名必须另起 breaking review。
5. 对 streaming，Phase 1 之前还必须先完成真实 `StreamSessionRegistry`、cancel/overflow/terminal callback tests、以及至少一个 stream integration smoke。

## 7. 风险清单

1. 过早升格 `ResolvedModelRoute` 会冻结当前 ModelRouter 的 fallback ordering 与 streaming flag，后续 profile / provider catalog 策略变化会被 shared ABI 反向约束。
2. 过早升格 `PromptPolicyDecision` 会把 governed messages 与 redaction/tool patch 细节暴露给 Runtime 或 Cognition，容易让 PromptPolicy 被误当作 Tool Policy Gate 或 ContextOrchestrator。
3. 过早升格 `StreamSessionRef` 会把 placeholder session id 误解为可取消、可观察、可恢复的 stream handle，绕过 036 的 fail-closed 结论。
4. 若未来迁移直接移动头文件路径，会破坏 C++ include source compatibility；必须先 side-by-side，而不是 rename/move。
5. 若只新增对象而没有 contract tests，旧消费者仍可能依赖默认值、字段缺省或字符串格式，从而形成 semantic break。

## 8. Design -> Build 映射

| Design 结论 | Build 动作 |
|---|---|
| 三个候选对象本轮 No-Go shared | 不新增 `contracts/include/**` 头文件，不移动 llm 头文件 |
| 形成消费者矩阵 | 回写本交付物、详细设计 7.2.2、专项 TODO 17.23 与 worklog |
| 固定迁移窗口 | 在本交付物中记录 Phase 0~3，不在当前提交中执行迁移 |
| contract gate 不回退 | 构建 `dasall_contract_tests` 并运行 `ctest -L contract` |

## 9. Build 三件套

1. 代码目标：无新增 shared 头文件；继续保持三个 supporting objects 在 llm module-local 路径。
2. 测试目标：现有 `LLMRequestResponseContractTest`、`PromptComposeRequestContractTest`、`PromptComposeRequestFieldContractTest`、`PromptComposeResultContractTest`、`PromptComposeResultFieldContractTest` 与全量 contract label 不回退。
3. 验收命令：`cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract`。
4. 验收结果：`Build_CMakeTools(buildTargets=["dasall_contract_tests"])` 成功，实际执行 `/usr/bin/ctest --output-on-failure -L contract`，157/157 contract tests 通过。`RunCtest_CMakeTools` 定向执行 `LLMRequestResponseContractTest`、`PromptComposeRequestContractTest`、`PromptComposeRequestFieldContractTest`、`PromptComposeResultContractTest`、`PromptComposeResultFieldContractTest`，5 个 shared handoff 锚点全部通过。

## 10. 合规复核

1. ADR-006：037 不把 PromptPolicyDecision 或 redaction/tool patch 变成 ContextPacket owner，也不让 llm 回写 memory。
2. ADR-007：037 不把 policy decision 或 stream failure 转化为 RecoveryManager 外的恢复准入。
3. ADR-008：037 不让 llm 或 runtime 因 shared object admission 获得 multi-agent/global orchestration 主控权。
4. Contracts 边界：本轮只评审 admission，不新增 `ModelRoute`、`PromptPolicyDecision`、`StreamHandle` shared 头文件。
