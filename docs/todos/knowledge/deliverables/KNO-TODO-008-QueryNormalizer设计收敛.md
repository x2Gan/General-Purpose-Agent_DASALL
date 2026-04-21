# KNO-TODO-008 QueryNormalizer 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-008 | 实现 QueryNormalizer`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.1 QueryNormalizer 卡片、7 KNO-D02、专项 TODO 中 KNO-R10 风险约束。
3. 前置满足：`KNO-TODO-006` 已冻结 `KnowledgeQuery` / `KnowledgeErrorCode` / `ErrorInfo`，`KNO-TODO-007` 已冻结 `KnowledgeConfigSnapshot` 投影；当前无 blocker。
4. 本轮目标：只落确定性 query 标准化 supporting layer，为 009 `CorpusRouter` 提供稳定的 `NormalizedQuery` 输入，不提前实现路由、rewrite 或索引读取。

## 2. 设计结论

### 2.1 边界与职责

1. `QueryNormalizer` 只负责：query text 清洗、lexical term 提取、domain tag / allowed corpus 归一、top-k / projection item 边界裁剪、`KnowledgeQueryKind` 到 query intent 默认值映射。
2. `QueryNormalizer` 不负责：
   - LLM query rewrite；
   - 语料选择或 retrieval mode 决策；
   - freshness 判断；
   - evidence ranking / assembly。
3. 输入超长时 fail-soft：裁剪并写入 warning，而不是 silently 丢弃或直接失败。
4. `domain_tags` / `allowed_corpora` 只允许收窄，不允许因为非法输入而扩大检索范围。

### 2.2 数据与接口

1. 新增 `NormalizedQuery`：固定 `normalized_text`、`lexical_terms`、`domain_tags`、`allowed_corpora`、`top_k`、`max_context_projection_items`、`prefer_exact_match`、`allow_stale` 与 `warnings`。
2. 新增 `NormalizeResult`：统一成功/失败返回面，成功时携带 `NormalizedQuery`，失败时携带结构化 `ErrorInfo`。
3. 新增 `QueryNormalizePolicy`：
   - `max_query_text_bytes`
   - `max_lexical_terms`
   - `max_top_k`
   - `max_context_projection_items`
   - `allowed_domain_tags`
   - `allowed_corpora`
   - `domain_tag_aliases`
4. `QueryNormalizer::normalize()` 的失败面：
   - 空/全空白 query -> `QueryValidationFailed`
   - `MultiHop` -> `NotSupported`
   - policy 自身不一致 -> `InternalError`

### 2.3 关键流程

1. 校验 `request_id` 与 `query_text` 的最小有效性。
2. 对 `query_text` 做空白折叠、ASCII lower-case 与 UTF-8 安全截断。
3. 从标准化文本提取 lexical terms；若提取结果为空则拒绝。
4. 归一 `domain_tags` / `allowed_corpora`：alias resolve、allowlist 过滤、去重。
5. 根据 `query_kind` 计算 `prefer_exact_match` 和 kind ceiling；再与 policy ceiling 取更严格上限。
6. 所有 fail-soft 行为都必须进入 `warnings`，保证 009/012/026 可观测。

### 2.4 评审补充

1. 专项 TODO 的 KNO-R10 明确要求：`MultiHop` 在 v1 只保留枚举，不允许落执行链路。因此本轮补充 `KnowledgeErrorCode::NotSupported`，避免把 feature gate 伪装成 `QueryValidationFailed`。
2. `NormalizedQuery` 不携带 `ContextPacket` 或 runtime state；它只保留路由所需的确定性 query 事实，维持 Query Plane 低耦合。

## 3. Design -> Build 映射

1. `knowledge/include/query/QueryNormalizer.h`
   - 定义 `QueryNormalizePolicy`、`NormalizedQuery`、`NormalizeResult`、`QueryNormalizer`。
2. `knowledge/src/query/QueryNormalizer.cpp`
   - 实现 text canonicalization、lexical term 提取、tag/corpus allowlist 过滤与 MultiHop fail-fast。
3. `knowledge/include/KnowledgeErrors.h`
   - 补齐 `NotSupported` 错误码，使 008 能按风险约束返回结构化错误。
4. `tests/unit/knowledge/QueryNormalizerTest.cpp`
   - 验证 canonical text、alias/allowlist、deterministic lexical terms。
5. `tests/unit/knowledge/QueryNormalizerBoundaryTest.cpp`
   - 验证空 query、超长 query、warning、MultiHop not supported。

## 4. 验证计划

1. Build_CMakeTools：`dasall_knowledge`、`dasall_query_normalizer_unit_test`、`dasall_query_normalizer_boundary_unit_test`。
2. RunCtest_CMakeTools：`QueryNormalizerTest`、`QueryNormalizerBoundaryTest`。
3. 若 public error surface 被修改，追加 `dasall_knowledge_interface_surface_unit_test` 回归。
4. build-ci 验收：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_query_normalizer_unit_test dasall_query_normalizer_boundary_unit_test dasall_knowledge_interface_surface_unit_test`
   - `ctest --test-dir build-ci -R "QueryNormalizer.*Test|dasall_knowledge_interface_surface_unit_test" --output-on-failure`

## 5. 完成判定

1. 空 query、超长 query、tag/corpus 收窄过滤、warning 语义都可自动验证。
2. `MultiHop` 在 v1 必须返回结构化 `NotSupported`，不允许悄悄降级为普通 fact lookup。
3. 008 交付后，009 可直接消费 `NormalizedQuery`，而不再关心文本清洗和 alias 归并细节。