# RT-TODO-029 RuntimeProfileCompatibility 设计收敛

日期：2026-04-22  
任务：RT-TODO-029  
状态：已完成

## 1. 本地证据

1. `docs/todos/runtime/DASALL_runtime子系统专项TODO.md` 把 029 定义为围绕 `RuntimePolicySnapshot` 的 profile compatibility gate，完成判定要求 `desktop_full`、`edge_balanced`、`edge_minimal` 三档 profile 的通过、降级、拒绝结论可二值判定，且不能依赖测试私有映射。
2. `profiles/src/RuntimePolicyProvider.cpp` 已经把 `profiles/*/runtime_policy.yaml` 解析为 `RuntimePolicySnapshot`；runtime 自己没有额外 config projector，因此 029 的真实控制面不是再造配置对象，而是直接验证 snapshot 本身的预算、降级和 enablement 投影。
3. `tests/integration/memory/MemoryProfileCompatibilityTest.cpp` 与 `tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp` 已经冻结了 profile compatibility 的仓库模式：必须走 `ProfileCatalog -> BuildProfileResolver -> RuntimePolicyProvider -> ProfileCompatibilityValidator` 的真实资产链路，而不是手写 YAML parser 或手工拼 snapshot。
4. 三个 runtime profile YAML 已经在仓库中冻结：
   - `profiles/desktop_full/runtime_policy.yaml`
   - `profiles/edge_balanced/runtime_policy.yaml`
   - `profiles/edge_minimal/runtime_policy.yaml`
   因此 029 的最小 Build 落点只需要新增 integration gate 与 discoverability 注册。

## 2. 设计结论

1. 029 只新增 `RuntimeProfileCompatibilityTest`，不改 runtime production 代码；因为 profile 投影逻辑已经存在于 `RuntimePolicyProvider`，本轮缺的是 gate，不是实现体。
2. 测试必须同时覆盖三类差异：
   - runtime budget：`worker_threads`、`max_tokens`、`max_tool_calls`、`max_latency_ms`；
   - degrade / execution：`allow_model_failover`、`fallback_chain`、`audit_level`、`allowed_tool_domains`；
   - enablement：build manifest 中的 `llm_cloud_adapter`、`tools_mcp`、`multi_agent` 等模块开关。
3. 测试除差异断言外，还必须复用 `ProfileCompatibilityValidator` 验证共享 validator 仍判为 `Compatible`，避免 runtime profile gate 和 shared profile gate 出现分叉语义。
4. discoverability 仍是 029 的 Build 面一部分；新增 test target 必须在 `tests/integration/agent_loop/CMakeLists.txt` 中显式注册，并能被 `ctest -N` 发现。

## 3. 边界 / 职责

| 对象 | 本轮职责 | 明确不负责 |
|---|---|---|
| `RuntimePolicyProvider` | 从真实 profile YAML 产出 `RuntimePolicySnapshot` | 不负责 compatibility 断言 |
| `BuildProfileResolver` | 提供 profile build manifest 与 target platform 基线 | 不负责 runtime budget 解释 |
| `ProfileCompatibilityValidator` | 证明 snapshot + manifest + environment 仍满足共享兼容规则 | 不负责 profile-specific runtime 差异断言 |
| `RuntimeProfileCompatibilityTest` | 对 runtime budget / degrade / enablement 差异做二值断言 | 不引入新的 projector 或私有 YAML 解析 |

## 4. 数据 / 接口说明

1. 测试读取的输入必须全部来自仓库真实资产：
   - `profiles/*/runtime_policy.yaml`
   - `ProfileCatalog`
   - `BuildProfileResolver`
   - `RuntimePolicyProvider`
2. 差异断言只使用 `RuntimePolicySnapshot` 已暴露的 getter：
   - `runtime_budget()`
   - `worker_threads()`
   - `model_profile()`
   - `token_budget_policy()`
   - `capability_cache_policy()`
   - `degrade_policy()`
   - `execution_policy()`
   - `ops_policy()`
3. enablement 差异只通过 `BuildProfileManifest::enables_module(...)` 判定，不手动推导开关。

## 5. 流程 / 时序

1. 对 `desktop_full`、`edge_balanced`、`edge_minimal` 分别执行 `BuildProfileResolver::resolve_build_manifest(...)` 与 `RuntimePolicyProvider::load_snapshot(...)`。
2. 断言 snapshot 本身保持一致且 profile id round-trip 正确。
3. 对三档 profile 断言 runtime budget、history/compression、model route、stale-read、degrade、audit、remote diagnostics 和 module enablement 差异。
4. 对三档 profile 分别调用 `ProfileCompatibilityValidator::validate(...)`，确认 shared validator 仍返回 `Compatible`。
5. 读取 `tests/integration/agent_loop/CMakeLists.txt`，断言 target 与 ctest name 都已注册。

## 6. 文件范围

1. `tests/integration/agent_loop/CMakeLists.txt`
2. `tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp`

## 7. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| runtime snapshot profile matrix | `tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` |
| runtime profile compatibility discoverability | `tests/integration/agent_loop/CMakeLists.txt` |

## 8. Build 三件套

1. 代码目标：新增 `RuntimeProfileCompatibilityTest` 并注册 `dasall_runtime_profile_compatibility_integration_test`。
2. 测试目标：`RuntimeProfileCompatibilityTest`。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_runtime_profile_compatibility_integration_test && ctest --test-dir build-ci -R "^RuntimeProfileCompatibilityTest$" --output-on-failure`
   - `ctest --test-dir build-ci -N | rg "RuntimeProfileCompatibilityTest"`

## 9. 风险与回退

1. 如果 029 用测试私有 YAML 解析或手工构造 snapshot，会脱离真实 profile 资产链，不能代表实际 runtime compatibility。
2. 如果 029 只断言数值而不跑 `ProfileCompatibilityValidator`，shared profile gate 与 runtime gate 可能出现漂移。
3. 如果 029 不验证 discoverability，新增 test 即使存在也可能不进入顶层 gate，导致 acceptance 证据不完整。