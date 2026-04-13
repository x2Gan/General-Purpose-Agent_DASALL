# LLM-TODO-025 OpenAICompatibleAdapter skeleton 设计收敛

日期：2026-04-13
任务：LLM-TODO-025
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 025 固定为 `llm/src/adapters/OpenAICompatibleAdapter.h`、`llm/src/adapters/OpenAICompatibleAdapter.cpp` 与 [llm/include/ILLMTransport.h](../../../../llm/include/ILLMTransport.h) 的最小 skeleton，并将验收出口收敛为 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest`。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.14 明确要求新 provider 接入遵循“adapter family 准入 -> registry 纳管 -> route 生效 -> 结果归一化”的主路径；adapter 只负责 provider 协议适配、请求投递、响应接收和本地错误采样，不负责 Prompt 选择、上下文装配或把 provider raw payload 写回 shared contracts。
3. 同一设计文档的 6.15.4 与 [llm/src/execution/ResponseNormalizer.cpp](../../../../llm/src/execution/ResponseNormalizer.cpp) 已冻结 022 的边界：adapter 成功路径要返回 shared `LLMResponse` + module-local diagnostics，malformed 2xx payload 应由 normalizer fail-closed 收口为 `ProviderProtocol`，而不是让 adapter 直接越权定义第二套协议失败 owner。
4. [docs/todos/llm/deliverables/LLM-TODO-041-ProviderConfig投影与mutable-overlay规则设计收敛.md](LLM-TODO-041-ProviderConfig%E6%8A%95%E5%BD%B1%E4%B8%8Emutable-overlay%E8%A7%84%E5%88%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已把 `auth_ref`、`header_refs`、`base_url_alias`、activation flag 与 `snapshot_version` 安全投影到 `LLMAdapterConfig`，因此 025 不再补 secret/config 投影，只消费既有 adapter init 输入。
5. [tests/unit/llm/AdapterHealthProbeTest.cpp](../../../../tests/unit/llm/AdapterHealthProbeTest.cpp) 当前只覆盖 `AdapterRegistry` 对 `ILLMAdapter::health_check()` 的聚合语义；025 需要在同名测试内再补 concrete adapter 自身的 healthy / degraded / unavailable 三条探针路径。

## 2. 外部参考

1. OpenAI Developers Chat Completions API 说明表明 OpenAI-compatible chat completion 的核心交互是 `POST /chat/completions`，请求主体围绕 `model` 与 `messages`，响应主体通过 `choices[*].message`、`finish_reason` 与 `usage` 返回结果。这为 025 的最小请求/响应映射提供了协议锚点。参考：`https://developers.openai.com/api/reference/resources/chat`。
2. LiteLLM 的 openai-compatible provider 文档强调：已 Admit 的 OpenAI-compatible 端点应通过 `api_base`、API key/secret 引用与统一的 chat-completions 路径复用同一 protocol family，而不是为每个 provider 复制一套 adapter 代码。这与 DASALL 6.14 的 family reuse 规则一致。参考：`https://docs.litellm.ai/docs/providers/openai_compatible`。

## 3. Design 结论

1. `ILLMTransport` 只提供 adapter-internal 的最小同步 transport 抽象：方法、URL、auth/header refs、base_url alias、snapshot version、body、timeout 与 transport response。它的 owner 仍在 adapter 层，不进入 shared contracts，也不承接 Prompt/Router/Normalizer 的职责。
2. `OpenAICompatibleAdapter::init()` 只做 fail-closed 配置冻结：要求 `adapter_family == "openai_compatible"`、`base_url` / `auth_ref` / `provider_instance_id` / `snapshot_version` 非空、timeout 有效，并把 041 投影出的 alias/ref 信息保存在 adapter 内部，供 `generate()` 与 `health_check()` 复用。
3. `generate()` 采用最小 unary skeleton：
   - 将 provider-neutral `LLMRequest` 映射为 `POST {base_url}/chat/completions`
   - 从 `request.model_route` 提取 concrete `model_id`
   - 把 `messages` 映射为 OpenAI-compatible `role/content` 数组，未显式带前缀的消息默认视为 `user`
   - 首轮只稳定支持 direct text、tool_calls 与 refusal 三类 provider 响应片段；`reasoning_content` 保留在 `AdapterProviderDiagnostics`，不进入 shared `LLMResponse`
