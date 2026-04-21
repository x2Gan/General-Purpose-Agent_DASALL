# KNO-TODO-001 lexical 索引技术选型与 PoC 设计收敛

- 日期：2026-04-21
- 任务：KNO-TODO-001
- 状态：已收敛
- 对应 Blocker：KNO-BLK-001

## 1. 输入与约束

1. `SparseRetriever`、`IndexReader`、`IndexWriter` 是 lexical-only 主链的核心组件；若 lexical engine 继续保持多候选，后续 Build 会同时卡住接口、测试夹具和 snapshot 格式定义。
2. `profiles/edge_balanced/runtime_policy.yaml` 给出 `max_latency_ms: 7000`、`max_memory_mb: 2048`、`worker_threads: 6`。按 knowledge 详设 6.10 的投影规则，`request_deadline_ms = clamp(7000 / 3, 300, 1500) = 1500ms`，`sparse_recall_timeout_ms = 1500 * 35% = 525ms`。
3. `third_party/README.md` 与 `cmake/DASALLThirdParty.cmake` 已冻结依赖引入优先级：submodule > local cache > FetchContent。001 不能通过新增另一套检索库绕开既有 third-party 约束。
4. `memory/CMakeLists.txt` 已存在 `dasall_sqlite3` 静态库构建配方，但当前 compile definitions 仅包含 `SQLITE_THREADSAFE=1` 与 `SQLITE_OMIT_LOAD_EXTENSION=1`，尚未显式启用 `SQLITE_ENABLE_FTS5`。

## 2. 本地证据

| 证据 | 观察结果 | 结论 |
|---|---|---|
| 仓库 sqlite 接入现状 | 已有 `dasall_sqlite3`，但没有 `SQLITE_ENABLE_FTS5` | 不需要新增检索第三方库；需要把现有 sqlite 配方提升为可复用 shared third-party target，并显式打开 FTS5 |
| edge_balanced 预算 | sparse lane 预算约 525ms | lexical lane 允许使用本地 BM25/FTS 查询，只要 p99 明显低于 525ms |
| 10k chunks 内存库 PoC | SQLite 3.45.1 + FTS5，`p50=0.3530ms`，`p95=0.4271ms`，`p99=0.9224ms`，`max=1.0761ms` | FTS5 BM25 在 host 上远低于 edge lane 预算 |
| 10k chunks 文件库/WAL PoC | `db_size=6.7773MB`，`p50=0.3576ms`，`p95=0.4314ms`，`p99=0.7640ms`，`max=0.9396ms` | 文件化 snapshot 读路径同样满足延迟预算 |
| 10k chunks trigram PoC | `db_size=4.5859MB`，`p50=3.1166ms`，`p95=5.0649ms`，`p99=5.7153ms`，中英文样例 query 均可命中 | CJK/中英混合语料可行，但延迟显著高于英文优先 tokenizer |
| unicode61 检索验证 | `"Knowledge 子系统"=0`、`"词法检索"=0`、`"子系统"=0`，而 `"active snapshot"=1`、`"knowledge"=1` | `unicode61` 不能被当成中文可检索的充分默认方案 |

## 3. 外部参考

1. SQLite FTS5 官方文档确认：FTS5 已包含在 sqlite amalgamation 中，手工编译 `sqlite3.c` 时需显式打开 `SQLITE_ENABLE_FTS5`；FTS5 原生支持 BM25、`unicode61`、`porter`、`trigram` tokenizer。
2. SQLite compile-time 选项文档确认：`SQLITE_OMIT_LOAD_EXTENSION` 与静态启用 FTS5 并不冲突，意味着 DASALL 可以继续禁止运行时 extension loading，同时通过静态编译获得 FTS5 能力。

## 4. 设计结论

### 4.1 唯一 lexical engine

