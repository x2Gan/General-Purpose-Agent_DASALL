# KNO-TODO-016 CorpusCatalog route metadata snapshot 设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-016
- 状态：已收敛
- 对应 Blocker：无

## 1. 输入与约束

1. `KNO-TODO-006` 已冻结 `CorpusDescriptor`，因此 016 的目标不是重新定义 corpus schema，而是把它收敛为 query/ingest 共用的 route metadata snapshot。
2. 详细设计 6.13.6 已明确 `CorpusCatalog` 的唯一职责是维护 corpus 描述、trust level、supported retrieval modes、active snapshot version 和 route tags；查询路径只能读取 snapshot，不能写 catalog。
3. `KNO-TODO-009` 的 `CorpusRouter` 直接依赖 `CorpusCatalogSnapshot`；如果 016 不先落 supporting header，Router 只能临时偷用 `std::vector<CorpusDescriptor>`，后续会返工过滤和 delta apply 语义。
4. `KNO-TODO-003` 已冻结首批 corpus baseline 与 typed provenance 规则，所以 016 可以直接把 `CorpusDescriptor` 当作 route/ingest 的共享事实来源，不需要再补 corpus 设计。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 详细设计 6.13.6 `CorpusCatalog` 卡片 | 明确要求“只暴露只读 snapshot”“delta 应用失败保留上一份 valid snapshot” | 016 可以直接实现 snapshot value object + fail-closed apply |
| 详细设计 6.13.1 `CorpusRouter` 输入 | Router 需要 `CorpusCatalogSnapshot` 而不是裸 descriptor list | 016 必须优先提供 `find/filter` 接口 |
| `KnowledgeTypes.h` 中 `CorpusDescriptor` | 已具备 `trust_level`、`authority_level`、`allowed_formats`、`supported_modes`、`tags`、`metadata` | route metadata 不需要再扩 public ABI |
| KNO-TODO-003 交付物 | 首批 corpus baseline、`allowed_formats`、`include/exclude_globs`、metadata 必填键均已冻结 | 016 只需校验并承载这些字段，不需要重开 schema 讨论 |

## 3. 外部参考

1. Martin Fowler / Thoughtworks 的 Versioned Value 模式强调“每次更新生成新版本，允许读者继续读取旧版本”，适合作为 catalog snapshot 更新的抽象依据：<https://martinfowler.com/articles/patterns-of-distributed-systems/versioned-value.html>
2. Copy-on-write 模式强调“直到写入前共享旧状态，失败时保留原有可读版本”，与 016 的“delta apply 失败保留上一 valid snapshot”一致：<https://en.wikipedia.org/wiki/Copy-on-write>

对本任务的落地启发：

1. `CorpusCatalogSnapshot` 应该是可复制的只读值对象，而不是暴露可变容器引用。
2. 写路径应先在候选副本上完成校验，再一次性替换 active snapshot，避免查询路径观察到半成品状态。
3. delta apply 的失败语义应 fail-closed：返回失败并保留旧 snapshot，而不是“尽量应用一部分”。

## 4. 设计结论

### 4.1 组件边界

`CorpusCatalog` 本轮只承担三件事：

1. 持有当前 active route metadata snapshot。
2. 提供只读 `CorpusCatalogSnapshot` 查询接口。
3. 提供 `replace_all` / `apply_delta` 两个受校验的更新入口。

本轮不承担：

1. 持久化 JSON 落盘。
2. freshness 计算。
3. ingest 扫描或 source trust 判决。
4. Router 的 mode 选择与 policy 判决。

### 4.2 Snapshot 结构

```cpp
class CorpusCatalogSnapshot {
public:
  bool empty() const;
  std::size_t size() const;
  bool has_consistent_values() const;
  std::vector<CorpusDescriptor> list_all() const;
  std::optional<CorpusDescriptor> find_by_id(std::string_view corpus_id) const;
  std::vector<CorpusDescriptor> filter_by_tags(const std::vector<std::string>& tags) const;
  std::vector<CorpusDescriptor> filter_by_mode(RetrievalMode mode) const;
};
```

语义固定：

