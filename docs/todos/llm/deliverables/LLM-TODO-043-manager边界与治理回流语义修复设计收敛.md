# LLM-TODO-043 manager 边界与治理回流语义修复设计收敛

日期：2026-04-13
任务：LLM-TODO-043
状态：D Gate PASS / B Gate In Progress

## 1. 本地证据

1. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.5.2 已冻结 `ILLMManager` 为 Runtime 访问 llm 的统一入口，因此 manager handoff 的输入边界与失败回流语义必须稳定，不能继续依赖测试中的隐式约定。
2. 同一设计文档的 6.7.2 与 6.15.3 明确要求 `OverBudget` 由 llm 回流 Runtime 做重装配，而不是在 llm 内部二次裁剪；这意味着 manager 不能把 `OverBudget` 折叠成只有 `PolicyDenied` 的普通 deny。
3. [contracts/include/llm/LLMRequest.h](../../../../contracts/include/llm/LLMRequest.h) 对 shared request 的必填边界已经要求 `model_route` 非空；但 [llm/include/LLMGenerateRequest.h](../../../../llm/include/LLMGenerateRequest.h) 与多处 manager/integration request helper 之前仍把它当作可空字段，形成 module-local handoff 与 shared contract 的公开矛盾。
4. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 在修复前把 `request.prompt_id` 直接映射到 `PromptQuery.prompt_release_id`，把输入审计锚点误用为显式 selector；同时对 `PromptPolicyDisposition::OverBudget` 与 `RequireRecompose` 没有任何专门回流字段。
5. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 上半部分的 3.2 / 4.2 / 4.3 仍是启动期快照，却继续以“当前状态”口吻描述 placeholder-only / 未落盘 / 未接线，已经与 17.x 里 001~042 全部 Done 的 Gate 证据冲突。

## 2. 外部参考

1. cppreference 对 `std::optional` 的定义适合当前 manager 边界：当 shared `ResultCode` 还没有更细的 over-budget code 时，module-local 结果对象应通过显式 optional 字段承载附加语义，而不是伪造一个新的默认值或复用无关字段。本轮据此把 `governance_disposition` 与 `prompt_release_id_override` 冻结为可选字段。参考：https://en.cppreference.com/w/cpp/utility/optional

## 3. Design 结论

1. `LLMGenerateRequest.request.model_route` 从“可空 pre-route hint”收敛为 required pre-route hint。它仍不是最终 provider route，但必须在 manager handoff 处非空，以保持与 shared `LLMRequest` 的最小契约一致，并给 PromptComposer/PromptPolicy 提供稳定预算输入。
2. 显式 PromptRegistry selector 与输出审计锚点彻底解耦：新增 `LLMGenerateRequest.prompt_release_id_override` 承载显式 release selector；`request.prompt_id/request.prompt_version` 继续仅用于响应审计锚点，不能再被复用为输入 selector。
3. `LLMManagerResult` 新增 `governance_disposition`，并把 `OverBudget` / `RequireRecompose` 通过 `ErrorInfo.safe_to_replan=true` 回流 Runtime。shared `ResultCode` 现阶段仍保持 `PolicyDenied`，但 module-local 结果语义不能再丢失“应重装配/重规划”的真实意图。
4. `LLMManager::make_prompt_query()` 不再把 `request.model_route` 误写成 `PromptQuery.model_family`；route hint 与 prompt 资产选择维度是两套不同语义，避免因为 route hint 非空而错误限制 PromptRegistry。
5. 本专项 TODO 的 3.2 / 4.2 / 4.3 保留为启动期历史快照，但必须显式标注为历史快照，并补一段当前状态更新，避免再次把启动时的 placeholder-only 描述误当作当前态。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| manager handoff 新增显式 release override，route hint 改为 required | [llm/include/LLMGenerateRequest.h](../../../../llm/include/LLMGenerateRequest.h) |
| manager failure 结果保留 governance disposition / safe_to_replan | [llm/include/LLMManagerResult.h](../../../../llm/include/LLMManagerResult.h)、[llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) |
| 不再复用 request.prompt_id 作为 PromptRegistry 显式 selector | [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp)、[tests/unit/llm/LLMManagerSuccessPathTest.cpp](../../../../tests/unit/llm/LLMManagerSuccessPathTest.cpp) |
| OverBudget / RequireRecompose 回流与 regression coverage | [tests/unit/llm/LLMManagerFailureMappingTest.cpp](../../../../tests/unit/llm/LLMManagerFailureMappingTest.cpp)、[tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp](../../../../tests/integration/llm/LLMGovernanceFailureIntegrationTest.cpp) |
| 启动快照与当前态区分 | [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md)、[docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) |

## 5. Build 三件套

1. 代码目标：修正 `LLMGenerateRequest`、`LLMManagerResult` 与 `LLMManager.cpp` 的边界；补齐所有 manager/unit/integration request helper；刷新专项 TODO 与设计文档中的当前态表述。
2. 测试目标：
   - `LLMInterfaceSurfaceTest` 冻结 route hint / prompt release override / governance disposition。
   - `LLMManagerSuccessPathTest` 验证显式 `prompt_release_id_override` 生效，且不会复用输入 audit `prompt_id`。
   - `LLMManagerFailureMappingTest` 与 `LLMGovernanceFailureIntegrationTest` 验证 deny / over-budget 的 disposition 与 `safe_to_replan`。
   - `LLMManagerTimeoutPolicyTest`、`LLMManagerRetryBudgetTest`、`LLMManagerConcurrencyGuardTest` 与受影响 integration fixture 通过新的 route-hint 边界。
3. 验收动作：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -R "LLM(InterfaceSurface|Manager(SuccessPath|FailureMapping|TimeoutPolicy|RetryBudget|ConcurrencyGuard)|GovernanceFailureIntegration)Test" --output-on-failure`

## 6. 风险与回退

1. 若把 `request.prompt_id` 再次复用为显式 selector，会重新引入“输入 selector 与输出 audit 锚点共用一条槽位”的语义污染，导致 PromptRegistry 行为和响应审计不可判定。
2. 若把 `request.model_route` 重新放宽为可空字段，manager handoff 会再次与 shared `LLMRequest` 的 required-fields contract 脱节，后续测试只能通过“测试故意不填字段”来规避错误语义。
3. 在 shared contracts 仍未补齐 finer-grained governance code 的前提下，本轮只能用 `governance_disposition + safe_to_replan` 保留 Runtime 回流语义；若后续要新增 shared result code，应由 contracts owner 单独发起评审，不应在 llm 内私下扩张。