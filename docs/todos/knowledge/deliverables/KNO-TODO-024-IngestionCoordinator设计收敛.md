# KNO-TODO-024 IngestionCoordinator 设计收敛

## 1. 背景与约束

### 1.1 本地设计依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md` 已冻结 `IngestionCoordinator` 的职责边界、`IndexUpdateBatch` shape、`build_update_batch / scan_and_canonicalize / build_chunk_records` 接口名与单源失败 warning 语义。
2. `docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` 已冻结 trusted corpus、typed metadata 与 quarantine 基线。
3. `knowledge/include/ingest/SourceScanner.h`、`Canonicalizer.h`、`Chunker.h` 已分别实现真实 scan / canonicalize / chunk 责任，因此 024 只负责编排，不回头修改三者对象面。

## 2. 设计结论

1. 024 的输出固定为 `IndexUpdateBatch`，只承载 chunk records、removed document refs 与 warning 聚合；不直接写索引、不切换 active snapshot。
2. selective refresh 的最小粒度收敛为“按受影响 corpus root 编排”，而不是在 024 重新发明文件级 watcher；`CorpusChangeSet` 非空时，只扫描其 source URI 落入的 trusted corpora。
3. `CorpusChangeSet` 为空时，024 按当前 `CorpusCatalogSnapshot` 做 trusted corpora full scan；这条路径用于 init 阶段冷启动或 runtime timer 的保底刷新。
4. 单个 source 失败必须 fail-soft：
   - `SourceScanner` quarantine 转成 batch warning；
   - `Canonicalizer` warning / quarantine 转成 batch warning；
   - 其他 source 继续进入 chunk 流程。
5. 因 017 `VersionLedger` 尚未落盘，024 需要一个稳定 replacement key 来表达“同一 source 的旧文档需要被移除”。本轮固定：
   - `document_lineage_id = sha256(source_id)` 的 deterministic opaque key；
   - 每个 chunk record 的 `metadata.document_lineage_id` 写入该 key；
   - `IndexUpdateBatch.removed_document_ids` 暂存同一 lineage key，供 020 `IndexWriter` 后续解释为 remove-by-lineage。
6. `batch_id` 采用 `chunk_records + removed_document_ids + warnings` 的 deterministic hash，便于 032/033 后续做 refresh traceability，而不依赖 wall clock。

## 3. 数据与接口

### 3.1 `IndexUpdateBatch`

字段：

1. `batch_id`
2. `chunk_records`
3. `removed_document_ids`
4. `warnings`

约束：

1. `batch_id` 不为空。
2. `chunk_records` 必须全部满足 `ChunkRecord::has_consistent_values()`。
3. `removed_document_ids` / `warnings` 需要去重且不能为空字符串。

### 3.2 `IngestionCoordinatorDeps`

字段：

1. `load_catalog_snapshot`
2. `load_inventory`
3. `repository_root`
4. `now_ms`

规则：

1. 024 只依赖当前 catalog snapshot 与 source inventory，不直接持有 `IndexWriter` / `VersionLedger`。
2. 所有依赖都通过 seam 注入，避免把 repo IO 或 runtime 主控直接塞进 coordinator。

## 4. 流程

1. 校验 `CorpusChangeSet` 与 injected deps。
2. 根据 change set 选择 trusted corpora：空 change set → full scan；非空 change set → 只选匹配 source root 的 corpora。
3. 对每个选中的 corpus：
   - 委托 `SourceScanner` 产出 `added/updated/removed/quarantined`；
   - 对 `added/updated` 委托 `Canonicalizer`，收敛 warning / quarantine；
   - 为 `updated/removed/quarantined` source 生成 stable lineage removal refs。
4. 把 canonical documents 委托给 `Chunker` 产出 chunk records，并补写 `metadata.document_lineage_id`。
5. 聚合 warning、removed refs 与 chunk records，生成 deterministic `batch_id`。

## 5. 文件范围

1. `knowledge/include/ingest/IngestionCoordinator.h`
2. `knowledge/src/ingest/IngestionCoordinator.cpp`
3. `tests/unit/knowledge/IngestionCoordinatorTest.cpp`
4. `tests/unit/knowledge/IngestionCoordinatorSelectiveRefreshTest.cpp`
5. `tests/unit/knowledge/IngestionCoordinatorBadSourceTest.cpp`
6. `knowledge/CMakeLists.txt`
7. `tests/unit/knowledge/CMakeLists.txt`

## 6. 测试矩阵

1. `IngestionCoordinatorTest`：验证 full scan 下 batch 组装、removed lineage refs 与 deterministic metadata 注入。
2. `IngestionCoordinatorSelectiveRefreshTest`：验证非空 `CorpusChangeSet` 只刷新受影响 corpora，而不是回退成全量扫描。
3. `IngestionCoordinatorBadSourceTest`：验证空正文/坏 source 只写 warning，不中断同批次其他 source 的 chunk 组装。

## 7. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_ingestion_coordinator_unit_test dasall_ingestion_coordinator_selective_refresh_unit_test dasall_ingestion_coordinator_bad_source_unit_test
ctest --test-dir build-ci -R "IngestionCoordinator.*Test" --output-on-failure
```