4. 2xx 但载荷不完整的场景不在 adapter 内部直接构造 provider-protocol error，而是返回“可被 normalizer 二次校验”的 shared `LLMResponse` 草稿，让 022 的 fail-closed 边界继续收口 malformed payload。
5. `health_check()` 复用 transport 发起轻量 GET 探针，首轮以 `{base_url}/models` 作为 OpenAI-compatible family 的最小可达性检查：2xx 记为 healthy，429/503 记为 degraded-but-ready，transport error 或其他非 2xx 记为 unavailable。这样 025 能为 021 的 registry 健康快照提供 concrete adapter 输入，但不宣称真实 endpoint 联调已完成。
6. `stream_generate()` 在 025 保持 fail-closed 占位，仅返回稳定的占位 `StreamSessionRef`，不提前解锁 036 的 streaming 生命周期 blocker。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 新增 adapter-internal transport 抽象，承载 URL/ref/body/timeout/response | [llm/include/ILLMTransport.h](../../../../llm/include/ILLMTransport.h)、[tests/unit/llm/InterfaceSurfaceTest.cpp](../../../../tests/unit/llm/InterfaceSurfaceTest.cpp) |
| 落 OpenAI-compatible adapter skeleton 与最小 unary/health glue | [llm/src/adapters/OpenAICompatibleAdapter.h](../../../../llm/src/adapters/OpenAICompatibleAdapter.h)、[llm/src/adapters/OpenAICompatibleAdapter.cpp](../../../../llm/src/adapters/OpenAICompatibleAdapter.cpp)、[llm/CMakeLists.txt](../../../../llm/CMakeLists.txt) |
| 验证 provider-neutral request -> OpenAI-compatible request 映射与 response -> AdapterCallResult 映射 | [tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../../tests/unit/llm/AdapterProtocolMappingTest.cpp)、[tests/unit/llm/CMakeLists.txt](../../../../tests/unit/llm/CMakeLists.txt)、[tests/unit/CMakeLists.txt](../../../../tests/unit/CMakeLists.txt) |
| 扩展 adapter health probe 验证 concrete adapter 的 healthy/degraded/unavailable 三态 | [tests/unit/llm/AdapterHealthProbeTest.cpp](../../../../tests/unit/llm/AdapterHealthProbeTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `ILLMTransport` 与 `OpenAICompatibleAdapter` skeleton，完成 init / generate / stream_generate 占位 / health_check 最小实现，并接入 llm 静态库。
2. 测试目标：`AdapterProtocolMappingTest` 覆盖请求映射、success response 映射与 provider-private diagnostics 剥离载体；`AdapterHealthProbeTest` 覆盖 concrete adapter healthy / degraded / unavailable 三条路径。
3. 验收命令：
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure`

## 6. 风险与回退

1. 025 只实现 OpenAI-compatible skeleton，不顺手扩到 Ollama / Local family，也不把真实 secret 解析、HTTP 客户端或 endpoint alias 解析器硬塞进本轮；这些仍属于后续 family 任务或 infra/config 注入链收口范围。
2. 由于仓库当前没有通用 JSON 依赖，025 的 payload 处理将采用最小 deterministic parser，只保证单测覆盖到的 chat-completions success/tool/refusal 片段；若后续 family 需要更完整的 schema 支持，应在不突破 shared 边界的前提下演进 adapter-internal parser。
3. 若后续发现 `/models` 不适合作为某些 OpenAI-compatible endpoint 的健康探针，替换动作应限定在 adapter 内部探针策略或 transport mock 夹具，不回改 021 registry 的健康 owner 语义。