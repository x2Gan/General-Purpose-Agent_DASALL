# KNO-TODO-017 FreshnessController 新鲜度评估设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-017
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. `KNO-TODO-007` 已冻结 `KnowledgeConfigSnapshot`，其中 `catalog_refresh_interval_ms`、`catalog_expire_after_ms`、`allow_stale_read` 已具备稳定来源，因此 017 不需要再解释 profile owner，只需要做纯计算评估。
2. 详细设计 6.13.4 已明确 `FreshnessController` 的职责是“根据 manifest、时间戳、profile 投影和 query 级 `allow_stale` 计算 `FreshnessSnapshot`”，并禁止它触发 ingest、切换 snapshot 或做恢复裁定。
3. `KNO-TODO-009` 的 `CorpusRouter` 与 `KNO-TODO-010` 的 `Reranker` 后续都依赖 `FreshnessSnapshot`；如果 017 不先落 supporting header，Router/Reranker 只能各自临时拼 freshness 逻辑，后续必然返工。
4. `EvidenceSlice` 的 `freshness` 字段已经在 006 进入 public ABI，因此 017 需要固定 freshness 状态与 reason code 语义，为 011 的证据投影准备单一事实来源。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 详细设计 6.13.4 `FreshnessController` 卡片 | 已定义 `FreshnessSnapshot` 字段、职责与验收出口 | 017 可以直接实现纯计算 controller + supporting structs |
| 详细设计 6.13.3 `IndexManifest` | 已冻结 `snapshot_id`、`built_at`、`effective_at`、`tokenizer_profile` 等最小字段 | 017 可以依赖 manifest 视图计算 `age_ms`，不需要等待 019 的真实读路径 |
| `KnowledgeConfigSnapshot` | 已包含 refresh/expire/stale policy 参数 | freshness 不需要自建配置系统 |
| 详细设计失败语义 | 明确“缺失 manifest 或时间戳非法时，不允许默认 fresh；仅 profile 与 query 同时允许时才允许 stale” | 017 必须把 dual gate 写进接口，而不是留给 Router 手写 |

## 3. 外部参考

1. RFC 5861 定义了 `stale-while-revalidate` 与 `stale-if-error`：内容可以在明确窗口内以 stale 形式服务，但必须有显式的 staleness 上限，超过窗口后不得继续无条件服务。017 可借用其“fresh window + stale window + hard stop”思路来映射 `catalog_refresh_interval_ms` / `catalog_expire_after_ms`：<https://datatracker.ietf.org/doc/html/rfc5861>

对本任务的落地启发：

1. freshness 需要区分“仍在 fresh window”“已 stale 但允许短暂服务”“已经超出可容忍 stale 上限”。
2. stale 服务必须是显式 opt-in，而不是默认行为。
3. freshness 组件应只产出状态与建议，不在评估函数里触发后台 refresh。

## 4. 设计结论

### 4.1 组件边界

`FreshnessController` 本轮只承担三件事：

1. 根据 manifest 与配置计算 `age_ms`。
2. 产出 `FreshnessSnapshot` 的状态、准入标志和 rebuild 建议。
3. 用稳定 `reason_codes` 描述 manifest 缺失、时间戳异常、stale allow/reject 的原因。

本轮不承担：

1. 触发 ingest / rebuild。
2. 写 `CorpusCatalog` 或 `VersionLedger`。
3. 裁定 Runtime 的 retry/replan/degrade。

### 4.2 输入对象与接口

为避免把 query 级 stale opt-in 隐含塞回 Router，本轮把 dual gate 显式收敛到接口：

```cpp
struct IndexManifest {
  std::uint32_t format_version = 1;
  std::string lexical_backend = "sqlite_fts5";
  std::string tokenizer_profile;
  std::string snapshot_id;
  std::int64_t built_at = 0;
  std::int64_t effective_at = 0;
  std::size_t document_count = 0;
  std::size_t chunk_count = 0;
  bool vector_enabled = false;
};

struct FreshnessSnapshot {
  FreshnessState state = FreshnessState::Unknown;
  std::int64_t age_ms = 0;
  bool stale_read_allowed = false;
  bool rebuild_recommended = false;
  std::vector<std::string> reason_codes;
};

class FreshnessController {
public:
  FreshnessSnapshot evaluate(
      const std::optional<IndexManifest>& manifest,
      const KnowledgeConfigSnapshot& config,
      std::int64_t now_ms,
      bool query_allow_stale = false) const;
};
```

