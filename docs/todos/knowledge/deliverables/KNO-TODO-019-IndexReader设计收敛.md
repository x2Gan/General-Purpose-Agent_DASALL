# KNO-TODO-019 IndexReader 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-019 | 实现 IndexReader active snapshot 读路径`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.3 `IndexReader` 卡片、并发安全模型与 10.4 snapshot-and-swap 一致性规则；补充锚点为 `KNO-TODO-001` 的 lexical snapshot 冻结结论。
3. 前置满足：`KNO-TODO-013` 已把 lexical search 收敛为 `SparseIndexSearchRequest/Result` seam，`KNO-TODO-018` 已提供 `VersionLedger` 与 checksum verifier seam，`KNO-TODO-006` 已冻结错误语义。
4. 本轮目标：落盘 active snapshot 的原子读取、manifest 查询与 lexical search 只读路径，同时避免在 019 内抢跑 020 `IndexWriter` 的 shadow build / ledger record / swap orchestration。

## 2. 设计结论

### 2.1 边界与职责

1. `IndexReader` 负责：
   - 持有 active snapshot 的只读引用；
   - 以 acquire/release 语义完成 lock-free `shared_ptr` 读取与切换；
   - 对外提供 `current_manifest()`、`search_sparse()` 与 active snapshot checksum 查询。
2. `IndexReader` 不负责：
   - 构建 shadow SQLite snapshot；
   - 执行 `record_candidate -> swap -> mark_active` 顺序编排；
   - 自行决定 last-known-good 或 refresh/rebuild 触发。

### 2.2 接口收敛

1. 详细设计卡片仍把 `search_sparse()` 简写为 `vector<RecallHit> search_sparse(const RetrievalPlan&)`，但 013 已把 lexical query/search filter 收敛为 knowledge-owned seam。
2. 为避免在 019 回退到旧签名，本轮 `IndexReader` 直接复用 013 的输入输出：
   - `retrieve::SparseIndexSearchRequest`
   - `retrieve::SparseIndexSearchResult`
3. 新增 `IndexSnapshot` supporting object：
   - `IndexManifest manifest`
   - `std::string checksum`
   - `std::function<SparseIndexSearchResult(const SparseIndexSearchRequest&)> search`
4. 新增 writer-facing `swap_active_snapshot(std::shared_ptr<const IndexSnapshot>)`：
   - 019 只负责原子替换 active snapshot 指针；
   - 020 再负责何时调用它，以及如何与 018 `VersionLedger` 协同。

### 2.3 并发与失败语义

1. 读路径统一使用 `std::atomic_load_explicit(..., std::memory_order_acquire)` 获取 `shared_ptr<const IndexSnapshot>` 副本。
2. 写路径统一使用 `std::atomic_store_explicit(..., std::memory_order_release)` 原子替换 active snapshot。
3. 因读侧持有的是 `shared_ptr` 副本，search 开始后即使发生 swap，当前读也继续使用旧 snapshot，保持 MVCC 语义。
4. 失败纪律固定为：
   - 无 active snapshot：返回 `IndexUnavailable`；
   - active snapshot 结构不一致：返回 `IndexUnavailable`；
   - search callback 返回不一致结果：返回 `InternalError`；
   - search callback 抛异常：返回 `IndexUnavailable`。

### 2.4 与 018/020 的协作边界

1. `current_manifest()` 与 `read_snapshot_checksum()` 只读取当前 active snapshot，不尝试提供 superseded snapshot 存储。
2. 这保证了 019 仍是读路径 owner，而不是回退 last-known-good 的完整存储 owner。
3. 020 后续只需：
   - 构造新的 `IndexSnapshot`；
   - 先调用 018 `record_candidate()`；
   - 再调用 019 `swap_active_snapshot()`；
   - 最后调用 018 `mark_active()`。

## 3. Design -> Build 映射

1. `knowledge/include/index/IndexReader.h`
   - 定义 `IndexSnapshot` 与 `IndexReader` public shape。
2. `knowledge/src/index/IndexReader.cpp`
   - 实现 atomic load/store、manifest 查询、checksum 查询与 search 失败归一化。
3. `tests/unit/knowledge/IndexReaderTest.cpp`
   - 验证 manifest 查询、search 透传与 active checksum 查询。
4. `tests/unit/knowledge/IndexReaderConcurrentSwapTest.cpp`
   - 验证旧读持有旧 snapshot，新读切到新 snapshot 的 MVCC 语义。
5. `tests/unit/knowledge/IndexReaderNoActiveSnapshotTest.cpp`
   - 验证 bootstrap / 无 active snapshot 场景显式失败。
6. `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`
   - 注册 `IndexReader` 头文件、源文件与 3 条 unit tests。

## 4. 验证计划

1. Build_CMakeTools：
   - `dasall_knowledge`
   - `dasall_index_reader_unit_test`
   - `dasall_index_reader_concurrent_swap_unit_test`
   - `dasall_index_reader_no_active_snapshot_unit_test`
2. build-ci 回退路径：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge dasall_index_reader_unit_test dasall_index_reader_concurrent_swap_unit_test dasall_index_reader_no_active_snapshot_unit_test`
   - `ctest --test-dir build-ci -R "IndexReader.*Test" --output-on-failure`

## 5. 完成判定

1. `IndexReader` 已把 active snapshot 原子读取、manifest 查询与 lexical search 只读路径固化成可测试 contract。
2. 无 active snapshot 时显式返回 `IndexUnavailable`，而不是 silent empty hit。
3. concurrent swap 场景下，旧读不被新 swap 打断，新读可立即看到新 snapshot。
4. 019 交付后，020 只需补 shadow build / ledger record / swap orchestration，不需要再重定义 sparse search seam。