1. Knowledge v1 lexical index 唯一技术路线冻结为 SQLite FTS5。
2. 本轮明确拒绝两类替代路线：
   - 不引入自研倒排索引，避免把排序、分词、持久化和跨平台维护成本一起引入到 K1 阶段。
   - 不引入新的第三方检索库，避免打破当前 third-party 治理顺序并额外制造 cross-compile 变量。

### 4.2 依赖接入方式

1. 复用现有 sqlite amalgamation 配方，不走运行时 loadable extension。
2. 在进入 KNO-TODO-013 / 019 / 020 之前，把当前定义在 `memory/CMakeLists.txt` 的 `dasall_sqlite3` 构建逻辑提升为共享 third-party target；knowledge 与 memory 都只链接该 target，不建立 `knowledge -> memory` 头文件依赖。
3. 共享 sqlite target 的 compile definitions 冻结为：
   - `SQLITE_THREADSAFE=1`
   - `SQLITE_OMIT_LOAD_EXTENSION=1`
   - `SQLITE_ENABLE_FTS5=1`

### 4.3 snapshot 与 manifest 冻结

1. lexical snapshot 采用“每个 snapshot 一份独立 SQLite 数据库文件”的不可变模型，由 `IndexWriter` 在 shadow 空间构建完成后原子切换。
2. v1 不采用另一种 lexical engine 作为 fallback；若构建失败，只允许回退到 last-known-good SQLite FTS5 snapshot。
3. `IndexManifest` 至少补齐以下字段，用于消除后续格式歧义：

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
```

4. `format_version=1` 对应“SQLite FTS5 snapshot + sidecar metadata”的首版格式；后续若 tokenizer profile、表结构或 filter 语义发生破坏性变化，只通过升级 `format_version` 并触发 rebuild 处理，不支持 down migration。

### 4.4 tokenizer 策略冻结

1. engine 固定为 SQLite FTS5，但 tokenizer 作为 snapshot 构建策略的一部分显式建模，不再隐含在实现里。
2. v1 默认 profile：`porter unicode61 remove_diacritics 1`。
   - 适用场景：英文、标识符、命令、文件路径、接口名主导的语料。
   - 选择原因：host-side p99 更低，且与 DASALL 当前工程文档、符号名检索模式更贴近。
3. CJK / 中英混合语料 profile：`trigram`。
   - 适用场景：中文说明文档、混合自然语言知识库。
   - 选择原因：能够覆盖中文短语与子串检索，而 `unicode61` 无法满足该需求。
4. 禁止把 `unicode61` 单独描述为“中英混合通用默认 tokenizer”。这是 001 需要显式消除的设计歧义。

### 4.5 延迟与资源结论

1. 依据当前 host-side PoC，默认 tokenizer 的文件库 `p99=0.7640ms`，内存库 `p99=0.9224ms`，均显著低于 `edge_balanced` sparse lane 预算 `525ms`。
2. `trigram` 在 10k chunks 混合语料上 `p99=5.7153ms`，仍低于 `525ms`，但相较默认 profile 明显更慢，因此只作为 CJK/混合语料的显式 profile，而不是统一默认。
3. 当前 PoC 结论足以解除“技术路线不可判定”的 blocker；后续 Build 阶段仍需补一轮基于仓库 vendored sqlite 和目标平台的 smoke/benchmark，以验证 host-side 结论不会因编译选项差异失真。

## 5. 执行流冻结

1. `IndexWriter` 在 shadow 目录创建新的 SQLite snapshot，按选定 `tokenizer_profile` 建表并写入 chunk 文本与 metadata。
2. `IndexWriter` 完成 shadow build 后，记录 candidate manifest，并通过 snapshot swap 切换 active snapshot。
3. `IndexReader` 只以只读方式打开 active snapshot；查询期间若发生 swap，当前读继续使用旧 snapshot，保持 MVCC 语义。
4. `SparseRetriever` 把 `RetrievalPlan` 转为 FTS5 `MATCH` 表达式、metadata filter 和 language/trust filter，按 BM25 得分产出 raw hits。
5. sentence-window 扩展在 FTS 命中之后执行，不改变 lexical engine，也不改写 snapshot 格式。

## 6. Design -> Build 映射

| 后续任务 | Build 入口 | 本次冻结给出的前置结论 |
|---|---|---|
| KNO-TODO-013 `SparseRetriever` | `knowledge/include/retrieve/SparseRetriever.h`、`knowledge/src/retrieve/SparseRetriever.cpp` | query backend 固定为 SQLite FTS5；需要显式支持 tokenizer profile 对应的 query 生成与 metadata filter |
| KNO-TODO-019 `IndexReader` | `knowledge/include/index/IndexReader.h`、`knowledge/src/index/IndexReader.cpp` | active snapshot 为只读 SQLite FTS5 DB；manifest 必须暴露 `format_version` / `lexical_backend` / `tokenizer_profile` |
| KNO-TODO-020 `IndexWriter` | `knowledge/include/index/IndexWriter.h`、`knowledge/src/index/IndexWriter.cpp` | 采用不可变 snapshot + shadow build + atomic swap；失败只回退到 last-known-good SQLite FTS5 snapshot |
| 共享 CMake 接线 | `cmake/DASALLThirdParty.cmake`、相关模块 `CMakeLists.txt` | sqlite target 需要从 memory 局部配方提升为共享 third-party target，并补 `SQLITE_ENABLE_FTS5=1` |

## 7. 本任务三件套

- 代码目标：更新 knowledge 详设、专项 TODO 和 worklog，冻结唯一 lexical 路线、manifest 口径与依赖接入方式。
- 测试目标：保留并回写 10k chunks BM25 host-side PoC 与 mixed-language tokenizer 证据，确认 edge_balanced 预算可承接 lexical lane。
- 验收命令：

```bash
rg -n "SQLite FTS5|tokenizer_profile|format_version|KNO-BLK-001|0.7640ms|5.7153ms" \
  docs/architecture/DASALL_knowledge子系统详细设计.md \
  docs/todos/knowledge/DASALL_knowledge子系统专项TODO.md \
  docs/todos/knowledge/deliverables/KNO-TODO-001-lexical索引技术选型与PoC设计收敛.md