说明：

1. `query_allow_stale` 作为显式参数进入 017，而不是等 009 再偷偷补接口；否则无法自动验证“profile 与 query 同时允许”这一约束。
2. `IndexManifest` 仍维持 module-local supporting type，不进入 contracts。

### 4.3 状态机与原因码

1. `manifest` 缺失：`state=Unknown`，`rebuild_recommended=true`，reason 包含 `manifest_missing`。
2. `manifest` 时间戳非法：`state=Unknown`，`rebuild_recommended=true`，reason 包含 `manifest_timestamp_invalid`。
3. `age_ms <= catalog_refresh_interval_ms`：`state=Fresh`，`rebuild_recommended=false`，reason 包含 `within_refresh_interval`。
4. `catalog_refresh_interval_ms < age_ms <= catalog_expire_after_ms`：
   - 若 `config.allow_stale_read && query_allow_stale`：`state=StaleAllowed`，`stale_read_allowed=true`，reason 包含 `refresh_interval_elapsed`、`stale_read_allowed`。
   - 否则：`state=StaleRejected`，`stale_read_allowed=false`，reason 包含 `refresh_interval_elapsed`，并根据拒绝来源补 `profile_stale_read_disabled` 或 `query_stale_opt_in_missing`。
5. `age_ms > catalog_expire_after_ms`：`state=StaleRejected`，`rebuild_recommended=true`，reason 包含 `catalog_expired`。

### 4.4 一致性约束

1. `FreshnessSnapshot` 必须保持唯一 `reason_codes`。
2. `FreshnessState::Fresh` 不允许 `stale_read_allowed=true` 或 `rebuild_recommended=true`。
3. `FreshnessState::StaleAllowed` 必须同时满足 `stale_read_allowed=true` 与 `rebuild_recommended=true`。
4. `FreshnessState::Unknown` / `StaleRejected` 都不允许 `stale_read_allowed=true`。

## 5. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| `IndexManifest` / `FreshnessSnapshot` supporting types | `knowledge/include/health/FreshnessController.h` |
| freshness 状态机与 reason code | `knowledge/src/health/FreshnessController.cpp` |
| freshness 单测 | `tests/unit/knowledge/FreshnessControllerTest.cpp`、`tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` |
| target 接线 | `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt` |

## 6. 本任务三件套

- 代码目标：新增 `FreshnessController`、`FreshnessSnapshot` 和最小 `IndexManifest` 视图，把 freshness 评估固定为纯计算组件。
- 测试目标：覆盖 manifest 缺失、timestamp 非法、fresh、stale allow、stale reject 与纯计算重复求值一致性。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_unit_tests && \
ctest --test-dir build-ci -R "FreshnessController.*Test" --output-on-failure
```

## 7. 风险与回退

1. 风险：如果 017 不把 `query_allow_stale` 显式建模，009 很容易在 Router 里重复 freshness 判定逻辑。
   - 处置：在 controller 接口层就把 dual gate 固定下来。
2. 风险：如果 freshness 组件默认把 manifest 缺失判成 fresh，后续 query path 会无根据地服务未知 snapshot。
   - 处置：manifest 缺失和时间戳异常统一 fail-closed 到 `Unknown`。
3. 风险：如果 stale 窗口没有硬上限，过时 snapshot 会被长期默许服务。
   - 处置：`catalog_expire_after_ms` 之后统一 `StaleRejected`。

## 8. 收敛结论

1. 017 已把 freshness 语义收敛为可复用 supporting layer，后续 009/010/026 可直接消费 `FreshnessSnapshot` 而不是自写年龄判断。
2. `FreshnessController` 保持纯计算边界，不触发 rebuild、不写状态，只输出事实快照与建议。
3. `query_allow_stale` 与 profile stale policy 的双门控已经固定，后续不会再把 stale serve 退化成隐式默认行为。