# KNO-TODO-021 SourceScanner 设计收敛

## 1. 背景与输入约束

### 1.1 本地设计依据

1. `docs/architecture/DASALL_knowledge子系统详细设计.md` 第 6.13.5 节已经冻结 `SourceScanner` 的最小职责：枚举 source、产出 `added/updated/removed/quarantine` 四类 delta、支持 `full_scan` 语义，但不承担 canonicalize、chunk、index write。
2. `docs/todos/knowledge/deliverables/KNO-TODO-003-corpus资产与metadata-trust基线设计收敛.md` 已冻结首批 trusted corpus 基线：`docs/architecture/`、`docs/adr/`、`docs/ssot/`、`profiles/*/runtime_policy.yaml`，并要求 source provenance 至少携带 `version`、`updated_at_ms`、`authority_level`、`source_uri`。
3. `knowledge/include/KnowledgeTypes.h` 已冻结 `TrustLevel`、`AuthorityLevel`、`SourceKind`、`SourceFormat`，因此 021 只需要新增 ingest 私有对象，不改 contracts/outward ABI。

### 1.2 联网业界参考

1. OWASP GenAI Security Project / LLM Top 10 项目主页持续强调需要识别并缓解 AI/LLM 应用中的高风险输入面，包括不受信训练/知识数据带来的污染与注入风险；因此知识 ingest 首层必须保留 quarantine 通道，而不是把不可读/非 allow-list source 静默吞掉。

## 2. 设计结论

1. `SourceScanner` 直接基于真实文件系统做递归枚举、glob 过滤、格式判定与内容哈希，不引入新的虚拟文件系统抽象。
2. 外部 seam 只保留三类最小依赖：
   - `lookup_corpus(corpus_id)`：确认 corpus 已注册，且 trust 为 `Trusted`。
   - `load_inventory(corpus_id)`：读取上一轮 `SourceRecord` inventory，用于 `added/updated/removed` diff。
   - `repository_root()/now_ms()`：解决相对路径与时间回退，避免测试依赖进程工作目录与系统时钟。
3. `scan(plan)` 不允许调用端借由 plan 扩权：
   - `root_uri` 必须解析到注册 descriptor 的同一 source root；
   - `source_kind` 必须与 descriptor 一致；
   - `allowed_formats` 只能是 descriptor allow-list 的子集。
3. `SourceRecord.version` 在 021 阶段固定为 `sha256:<content_hash>`：
   - 这样可以立即满足 021 的 version/update diff 要求；
   - 022 之后若 canonicalizer 从 front matter 或规范化正文中解析出更高层版本语义，可在 canonical document 层覆写，但不能破坏当前 source-level 版本稳定性。
4. quarantine 为 source 级别结果，不让单个坏文件导致整个 scan 失败；若 corpus 本身未注册、trust 不足、plan 越权或 root 缺失，则输出 `corpus::<corpus_id>` 级 quarantine，显式 fail-closed。

## 3. 数据与接口

### 3.1 `CorpusScanPlan`

字段：

1. `corpus_id`：目标 corpus。
2. `root_uri`：source root，相对路径时以 `repository_root()` 解析。
3. `source_kind`：当前计划的 source kind。
4. `include_globs` / `exclude_globs`：文件过滤规则。
5. `allowed_formats`：本轮允许的格式集合。
6. `full_scan`：调用端显式要求全量扫描的开关。

约束：

1. `corpus_id`、`root_uri` 非空。
2. `include_globs`、`allowed_formats` 非空。
3. glob 与 format 集合不允许重复。

### 3.2 `SourceRecord`

字段：

1. `source_id = <corpus_id>::<source_uri>`，保证 inventory diff 稳定键。
2. `source_uri` 统一输出为 repo-root-relative 路径；若无法相对化则保留规范化绝对路径。
3. `content_hash` 为 64 位小写十六进制 SHA-256。
4. `version = sha256:<content_hash>`。
5. `updated_at_ms` 优先取文件 `last_write_time`，失败时回退 `now_ms()`。
6. `authority_level`、`language`、`tags` 直接继承 `CorpusDescriptor` 冻结基线。

### 3.3 `SourceScanDelta`

输出：

1. `added`
2. `updated`
3. `removed_source_ids`
4. `quarantined_source_ids`
5. `full_scan`

判定规则：

1. 当前存在、旧 inventory 不存在：`added`
2. 当前存在、旧 inventory 存在，且 `content_hash/version/updated_at_ms` 任一变化：`updated`
3. 旧 inventory 存在、当前不存在：`removed_source_ids`
4. 当前 source 因不可读、格式不在 allow-list、无法生成 provenance 被拒绝：`quarantined_source_ids`

## 4. 流程

1. `scan(plan)` 先校验 `plan.has_consistent_values()`。
2. 通过 `lookup_corpus` 读取 descriptor；若不存在、descriptor 不一致或 `trust_level != Trusted`，返回 corpus 级 quarantine。
3. 解析 `root_uri`，递归枚举常规文件。
4. 先按 include/exclude glob 过滤，再按扩展名映射 `SourceFormat`。
5. 对命中的允许格式文件读取字节串并计算 SHA-256；无法读取或无法生成稳定 provenance 的文件进入 quarantine。
6. 产出当前轮 `SourceRecord` map，并与 `load_inventory` 结果做 diff。
7. 当调用端显式要求 `full_scan` 或上一轮 inventory 为空时，`delta.full_scan=true`。

## 5. 文件范围

1. `knowledge/include/ingest/SourceScanner.h`
2. `knowledge/src/ingest/SourceScanner.cpp`
3. `tests/unit/knowledge/SourceScannerTest.cpp`
4. `tests/unit/knowledge/SourceScannerDeltaDiffTest.cpp`
5. `tests/unit/knowledge/SourceScannerQuarantineTest.cpp`
6. `knowledge/CMakeLists.txt`
7. `tests/unit/knowledge/CMakeLists.txt`

## 6. 测试矩阵

1. `SourceScannerTest`：trusted corpus + glob 匹配 + 初次扫描返回 `added/full_scan`。
2. `SourceScannerDeltaDiffTest`：覆盖 `added/updated/removed` diff。
3. `SourceScannerQuarantineTest`：覆盖格式不在 allow-list 时进入 quarantine，且不影响同轮其他 source。

## 7. 验收命令

```bash
cmake -S . -B build-ci -G "Unix Makefiles"
cmake --build build-ci --target dasall_knowledge dasall_source_scanner_unit_test dasall_source_scanner_delta_diff_unit_test dasall_source_scanner_quarantine_unit_test
ctest --test-dir build-ci -R "SourceScanner.*Test" --output-on-failure
```