```

## 8. 风险与回退

1. 风险：当前 PoC 基于 host Python sqlite3 3.45.1，尚不是仓库最终 vendored target 的直接产物。
   - 处置：Build 阶段补一次基于共享 `dasall_sqlite3` 的定向 smoke/benchmark。
2. 风险：若继续让 `dasall_sqlite3` 仅存在于 `memory/CMakeLists.txt`，knowledge 会被迫在构建层面对 memory 产生不必要耦合。
   - 处置：把 sqlite 配方提升到共享 third-party helper，再由 memory 与 knowledge 分别链接。
3. 风险：若把 `trigram` 当成默认 profile，会在英文/标识符主导语料上引入不必要的索引与查询成本。
   - 处置：默认保持 `porter unicode61`，只对 CJK/混合语料显式选择 `trigram`。
4. 回退策略：如果共享 target 接线在 Build 初期未就绪，只允许使用测试夹具继续做 FTS5 单测与 smoke，禁止临时切换到新 engine 或重新打开“SQLite FTS5 / 自研倒排 / 三方库”三选一讨论。

## 9. 收敛结论

1. KNO-TODO-001 已形成唯一 lexical 技术路线：SQLite FTS5 + 共享静态 sqlite target + 显式 tokenizer profile。
2. `KNO-BLK-001` 的“选型未定”部分可以关闭；后续剩余工作属于 Build 接线与实现验证，而不是继续讨论 engine 候选。
3. 进入 KNO-TODO-013 / 019 / 020 时，默认以本文件为 lexical 设计基线，不再重复发明 engine、manifest 或 tokenizer 口径。