1. snapshot 是 cheap-copy 的只读对象，内部共享不可变状态。
2. `filter_by_tags()` 采用“query tags 全包含匹配”语义；传空 tags 时返回全量列表。
3. `filter_by_mode()` 只根据 `supported_modes` 做显式过滤，不代替 Router 做 degrade 判决。

### 4.3 Delta apply 纪律

```cpp
struct CorpusCatalogDelta {
  std::vector<CorpusDescriptor> upserted_descriptors;
  std::vector<std::string> removed_corpus_ids;
};
```

规则：

1. `upserted_descriptors` 中每个 descriptor 必须满足 `CorpusDescriptor::has_consistent_values()`。
2. `removed_corpus_ids` 不能重复，也不能和 upsert 的 `corpus_id` 冲突。
3. 候选 snapshot 额外校验两个全局唯一键：`corpus_id`、`source_uri`。
4. 若 delta 非法或候选 snapshot 非法，`apply_delta()` 返回 `false`，active snapshot 保持原值不变。
5. 更新顺序固定为：先基于当前 snapshot 构造候选副本，再执行 remove/upsert，再整体校验，再原子替换为新 snapshot。

### 4.4 Cold start 语义

1. 默认构造的 `CorpusCatalog` 必须返回空但一致的 snapshot。
2. `replace_all()` 允许从空 catalog 启动首份 baseline。
3. 空 snapshot 下 `find_by_id()` / `filter_by_tags()` / `filter_by_mode()` 都必须返回空结果，而不是抛异常。

## 5. Design -> Build 映射

| Design 项 | Build / 文档落点 |
|---|---|
| `CorpusCatalogSnapshot` 只读值对象 | `knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp` |
| `replace_all` / `apply_delta` fail-closed 入口 | `knowledge/include/index/CorpusCatalog.h`、`knowledge/src/index/CorpusCatalog.cpp` |
| corpus catalog 单测 | `tests/unit/knowledge/CorpusCatalogTest.cpp`、`tests/unit/knowledge/CorpusCatalogDeltaApplyTest.cpp`、`tests/unit/knowledge/CorpusCatalogColdStartTest.cpp` |
| target 接线 | `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt` |

## 6. 本任务三件套

- 代码目标：新增 `CorpusCatalog` / `CorpusCatalogSnapshot` / `CorpusCatalogDelta`，把 route metadata 统一收敛为可复制、可过滤、可 fail-closed 更新的 snapshot 组件。
- 测试目标：覆盖基础查询、delta apply 成功路径、delta apply 失败保留旧 snapshot，以及 cold-start 空 catalog 行为。
- 验收命令：

```bash
cmake -S . -B build-ci -G "Unix Makefiles" && \
cmake --build build-ci --target dasall_knowledge dasall_unit_tests && \
ctest --test-dir build-ci -R "CorpusCatalog.*Test" --output-on-failure
```

## 7. 风险与回退

1. 风险：如果 016 直接把 `CorpusCatalogSnapshot` 暴露为可变容器引用，后续 Router / IngestionCoordinator 很容易在读路径写 catalog。
   - 处置：snapshot 只暴露值拷贝接口，不返回可写引用。
2. 风险：如果 delta apply 允许部分成功，Router 可能看到重复 `corpus_id` 或失配 `source_uri` 的半成品视图。
   - 处置：候选副本先完整校验，失败时保留旧 snapshot。
3. 风险：如果 cold-start 空 catalog 被视为异常，后续 init/bootstrap 需要额外写一次 special-case。
   - 处置：把“空但一致”作为第一类合法状态。

## 8. 收敛结论

1. 016 已把 `CorpusCatalog` 收敛为 Route / Evidence 纯计算链的 supporting snapshot 组件，而不是提前实现 ingest/index/freshness 主链。
2. `CorpusRouter` 后续将直接消费 `CorpusCatalogSnapshot` 的 `find/filter` 接口，不再需要自造 descriptor 过滤逻辑。
3. delta apply fail-closed 语义已固定，后续 `IndexWriter` / `IngestionCoordinator` 可以在此基础上补真实 manifest refresh，而不会反向改动 query 读接口。