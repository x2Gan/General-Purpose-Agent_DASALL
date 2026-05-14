# LLM-TODO-036 streaming 生命周期设计并后置实现设计收敛

日期：2026-05-14
任务：LLM-TODO-036
状态：Done

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 将 036 定义为阶段 J 后置项，要求先解 `LLM-BLK-005`：冻结 cancel / ownership / bounded session / backpressure 语义，且不新增 shared 头文件。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 7.2、10.1、12.1 明确 `shared StreamHandle` 尚未冻结，streaming 只能先保持 module-local / deferred。
3. [llm/include/stream/StreamSessionRef.h](../../../../llm/include/stream/StreamSessionRef.h) 当前只承载 `session_id`，是 module-local 生命周期锚点，不是 shared streaming handle。
4. [llm/src/LLMManager.cpp](../../../../llm/src/LLMManager.cpp) 中 `LLMManager::stream_generate()` 当前 fail-closed 返回 `RuntimeRetryExhausted`，没有打开 route execution 或 adapter session。
5. [llm/src/adapters/OpenAICompatibleAdapter.cpp](../../../../llm/src/adapters/OpenAICompatibleAdapter.cpp)、[llm/src/adapters/OllamaAdapter.cpp](../../../../llm/src/adapters/OllamaAdapter.cpp)、[llm/src/adapters/LocalLLMAdapter.cpp](../../../../llm/src/adapters/LocalLLMAdapter.cpp) 的 `stream_generate()` 都只返回稳定 placeholder `StreamSessionRef`。

## 2. 外部参考

1. Reactive Streams 规范把 backpressure 作为异步流的核心治理目标，强调不要让快速生产者迫使接收侧无限缓冲；这支持 DASALL 在 streaming 里采用 bounded session / bounded buffer / reject-on-overflow，而不是无界队列。
   参考：https://www.reactive-streams.org/
2. MDN AbortController 文档说明 abort 信号可用于中止异步操作、fetch body consumption 和 streams；这支持 DASALL 将 cancel 作为显式 lifecycle 事件，而不是依赖析构或断链隐式完成。
   参考：https://developer.mozilla.org/en-US/docs/Web/API/AbortController

## 3. Design 结论

1. `LLM-BLK-005` 在本轮被解为“设计与评审门已冻结”，不是“streaming 已可生产使用”。036 的完成条件是明确 lifecycle 语义、补 guard 测试、保持实现后置。
2. 当前不新增 `StreamHandle`、不修改 `contracts/`、不扩 `LLMRequest` / `LLMResponse`，继续把 `StreamSessionRef` 保持在 llm module-local 边界内。
3. 未来 `StreamSessionRegistry` 的 owner 是 llm 内部生命周期表，负责 session id 分配、bounded capacity、cancel mark、terminal cleanup 和 TTL reap；Runtime 仍拥有调用时机和最终恢复裁定，adapter 只拥有 provider transport cursor。
4. 取消语义冻结为显式状态转换：`Accepted/Active -> CancelRequested -> Cancelled`。取消必须通知 adapter transport，不允许靠 detach thread、析构副作用或 observer 丢失来表达。
5. ownership 语义冻结为三段：Runtime owns request intent 与 observer lifetime；llm owns session registry entry 与 terminal state；adapter owns provider stream cursor。`IStreamObserver*` 不得被 registry 持久拥有，只能在 active callback 窗口内使用。
6. backpressure 语义冻结为 fail-closed：全局 active stream session 有上限，超过上限拒绝新 session；每个 session 的 pending delta buffer 有上限，超过上限关闭该 session 并上报 overflow，不允许无界缓存。
7. cleanup 语义冻结为幂等：`Completed`、`Cancelled`、`Failed`、`Expired` 都是 terminal state；terminal 后的 cancel/cleanup 是 no-op；registry 必须允许周期性 reap，避免长期泄漏。
8. 由于 shared handle 和跨模块消费者仍未成熟，036 不实现真实 SSE/delta merge/adapter streaming。真实实现后续应以单独 Build 任务推进，并先通过 `StreamSessionLifecycleTest` 扩展为正向 session registry 用例。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| lifecycle / cancel / ownership / backpressure 冻结 | [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) |
| 036 交付证据 | [docs/todos/llm/deliverables/LLM-TODO-036-streaming生命周期设计并后置实现设计收敛.md](LLM-TODO-036-streaming生命周期设计并后置实现设计收敛.md) |
| fail-closed 与 placeholder guard | [tests/unit/llm/StreamSessionLifecycleTest.cpp](../../../../tests/unit/llm/StreamSessionLifecycleTest.cpp) |
| unit discoverability | [tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |
| TODO / worklog 证据回写 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_开发执行记录.md) |

