# LLM-GAP-001 D10 streaming lifecycle closeout

日期：2026-05-19
来源任务：LLM-GAP-001
状态：Done

## 1. 任务边界

1. 本轮只收口 `LLM-GAP-001`，不合并 `LLM-GAP-002` 或后续缺口。
2. 任务目标是确认 D10 streaming lifecycle 已由 `LLM-FIX-001` 在 llm owner 内部闭合，并把该结论从“缺口描述”提升为可追溯 closeout 记录。
3. 本轮不推进 shared `StreamHandle` / `StreamSessionRef` admission，不声明跨模块 shared stream-ready。
4. 本轮不使用 qemu / kvm，不生成 release 或 L6 soak 证据。

## 2. 本地证据

| 证据面 | 当前状态 | 判定 |
|---|---|---|
| 生命周期 owner | `StreamSessionRegistry` 已落地 registry / cancel / overflow / terminal state 语义 | D10 module-local owner 存在 |
| manager 编排 | `LLMManager::stream_generate()` 已调用 streaming route 并收口 observer / usage / terminal result | 不再是 placeholder path |
| OpenAI-compatible adapter | `OpenAICompatibleAdapter::stream_generate()` 已处理 SSE / delta merge / terminal frame | OpenAI-compatible streaming path 可测 |
| cognition bridge | `CognitionLlmBridgeErrorMappingTest` 覆盖 streaming preference 的 failure projection | cognition 不再误把 streaming preference 当作不可解释失败 |

## 3. 外部参考

MDN Server-Sent Events 文档明确 `text/event-stream` 使用以空行分隔的 UTF-8 文本事件，`data:` 多行需要拼接，连接异常会触发 error 事件，连接可通过 close 结束。这与当前 OpenAI-compatible SSE delta merge、observer rejection 与 terminal state 收口的测试口径一致。

## 4. Design -> Build 映射

| Design 判定 | Build 三件套 |
|---|---|
| D10 lifecycle owner 必须 module-local，不反向进入 shared contracts | 代码目标：复用既有 `StreamSessionRegistry`、`LLMManager::stream_generate()`、`OpenAICompatibleAdapter::stream_generate()`；本轮不新增产品代码 |
| streaming 成功、取消、overflow、observer rejection 与 terminal usage 必须可二值判定 | 测试目标：`StreamSessionLifecycleTest`、`LLMStreamingIntegrationTest`、`CognitionLlmBridgeErrorMappingTest` |
| 关闭 GAP 时不得外推 shared stream-ready | 验收命令：`RunCtest_CMakeTools(tests=["StreamSessionLifecycleTest","LLMStreamingIntegrationTest","CognitionLlmBridgeErrorMappingTest"])` |

## 5. D Gate

结果：PASS。

1. 范围单一：只处理 `LLM-GAP-001`。
2. 设计边界清楚：module-local streaming closed；shared stream admission deferred。
3. Build 三件套已锁定：代码目标、测试目标、验收命令均可二值判断。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_stream_session_lifecycle_unit_test","dasall_llm_streaming_integration_test","dasall_cognition_llm_bridge_error_mapping_unit_test"])`
	- 结果：通过；三个 focused targets 构建成功。
2. `RunCtest_CMakeTools(tests=["StreamSessionLifecycleTest","LLMStreamingIntegrationTest","CognitionLlmBridgeErrorMappingTest"])`
	- 结果：工具在 generation 层失败，未进入测试执行；该结果不代表测试失败。
3. fallback：`ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(StreamSessionLifecycleTest|LLMStreamingIntegrationTest|CognitionLlmBridgeErrorMappingTest)$'`
	- 结果：通过；`100% tests passed (3/3)`，`CognitionLlmBridgeErrorMappingTest`、`StreamSessionLifecycleTest`、`LLMStreamingIntegrationTest` 均通过。

## 7. 完成判定

`LLM-GAP-001` 已关闭。

1. D10 streaming lifecycle 在 llm module-local owner 内已闭合。
2. 本轮 focused validation 证明 streaming lifecycle、LLM streaming integration 与 cognition streaming preference projection 未回退。
3. shared `StreamHandle` / `StreamSessionRef` admission 继续保持 deferred，不作为本 gap 的完成条件。