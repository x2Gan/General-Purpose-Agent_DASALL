# LLM-TODO-027 LocalLLMAdapter skeleton 设计收敛

日期：2026-04-13
任务：LLM-TODO-027
状态：D Gate PASS / B Gate PASS

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm子系统专项TODO.md) 已把 027 固定为 [llm/src/adapters/LocalLLMAdapter.h](../../../../llm/src/adapters/LocalLLMAdapter.h) 与 [llm/src/adapters/LocalLLMAdapter.cpp](../../../../llm/src/adapters/LocalLLMAdapter.cpp) 的 Local family skeleton，验收出口继续收敛为 `AdapterProtocolMappingTest` 与 `AdapterHealthProbeTest`。
2. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) 的 6.14 与 adapter endpoints 约束已冻结 027 的 owner：concrete adapter 只负责 provider 协议适配、请求投递、响应接收与本地错误采样，`local runtime path` 作为 adapter init 已投影的 endpoint 输入被消费，不重写 Prompt/Router/Normalizer 的 owner。
3. [docs/todos/llm/deliverables/LLM-TODO-025-OpenAICompatibleAdapter-skeleton设计收敛.md](LLM-TODO-025-OpenAICompatibleAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 与 [docs/todos/llm/deliverables/LLM-TODO-026-OllamaAdapter-skeleton设计收敛.md](LLM-TODO-026-OllamaAdapter-skeleton%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已冻结 concrete family skeleton 的 transport seam：027 继续复用 [llm/include/ILLMTransport.h](../../../../llm/include/ILLMTransport.h) 作为 adapter-internal transport 抽象，不新增第二套 local SPI。
4. [docs/todos/llm/deliverables/LLM-TODO-041-ProviderConfig投影与mutable-overlay规则设计收敛.md](LLM-TODO-041-ProviderConfig%E6%8A%95%E5%BD%B1%E4%B8%8Emutable-overlay%E8%A7%84%E5%88%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已把 `auth_ref`、`header_refs`、`base_url_alias`、activation flag 与 `snapshot_version` 投影到 `LLMAdapterConfig`，因此 027 只消费既有 adapter init 输入，不在 adapter 内直连 secret / endpoint resolver。
5. [docs/architecture/DASALL_profiles模块详细设计.md](../../../architecture/DASALL_profiles模块详细设计.md) 已冻结 `llm_local_adapter` profile key，以及 `edge_minimal` 的 Local 优先与 `factory_test` 的 Local/LAN 优先语义。027 只需提供 Local family 的最小 skeleton，使这些 profile 偏好后续有稳定接缝可接，而不提前宣称真实 runtime 联调闭环。
6. [tests/unit/llm/ModelRouterTestSupport.h](../../../../tests/unit/llm/ModelRouterTestSupport.h) 已存在 `local-runtime` provider 与 `local-small` model fixture，因此 027 继续沿用 `local-runtime/local-small` 作为 route / model identity 锚点，避免 family skeleton 与既有 router fixture 漂移。

## 2. 外部参考

1. 无新增外部协议文档。027 的 local runtime contract 仍属于仓库内 module-local skeleton 约定，本轮以 repo 内详细设计、profile 语义与 router fixture 作为唯一锚点，不引入未评审的第三方协议面。

## 3. Design 结论

1. `LocalLLMAdapter::init()` 只做 fail-closed 配置冻结：要求 `adapter_family == "local_runtime"`，并校验 `adapter_id`、`provider_instance_id`、`base_url`、`base_url_alias`、`auth_ref`、`snapshot_version` 与 timeout 有效；header refs 必须保持唯一。027 不在 adapter 内新增本地 socket、共享内存或专用 executor 接缝。
2. `generate()` 采用最小 unary skeleton，并把 local runtime 路径固定为 repo-owned transport contract：
   - 将 provider-neutral `LLMRequest` 映射为 `POST {base_url}/generate`
   - 从 `request.model_route` 提取 concrete model id，并继续沿用 `local-runtime/local-small` fixture
   - 复用 shared `messages`，将 `developer:` 下沉为 `system` role，其余 `system/user/assistant/tool` 保持原语义
   - 请求体固定包含 `stream:false` 与 `execution_mode:"local_runtime"`
   - `response_format` 与 `max_output_tokens` 继续作为可选 runtime 参数向下投影
3. 成功路径只收敛到 shared `LLMResponse` 所需的最小字段：
   - `output_text` 映射为 direct response
   - `tool_calls` 映射为 `ToolCallIntent`
   - `refusal_reason` 映射为 `Refusal`
   - `finish_reason` 透传给 022 做 canonicalization
   - `input_tokens` / `output_tokens` / `total_tokens` 收敛为 usage fragment
   - `runtime_session_id` 映射为 `provider_trace_id`
   - `reasoning_trace` 保留到 `AdapterProviderDiagnostics.reasoning_content`
4. `health_check()` 复用 transport 发起 `GET {base_url}/health` 探针：2xx 视为 healthy，429/503 视为 degraded-but-ready，其余非 2xx 或 transport error 视为 unavailable。这样 027 能把 concrete local adapter 健康状态接到 021 的 registry 快照，而不扩大 health owner。
5. `stream_generate()` 在 027 继续保持 fail-closed 占位，只返回稳定的占位 `StreamSessionRef`，不提前解锁 streaming 生命周期相关任务。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 落 Local family adapter skeleton 与最小 unary/health glue | [llm/src/adapters/LocalLLMAdapter.h](../../../../llm/src/adapters/LocalLLMAdapter.h)、[llm/src/adapters/LocalLLMAdapter.cpp](../../../../llm/src/adapters/LocalLLMAdapter.cpp)、[llm/CMakeLists.txt](../../../../llm/CMakeLists.txt) |
| 验证 shared request -> local runtime request 的映射、usage / diagnostics 收敛与 transport failure 非异常返回 | [tests/unit/llm/AdapterProtocolMappingTest.cpp](../../../../tests/unit/llm/AdapterProtocolMappingTest.cpp) |
| 扩展 Local concrete adapter 健康探针，覆盖 healthy / degraded / unavailable 三态 | [tests/unit/llm/AdapterHealthProbeTest.cpp](../../../../tests/unit/llm/AdapterHealthProbeTest.cpp) |

## 5. Build 三件套

1. 代码目标：新增 `LocalLLMAdapter` skeleton，完成 init / generate / stream_generate 占位 / health_check 最小实现，并接入 llm 静态库。
2. 测试目标：`AdapterProtocolMappingTest` 覆盖 local runtime 请求映射、usage / diagnostics 收敛与 transport failure；`AdapterHealthProbeTest` 覆盖 concrete adapter 的 healthy / degraded / unavailable 三态。
3. 验收命令：
   - `cmake --build build-ci --target dasall_unit_tests`
   - `ctest --test-dir build-ci -R "Adapter(ProtocolMapping|HealthProbe)Test" --output-on-failure`

## 6. 风险与回退

1. 027 只冻结 Local family 的最小 `/generate` / `/health` 语义，不同时扩到真实 runtime session 生命周期、模型 warmup、streaming 或本地 IPC 联调；这些仍属于后续 Local integration 或 streaming 任务范围。
2. 027 继续复用 module-local 的轻量 payload parser，只保证单测覆盖到的 `output_text`、`tool_calls`、`refusal_reason`、usage、`runtime_session_id` 与 `reasoning_trace` 片段能被 deterministic 映射；若后续 local runtime schema 演进，需要继续在 adapter 内扩展，而不是反推 shared contracts。
3. 若后续真实 local runtime 不再使用当前 `local-runtime:///...` transport 地址形式，调整也应先回到 041 的投影/transport contract 评审层，而不是直接在 027 的 adapter 内部引入第二套 endpoint 解释规则。