# LLM-TODO-026 OllamaAdapter skeleton 设计收敛

日期：2026-04-13
任务：LLM-TODO-026
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 026 固定为 [llm/src/adapters/OllamaAdapter.h](../../../../llm/src/adapters/OllamaAdapter.h) 与 [llm/src/adapters/OllamaAdapter.cpp](../../../../llm/src/adapters/OllamaAdapter.cpp) 的 LAN family skeleton，验收出口继续收敛为 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest`。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.14 要求新 provider 接入沿“adapter family 准入 -> registry 纳管 -> route 生效 -> 结果归一化”主路径推进；adapter 只负责 provider 协议适配、请求投递、响应接收与本地错误采样，不重写 Prompt/Router/Normalizer 的 owner。
3. [docs/todos/llm/deliverables/LLM-TODO-025-OpenAICompatibleAdapter-skeleton设计收敛.md](LLM-TODO-025-OpenAICompatibleAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已冻结 025 的 transport seam：026 复用 [llm/include/ILLMTransport.h](../../../../llm/include/ILLMTransport.h) 作为 adapter-internal transport 抽象，不再扩 shared contracts 或新增第二套 transport SPI。
4. [docs/todos/llm/deliverables/LLM-TODO-041-ProviderConfig投影与mutable-overlay规则设计收敛.md](LLM-TODO-041-ProviderConfig%E6%8A%95%E5%BD%B1%E4%B8%8Emutable-overlay%E8%A7%84%E5%88%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已把 `auth_ref`、`header_refs`、`base_url_alias`、activation flag 与 `snapshot_version` 投影到 `LLMAdapterConfig`，因此 026 只消费既有 adapter init 输入，不越权解析 secret 或 endpoint alias。
5. [llm/src/execution/ResponseNormalizer.cpp](../../../../llm/src/execution/ResponseNormalizer.cpp) 与 022 的结论仍然成立：adapter 成功路径返回 shared `LLMResponse` 草稿 + module-local diagnostics；`thinking` 等 provider-private 字段停留在 diagnostics，malformed payload 仍由 normalizer fail-closed 收口。

## 2. 外部参考

1. Ollama API 文档说明原生 chat 调用采用 `POST /api/chat`，请求主体围绕 `model`、`messages`、`stream`、`format` 与 `options`，无流式时响应主体通过 `message.content`、`message.tool_calls`、`done_reason`、`prompt_eval_count` 与 `eval_count` 返回结果。这为 026 的最小 request/response 映射提供了协议锚点。参考：`https://github.com/ollama/ollama/blob/main/docs/api.md`。
2. 同一 API 文档给出 `GET /api/tags` 作为列出本地可用模型的最小可达性端点，因此 026 的 LAN health probe 采用 `/api/tags`，只宣称“服务可达/退化/不可达”，不宣称模型拉取、warmup 或真实业务联调闭环。参考：`https://docs.ollama.com/api`。

## 3. Design 结论

1. `OllamaAdapter::init()` 只做 fail-closed 配置冻结：要求 `adapter_family == "ollama_native"`，并校验 `adapter_id`、`provider_instance_id`、`base_url`、`base_url_alias`、`auth_ref`、`snapshot_version` 与 timeout 有效；header refs 必须保持唯一。026 不在 adapter 内引入 LAN 特判 secret 解析或本地 socket 直连。
2. `generate()` 采用最小 unary skeleton，并选择 Ollama 原生 chat 语义而非 `/api/generate`：
   - 将 provider-neutral `LLMRequest` 映射为 `POST {base_url}/api/chat`
   - 从 `request.model_route` 提取 concrete model id
   - 复用 shared `messages`，将 `developer:` 前缀下沉为 Ollama 支持的 `system` role，其余 `system/user/assistant/tool` 保持原语义
   - `response_format == "json_object"` 时映射为 `"format":"json"`
   - `max_output_tokens` 映射为 `options.num_predict`
3. 成功路径只收敛到 shared `LLMResponse` 所需的最小字段：
   - `message.content` 映射为 direct response
   - `message.tool_calls` 映射为 `ToolCallIntent`
   - `done_reason` 透传给 022 做 canonicalization
   - `prompt_eval_count` / `eval_count` 收敛为 usage fragment，并在 adapter 内计算 `total_tokens`
   - `message.thinking` 保留到 `AdapterProviderDiagnostics.reasoning_content`
   - Ollama native 最小 API 不提供稳定 trace id，因此 `provider_trace_id` 在 026 保持为空
4. `health_check()` 复用 transport 发起 `GET {base_url}/api/tags` 探针：2xx 视为 healthy，429/503 视为 degraded-but-ready，其余非 2xx 或 transport error 视为 unavailable。这样 026 能把 concrete LAN adapter 健康状态接到 021 的 registry 快照，而不扩大 health owner。
5. `stream_generate()` 在 026 继续保持 fail-closed 占位，只返回稳定的占位 `StreamSessionRef`，不提前解锁 streaming 生命周期相关任务。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落 Ollama native adapter skeleton 与最小 unary/health glue | [llm/src/adapters/OllamaAdapter.h](../../../../llm/src/adapters/OllamaAdapter.h)、[llm/src/adapters/OllamaAdapter.cpp](../../../../llm/src/adapters/OllamaAdapter.cpp)、[llm/CMakeLists.txt](../../../../llm/CMakeLists.txt) |
| 验证 shared request -> Ollama native chat request 的映射、usage 推导与 transport failure 非异常返回 | [tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../../tests/unit/llm/AdapterProtocolMappingTest.cpp) |
| 扩展 LAN concrete adapter 健康探针，覆盖 healthy / degraded / unavailable 三态 | [tests/unit/llm/AdapterHealthProbeTest.cpp](../../../../tests/unit/llm/AdapterHealthProbeTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `OllamaAdapter` skeleton，完成 init / generate / stream_generate 占位 / health_check 最小实现，并接入 llm 静态库。
2. 测试目标：`AdapterProtocolMappingTest` 覆盖 Ollama native chat 请求映射、usage 推导与 transport failure；`AdapterHealthProbeTest` 覆盖 concrete adapter 的 healthy / degraded / unavailable 三态。
3. 验收命令：
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure`

## 6. 风险与回退

1. 026 只冻结 LAN family 的最小 `/api/chat` / `/api/tags` 语义，不同时扩到 `/api/generate`、模型拉取、warmup 或本地 runtime session 管理；这些仍属于后续 Local family 或 integration 任务范围。
2. 026 继续复用 module-local 的轻量 payload parser，只保证单测覆盖到的 message/tool/thinking/usage 片段能被 deterministic 映射；如果后续 Ollama schema 演进，需要在 adapter 内部扩展，而不是反推 shared contracts。
3. 若后续真实 LAN 环境要求无 auth_ref 直连，调整也应先落在 041 的投影/transport contract 评审层，而不是直接在 026 的 adapter 内部放松 reference 边界。