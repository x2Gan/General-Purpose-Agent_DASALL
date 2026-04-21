# KNO-TODO-022 Canonicalizer 设计收敛

## 1. 背景与约束

### 1.1 本地设计依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md` 已冻结 `Canonicalizer` 的职责：把 `SourceRecord` 规范化为 `CanonicalDocument`，并负责编码/换行归一、markdown/yaml canonicalize、title 与基础 metadata 提取、stable `document_id` 生成。
2. `docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` 已冻结 `CanonicalDocument` 的必填字段、markdown/profile policy 两类 metadata baseline 以及 quarantine 规则。
3. `knowledge/include/ingest/SourceScanner.h` 已固定 `SourceRecord` 的输入语义，因此 022 只消费 scanner 输出，不回头做 source diff。

### 1.2 联网业界参考

1. CommonMark 0.31.2 明确把 heading、list、code fence 视为 markdown 的结构元素，并强调代码块内容按 literal text 处理、换行是显式结构的一部分。因此 022 的 markdown canonicalize 只能做表示归一和结构保留，不能为了“更好召回”改写正文语义或破坏代码块边界。

## 2. 设计结论

1. `Canonicalizer` 直接从 `SourceRecord.source_uri` 读取真实文件内容，不新增外部 IO seam；唯一 policy 输入是 `repository_root`，用于解析相对路径。
2. markdown canonicalize 规则固定为：
   - 去 UTF-8 BOM；
   - 统一 `CRLF/CR -> LF`；
   - 去除 front matter 后再生成 `canonical_text`；
   - 保留 heading、列表顺序和 code fence 文本，不做语义改写。
3. runtime policy yaml canonicalize 规则固定为：
   - 以稳定 key-path flatten 输出 `key=value` 行；
   - mapping key 按稳定排序输出；
   - scalar list 以 `path[index]=value` 形式保序；
   - 不支持的复杂结构直接 quarantine，禁止输出半有效文本。
4. `CanonicalDocument.version` 在 022 阶段优先使用显式版本元信息；缺失时回退到 `sha256:<canonical_text_hash>`，从而消解“仅 key 顺序变化但语义未变”的噪音 diff。
5. `document_id` 固定为 `doc:<sha256(corpus_id + source_uri + version + canonical_text)>`，保证同一输入和同一 canonicalize 策略下结果稳定。
6. metadata fail-closed 规则：
   - `document_class`、`section_path` 必须最终落地；
   - profile policy 额外要求 `profile_name`、`policy_domain=runtime_policy`；
   - 若 fallback 后仍无法补齐，则 quarantine。

## 3. 数据与接口

### 3.1 `CanonicalDocument`

字段：

1. `document_id`
2. `corpus_id`
3. `source_id`
4. `source_uri`
5. `title`
6. `canonical_text`
7. `source_hash`
8. `version`
9. `updated_at_ms`
10. `source_format`
11. `authority_level`
12. `language`
13. `tags`
14. `metadata`

约束：

1. `canonical_text` 不允许为空。
2. `source_hash` 必须保持 64 位小写十六进制。
3. markdown 文档必须具备 `metadata.document_class`、`metadata.section_path`。
4. yaml profile 文档额外必须具备 `metadata.profile_name`、`metadata.policy_domain=runtime_policy`。

### 3.2 `CanonicalizeResult`

输出：

1. `ok`
2. `document`
3. `warnings`
4. `quarantine_reason`

语义：

1. `ok=true` 时必须返回一致的 `CanonicalDocument`。
2. `ok=false` 时不得返回半有效 document，必须给出 quarantine reason。
3. fallback 允许继续的场景通过 `warnings` 暴露，不静默吞掉。

## 4. 流程

1. 读取 `SourceRecord` 并校验 provenance 基本一致性。
2. 从 `source_uri` 读取原始文件内容，统一编码与换行。
3. 按 `SourceFormat` 分派：
   - markdown：strip front matter、保留结构、提取 heading/title/tags；
   - yaml：做稳定 key-path flatten，并从 canonical text 读取 profile metadata。
4. 基于 canonical text 与 typed metadata 生成 `CanonicalDocument.version`、`document_id`。
5. 如果 canonical text 为空、metadata 必填字段补不齐或 flatten 失败，则返回 quarantine。

## 5. 文件范围

1. `knowledge/include/ingest/Canonicalizer.h`
2. `knowledge/src/ingest/Canonicalizer.cpp`
3. `tests/unit/knowledge/CanonicalizerTest.cpp`
4. `tests/unit/knowledge/CanonicalizerMarkupNormalizeTest.cpp`
5. `tests/unit/knowledge/CanonicalizerMetadataFallbackTest.cpp`
6. `knowledge/CMakeLists.txt`
7. `tests/unit/knowledge/CMakeLists.txt`

## 6. 测试矩阵

1. `CanonicalizerTest`：验证 runtime policy yaml flatten 稳定，key 顺序变化不改变 canonical text / version / document id。
2. `CanonicalizerMarkupNormalizeTest`：验证 markdown 换行、BOM、front matter 去除与 heading/list/code fence 保留。
3. `CanonicalizerMetadataFallbackTest`：验证 title/language/document_class/section_path fallback warning，以及空正文 quarantine。

## 7. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_canonicalizer_unit_test dasall_canonicalizer_markup_normalize_unit_test dasall_canonicalizer_metadata_fallback_unit_test
ctest --test-dir build-ci -R "Canonicalizer.*Test" --output-on-failure
```