## 5. Build 三件套

1. 代码目标：新增 `StreamSessionLifecycleTest`，接入 llm unit CMake 和 `dasall_unit_tests` 聚合；本轮不新增 `llm/src/stream/StreamSessionRegistry.cpp`，真实 streaming 实现后置。
2. 测试目标：
   - 负例：`LLMManager::stream_generate()` 在 lifecycle owner 未落地前保持 fail-closed，不创建 response、不触发 route execution、不发出 recompose 信号。
   - 正例：OpenAI-compatible / Ollama / Local adapter 的 `stream_generate()` 继续返回稳定 placeholder `StreamSessionRef`，证明 adapter skeleton 没有误宣称 stream-ready。
3. 验收命令：
   - `cmake --build build-ci --target dasall_stream_session_lifecycle_unit_test`
   - `ctest --test-dir build-ci -N -R StreamSessionLifecycleTest`
   - `ctest --test-dir build-ci -R StreamSessionLifecycleTest --output-on-failure`
4. 验收结果：
   - `Build_CMakeTools` 构建 `dasall_stream_session_lifecycle_unit_test` 成功。
   - `ListTests_CMakeTools` 已列出 `StreamSessionLifecycleTest`。
   - `RunCtest_CMakeTools` 定向执行 `StreamSessionLifecycleTest`，结果为 1/1 通过。
   - 追加构建 `dasall_unit_tests` 时，llm 段 38 条测试全部通过且包含 `StreamSessionLifecycleTest`；聚合目标后续被非 llm 的 `DaemonReadinessCommandTest`、`DaemonCancelCommandTest`、`AccessCancelForwardingTest` 三项既有失败挡住，记为无关 validation blocker，不作为 036 回退依据。

## 6. Build 合规复核

1. 代码注释：新增测试的函数名和断言消息已直接表达设计意图；生产代码未改动，因此无需补实现注释。
2. 正负例：已覆盖 manager fail-closed 负例和 adapter placeholder 正例。
3. 测试发现性：`StreamSessionLifecycleTest` 已接入 llm unit CMake、添加 `unit;llm` 标签，并进入 `dasall_unit_tests` 聚合目标。
4. TODO / worklog：本轮回写专项 TODO、详细设计与开发执行记录，足以追溯 `LLM-BLK-005` 的解阻方式。
5. 提交隔离：036 的提交范围应只包含本交付物、详细设计/TODO/worklog 回写、CMake 接线与新增 guard 测试。

## 7. 风险与回退

1. 若后续把 placeholder `StreamSessionRef` 误判为 stream-ready，会绕过本轮冻结的 cancel/backpressure/cleanup 门；应回退到 `LLMManager::stream_generate()` fail-closed。
2. 若后续需要真实 streaming，应新增 `StreamSessionRegistry`、adapter delta transport、observer terminal callback 与 timeout/cancel 测试，而不是在 036 这个设计后置任务中继续扩张。
3. shared `StreamHandle` 是否进入 contracts 仍属于 037 或后续 contracts admission owner；036 不给 shared contracts Go 结论。
