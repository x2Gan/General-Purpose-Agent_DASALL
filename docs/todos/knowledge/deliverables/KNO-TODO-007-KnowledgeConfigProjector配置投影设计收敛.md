# KNO-TODO-007 KnowledgeConfigProjector 配置投影设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-007
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. 006 已经把 `KnowledgeConfigSnapshot` 冻结进 public surface，因此 007 的目标不是重新发明 config 对象，而是实现唯一的 profile/deployment 投影入口。
2. 详细设计 6.10 与 6.13.6 已明确：Knowledge 不新增 profile schema v1 顶层域，只能复用 `enabled_modules.knowledge`、`enabled_modules.memory_vector`、`token_budget_policy`、`capability_cache_policy`、`degrade_policy` 与 `runtime_budget`。
3. `RuntimePolicySnapshot` 当前并不暴露 `enabled_modules.*`，因此 007 若只消费 snapshot，会丢失 `knowledge_enabled` / `vector_enabled` 的唯一 owner 信息；必须和 `BuildProfileManifest` 一起消费，才能维持“投影视图而不是平行配置系统”的边界。
4. deployment override 只能覆盖 Knowledge module-local snapshot 字段，不能重写 profile schema 含义；request-level override 仍然留在 `KnowledgeQuery` / facade merge 路径，不属于 007。
5. `knowledge=true && memory_vector=false` 是明确合法组合；007 必须把它收敛为 lexical-only，而不是把它视为配置错误。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 详细设计 6.10 投影表 | 已给出 `knowledge_enabled`、`vector_enabled`、`retrieval_mode_default`、`evidence_budget_tokens`、`request_deadline_ms`、`max_parallel_recall` 等派生规则 | 007 可以直接把这些规则实现为 projector |
| `profiles/include/RuntimePolicySnapshot.h` | 暴露 token/capability/degrade/runtime budget，但不暴露 `enabled_modules.*` | 模块开关必须从 `BuildProfileManifest` 读取 |
| `memory/src/config/MemoryConfigProjector.cpp` | 已采用 `RuntimePolicySnapshot + BuildProfileManifest` 双输入模式 | Knowledge projector 应复用同一 owner 模式，而不是另造读取 YAML 的入口 |

## 3. 设计结论

### 3.1 Projector 输入边界

`KnowledgeConfigProjector::project(...)` 收敛为：

```cpp
std::optional<KnowledgeConfigSnapshot> project(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest,
    const KnowledgeConfigProjectorOverlay& overlay = {});
```

边界解释：

1. `snapshot` 提供 token/capability/degrade/runtime budget 等 profile 已投影的共享策略域。
2. `manifest` 提供 `knowledge` / `memory_vector` 模块开关，避免 projector 偷读 YAML。
3. `overlay` 只表达 module-local deployment override，不承担 request-level override。

### 3.2 派生规则落点

1. `knowledge_enabled = manifest.enables_module("knowledge")`
2. `vector_enabled = manifest.enables_module("memory_vector")`
3. `retrieval_mode_default`：
   - `knowledge=false` -> `LexicalOnly`
   - `knowledge=true && memory_vector=false` -> `LexicalOnly`
   - `knowledge=true && memory_vector=true` -> `Hybrid`
4. `evidence_budget_tokens = min(max_input_tokens / 4, compression_threshold / 2)`
5. `max_context_projection_items` 按 worker tier 派生：8 线程及以上 -> 8；4-7 -> 6；其余 -> 4。
6. `request_deadline_ms = clamp(runtime_budget.max_latency_ms / 3, 300, 1500)`
7. `max_parallel_recall = min(2, max(1, worker_threads / 2))`
8. `sparse_recall_timeout_ms` / `dense_recall_timeout_ms` 默认取 `request_deadline_ms * 35%`
9. `ingest_timeout_ms` 采用 profile class 默认：desktop/cloud 30000，edge/factory 10000。

### 3.3 Override merge 纪律

1. overlay 只允许覆盖 `KnowledgeConfigSnapshot` 已定义字段，不允许重写 `knowledge_enabled` / `vector_enabled` 的 owner。
2. 若 overlay 覆盖了 `request_deadline_ms`，且未显式给出 lane timeout，则 sparse/dense lane timeout 必须基于新的 deadline 重新派生。
3. 若 overlay 试图在 `memory_vector=false` 时把 `retrieval_mode_default` 改成 `DenseOnly` / `Hybrid`，投影必须返回 `std::nullopt`，保持 fail-closed。
4. projector 的合法输出必须继续满足 006 冻结的 `KnowledgeConfigSnapshot::has_consistent_values()`，不允许绕过 ABI 自校验。

## 4. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| `snapshot + manifest + overlay -> KnowledgeConfigSnapshot` | `knowledge/src/config/KnowledgeConfigProjector.h`、`knowledge/src/config/KnowledgeConfigProjector.cpp` |
| projector source 接入 knowledge target | `knowledge/CMakeLists.txt` |
| projector unit gate | `tests/unit/knowledge/KnowledgeConfigProjectionTest.cpp`、`tests/unit/knowledge/CMakeLists.txt` |
| TODO / worklog 回写 | `docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. 本任务三件套

- 代码目标：实现 `KnowledgeConfigProjector::project(...)`，把 `RuntimePolicySnapshot + BuildProfileManifest + overlay` 收敛为唯一的 `KnowledgeConfigSnapshot` 投影入口。
- 测试目标：`KnowledgeConfigProjectionTest` 覆盖默认派生、override merge、`knowledge=true && memory_vector=false` 兼容性与非法 override fail-closed。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_knowledge_config_projection_unit_test dasall_knowledge_interface_surface_unit_test && \
ctest --test-dir build-ci -R "KnowledgeConfigProjectionTest|dasall_knowledge_interface_surface_unit_test" --output-on-failure
```

## 6. 风险与回退

1. 风险：若后续实现直接从 YAML 或环境变量重读 `knowledge` / `memory_vector`，会绕过 manifest owner，破坏 profiles -> consumer 投影矩阵。
   - 处置：007 明确把模块开关固定为 `BuildProfileManifest` owner；projector 只消费 manifest，不读 YAML。
2. 风险：若 overlay 可以重写 `knowledge_enabled` / `vector_enabled`，会形成 deployment 层第二套模块开关系统。
   - 处置：overlay 不暴露这两个字段；enabled 状态只能来自 manifest。
3. 风险：若 `request_deadline_ms` override 不重新驱动 lane timeout 派生，会导致 facade 使用的总 budget 与 lane budget 脱节。
   - 处置：overlay deadline 生效后，未显式覆盖的 sparse/dense timeout 必须按新 deadline 重新计算。

## 7. 收敛结论

1. Knowledge 配置投影已实现为单一入口，并与 memory/llm 一样维持“共享 snapshot + consumer projector”的一致模式。
2. `knowledge=true && memory_vector=false` 已在 projector 层收敛为合法 lexical-only 组合，为后续 profile compatibility integration 提前消除了歧义。
3. 007 只落投影视图和单测证据，没有提前实现 facade merge、health probe 或 telemetry，因此仍保持原子任务边界。