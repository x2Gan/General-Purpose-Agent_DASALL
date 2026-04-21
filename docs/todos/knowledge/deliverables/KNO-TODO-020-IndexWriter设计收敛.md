# KNO-TODO-020 IndexWriter 设计收敛

## 1. 背景与约束

### 1.1 本地设计依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md` 已冻结 `IndexWriter` 的职责边界、`apply_update_batch / rebuild_all / build_shadow_index / swap_active_snapshot` 接口名与 `record_candidate -> swap -> mark_active` 生命周期顺序。
2. `docs/todos/knowledge/deliverables/KNO-TODO-001-lexical索引技术选型与PoC设计收敛.md` 已冻结 lexical snapshot 采用 SQLite FTS5、显式 `tokenizer_profile` 与不可变 snapshot + shadow build 模式。
3. `knowledge/include/index/IndexReader.h` 与 `knowledge/include/index/VersionLedger.h` 已分别提供 active snapshot 原子读路径与 snapshot 状态账本，因此 020 只负责写路径 owner，不回头改写 018/019 的职责边界。
4. `knowledge/include/ingest/IngestionCoordinator.h` 已把 refresh 输入收敛为 `IndexUpdateBatch`，其中 `removed_document_ids` 已固定表达 source-lineage replacement key，020 必须沿用这一 remove-by-lineage 语义，不能重新发明删除键。

## 2. 设计结论

1. 020 的唯一 owner 是 `IndexWriter`：
   - 消费 `IndexUpdateBatch` 或 `RebuildPlan`；
   - 在 shadow 空间构建新的 lexical snapshot；
   - 完成 `record_candidate -> swap -> mark_active`；
   - 在 activation 失败时回退到 writer 维护的 last-known-good snapshot。
2. shadow snapshot 的最小落盘形态固定为：
   - `lexical.sqlite`：SQLite FTS5 主库，承载 chunk metadata 与 lexical text；
   - `manifest.txt`：sidecar manifest，承载 snapshot 元信息；
   - 单个 snapshot 一个独立目录，不在 active 数据库上原地修改。
3. `apply_update_batch()` 采用“复制当前 active snapshot -> remove-by-lineage -> 插入新 chunks -> 生成新 manifest”的最小闭环；若当前尚无 active snapshot，则从空库冷启动构建。
4. `rebuild_all()` 采用“从 `RebuildPlan` 全量重建新 snapshot”的路径，不复用旧库内容；其产物仍然复用 020 的 activation 协议。
5. lexical search callback 绑定到 snapshot 自身：`IndexSnapshot::search` 通过只读 SQLite 查询执行 `MATCH` 检索，再在 C++ 层补做 corpus/language/tag/authority filter。这样 019 `IndexReader` 无需理解写路径细节，但 032/033 已经可以接到真实 lexical snapshot。
6. catalog refresh 在 020 内先收敛为可注入 seam：`refresh_catalog(IndexManifest)` 在 activation 成功后立即调用，用于验证 `record_candidate -> swap -> mark_active -> refresh_catalog` 顺序已经落盘；真实 `CorpusCatalog` 更新逻辑留待 032 接线。
7. 失败纪律固定：
   - batch / rebuild plan 不一致：直接 fail-closed，active snapshot 不变；
   - shadow build 失败：不记录 candidate、不切换 active；
   - `record_candidate()` 失败：不切换 active；
   - `mark_active()` 失败：若存在前一份 good snapshot，则立即回退 reader 到旧 snapshot；新 shadow 保持孤立，不污染读路径；
   - `refresh_catalog()` 返回失败：当前轮次只记 warning，不回滚已成功激活的 snapshot，因为 020 尚未引入 `CorpusCatalog` 的双向补偿协议。

## 3. 对象与数据

### 3.1 `UpdateReport`

字段：

1. `ok`
2. `snapshot_id`
3. `manifest`
4. `warnings`
5. `error`

语义：

1. `ok=true` 时必须给出一致的 manifest 与 snapshot id。
2. `ok=false` 时必须给出结构一致的 `ErrorInfo`。

### 3.2 `RebuildPlan / RebuildReport`

1. `RebuildPlan` 承载 rebuild reason、全量 `ChunkRecord`、`tokenizer_profile` 与 `vector_enabled` 标记。
2. `RebuildReport` 与 `UpdateReport` 保持同构，便于 032/033 后续直接汇总 refresh / rebuild 结果。

### 3.3 Snapshot 落盘模型

1. 主表 `chunks` 保存：`chunk_id / document_id / corpus_id / source_id / source_uri / chunk_text / citation_ref / updated_at / authority_level / language / tags / version / token_estimate / span_begin / span_end / document_lineage_id`。
2. FTS 表 `chunks_fts` 只保存 lexical text，并以 `rowid` 与 `chunks.row_id` 对齐。
3. `document_count` 以 `document_lineage_id` 去重计数；`chunk_count` 为物理 chunk 行数。

## 4. Design -> Build 映射

1. `knowledge/include/index/IndexWriter.h`
   - 定义 `UpdateReport`、`RebuildPlan`、`RebuildReport`、`IndexWriterDeps` 与 `IndexWriter` public shape。
2. `knowledge/src/index/IndexWriter.cpp`
   - 实现 SQLite shadow build、sidecar manifest、checksum 生成、snapshot swap 与 activation rollback。
3. `tests/unit/knowledge/IndexWriterTest.cpp`
   - 验证 cold-start build、ledger activation 与 active snapshot search 可用。
4. `tests/unit/knowledge/IndexWriterSnapshotSwapTest.cpp`
   - 验证 remove-by-lineage 增量替换与 active snapshot 原子切换。
5. `tests/unit/knowledge/IndexWriterRecoveryTest.cpp`
   - 验证 activation 失败后回退到前一份 good snapshot。
6. `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`
   - 注册 `IndexWriter` 头/源与三条 knowledge unit tests，并接入 SQLite target。

## 5. 测试矩阵

1. `IndexWriterTest`
   - 正例：空 reader 冷启动后可生成 active manifest、ledger active entry 与可检索 chunk。
   - 负例：隐式覆盖 `IndexUpdateBatch` 不一致时 fail-closed，不污染 active snapshot。
2. `IndexWriterSnapshotSwapTest`
   - 正例：同一 `document_lineage_id` 的旧 chunks 会被新 batch 替换。
   - 负例：旧 lexical 命中在 swap 后不再可见。
3. `IndexWriterRecoveryTest`
   - 正例：第二次 activation 失败时，reader 仍回到上一份 active snapshot。
   - 负例：失败的 shadow snapshot 不会成为当前 manifest。

## 6. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_index_writer_unit_test dasall_index_writer_snapshot_swap_unit_test dasall_index_writer_recovery_unit_test
ctest --test-dir build-ci -R "IndexWriter.*Test" --output-on-failure
```