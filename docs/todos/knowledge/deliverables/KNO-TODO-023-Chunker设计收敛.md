# KNO-TODO-023 Chunker 设计收敛

## 1. 背景与约束

### 1.1 本地设计依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md` 已冻结 `Chunker` 的职责、`ChunkRecord` 字段、`split_into_spans` / `build_chunk_id` 接口和空正文/forced split 语义。
2. `docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` 已冻结 `ChunkRecord` 的 typed provenance 继承规则，以及 `metadata.document_class` / `metadata.section_path` 的最低保留要求。
3. `knowledge/include/ingest/Canonicalizer.h` 已提供稳定 `CanonicalDocument` 输入，因此 023 只负责 chunk 边界、overlap、citation span 与 stable chunk id，不再回头做 canonicalize。

## 2. 设计结论

1. 022 已把同语义 markdown/yaml 输入收敛成稳定 `canonical_text`，因此 023 的 `chunk_id` 固定建立在 `document_id + version + policy + span + chunk_text` 上，避免 policy 漂移或边界变化时出现假稳定。
2. v1 默认执行 `ChunkStrategy::FixedSize`，但对象面保留 `Semantic` / `DocumentAware` 扩展槽位；当前三种策略都走同一 deterministic splitter，后续升级不破坏 ABI。
3. `split_into_spans` 采用“标题/空行边界优先，超长段落再 forced split”的顺序：
   - heading 起点与 blank-line 序列末尾作为优先 breakpoint；
   - 若在 `target_chunk_chars` 范围内找不到 breakpoint，则退到 `max_chunk_chars` 范围内的 soft break；
   - 再找不到时按 `max_chunk_chars` 强制切分。
4. overlap 采用 char-span 语义，`next_span.begin = previous_end - overlap_chars` 后再向前方单词边界轻量校正，保证 deterministic，同时尽量避免从半个单词开始。
5. `citation_ref` 固定为 `source_uri#char=<begin>-<end>`；`adjacent_chunk_refs` 保存相邻 chunk 的 `chunk_id`，为 sentence-window / evidence stitching 预留稳定邻接线索。
6. provenance fail-closed 规则：
   - 只要 `document_id`、`version`、`source_uri`、`authority_level`、`metadata.document_class`、`metadata.section_path` 等继承字段不完整，就拒绝 chunk；
   - `canonical_text` 为空时返回空列表，作为 defensive no-op；
   - 非法 `ChunkPolicy` 直接构造失败，不静默修正。

## 3. 数据与接口

### 3.1 `ChunkPolicy`

字段：

1. `strategy`
2. `target_chunk_chars`
3. `max_chunk_chars`
4. `overlap_chars`
5. `min_chunk_chars`

约束：

1. `target_chunk_chars > 0`
2. `max_chunk_chars >= target_chunk_chars`
3. `overlap_chars < max_chunk_chars`
4. `min_chunk_chars <= target_chunk_chars`

### 3.2 `ChunkRecord`

字段：

1. `chunk_id`
2. `document_id`
3. `corpus_id`
4. `source_id`
5. `source_uri`
6. `chunk_text`
7. `version`
8. `updated_at_ms`
9. `source_format`
10. `authority_level`
11. `language`
12. `token_estimate`
13. `span_begin`
14. `span_end`
15. `citation_ref`
16. `tags`
17. `adjacent_chunk_refs`
18. `metadata`

规则：

1. provenance typed 字段必须从 `CanonicalDocument` 继承。
2. `metadata` 至少保留 `document_class`、`section_path`。
3. `chunk_text` 不允许为空；空正文的合法表现是返回空 chunk 列表。

## 4. 流程

1. 校验 `ChunkPolicy` 与 `CanonicalDocument` 的 provenance 完整性。
2. 从 `canonical_text` 收集 heading / blank-line breakpoints。
3. 依据 `target_chunk_chars` / `max_chunk_chars` / `overlap_chars` 生成稳定 `TextSpan`。
4. 为每个 span 计算 `chunk_text`、`token_estimate`、`citation_ref`、`chunk_id`。
5. 回填 `adjacent_chunk_refs` 并输出 `ChunkRecord` 序列。

## 5. 文件范围

1. `knowledge/include/ingest/Chunker.h`
2. `knowledge/src/ingest/Chunker.cpp`
3. `tests/unit/knowledge/ChunkerTest.cpp`
4. `tests/unit/knowledge/ChunkerStableIdTest.cpp`
5. `tests/unit/knowledge/ChunkerBoundaryFallbackTest.cpp`
6. `knowledge/CMakeLists.txt`
7. `tests/unit/knowledge/CMakeLists.txt`

## 6. 测试矩阵

1. `ChunkerTest`：验证段落/标题边界优先切分、provenance 继承和相邻 refs 回填。
2. `ChunkerStableIdTest`：验证同一 canonical document 在同一 policy 下得到稳定 chunk ids。
3. `ChunkerBoundaryFallbackTest`：验证空正文返回空列表，以及超长段落 forced split 时 span 连续且 overlap 稳定。

## 7. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_chunker_unit_test dasall_chunker_stable_id_unit_test dasall_chunker_boundary_fallback_unit_test
ctest --test-dir build-ci -R "Chunker.*Test" --output-on-failure
```