# KNO-TODO-018 VersionLedger 设计收敛

## 1. 输入与目标

1. 来源任务：`KNO-TODO-018 | 实现 VersionLedger snapshot 账本`。
2. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 6.13.4 `VersionLedger` 卡片、10.4 snapshot-and-swap 一致性规则与 7 KNO-D05。
3. 前置满足：`KNO-TODO-006` 已冻结 `KnowledgeErrors` / `KnowledgeTypes` 公共语义，`KNO-TODO-016/017/026` 已把 catalog / freshness / health 的只读边界落盘，`KNO-TODO-013` 已明确 019 `IndexReader` 仍是 active snapshot 的 concrete owner。
4. 本轮目标：落盘 snapshot lineage、candidate/active/superseded 状态流转、`last_known_good()` 与 checksum 失配拒绝路径，同时避免在 018 内抢跑 019/020 的 snapshot 读写 owner 与持久化文件格式。

## 2. 设计结论

### 2.1 边界与职责

1. `VersionLedger` 负责：
   - 记录 snapshot entry 的 append-only lineage；
   - 固化 `record_candidate -> mark_active -> superseded/rollback target` 的状态机；
   - 为恢复与 health 路径提供 `last_known_good()` 的选择逻辑。
2. `VersionLedger` 不负责：
   - 构建或切换 SQLite active snapshot；
   - 持有查询面读引用；
   - 直接访问文件系统或决定 JSONL / SQLite 持久化介质；
   - 发起 audit sink、catalog refresh 或 freshness 裁定。

### 2.2 接口收敛

1. `VersionLedgerEntry` 保持与详设一致，显式携带：
   - `snapshot_id` / `parent_snapshot_id` / `batch_id`
   - `built_at` / `activated_at`
   - `SnapshotState`
   - `document_count` / `chunk_count`
   - `checksum`
   - `rollback_eligible`
2. 因 018 尚未拥有真实 snapshot storage owner，本轮新增 `VersionLedgerDeps::read_snapshot_checksum`：
   - 018 只负责“账本记录的 checksum 是否可信”；
   - 真实 snapshot checksum 的读取由后续 019/020 接到同一 seam。
3. `record_candidate()` 额外固定两条输入纪律：
   - candidate 必须是 `Pending`，不得预先带 `activated_at` 或 `rollback_eligible=true`；
   - 若声明 `parent_snapshot_id`，则 lineage 父节点必须已存在。

### 2.3 状态机与回退语义

1. 状态流转固定为：
   - `record_candidate(entry)`：新增 `Pending` entry；
   - `mark_active(snapshot_id, activated_at)`：目标 entry 进入 `Active`，前一 active 自动转 `Superseded + rollback_eligible=true`；
   - `mark_superseded(snapshot_id)`：仅允许已激活 snapshot 进入 `Superseded`，禁止把 `Pending` candidate 伪装成 superseded。
2. `last_known_good()` 选择规则：
   - 优先最新 `Active`；
   - 若当前 active checksum 失配，则回退到最近一个 `rollback_eligible=true` 的 `Superseded`；
   - 若 verifier 缺失、读取失败或 checksum 不一致，则 fail-closed，不把该 snapshot 视为可回退目标。

### 2.4 实现取舍

1. v1 本地实现采用进程内 append-only entry 列表，而不提前落盘 JSON Lines / SQLite 物理格式。
2. 这样 018 可以先把 lineage/state/rollback contract 收紧；020 再把同一对象接到 shadow build、snapshot swap 和持久化介质，不需要推翻 018 的 public shape。
3. checksum 比对依赖注入而不是直接读文件，避免把 018 与 019 `IndexReader`、020 `IndexWriter` 的 owner 方向缠死。

## 3. Design -> Build 映射

1. `knowledge/include/index/VersionLedger.h`
   - 定义 `SnapshotState`、`VersionLedgerEntry`、`VersionLedgerDeps` 与 `VersionLedger`。
2. `knowledge/src/index/VersionLedger.cpp`
   - 实现 candidate/active/superseded 状态流转、lineage 校验与 `last_known_good()` 选择逻辑。
3. `tests/unit/knowledge/VersionLedgerTest.cpp`
   - 验证 candidate 录入、重复 snapshot 拒绝与 parent lineage 约束。
4. `tests/unit/knowledge/VersionLedgerActivationTest.cpp`
   - 验证 `mark_active()` 激活、前一 active 自动 supersede 与 active checksum 失配回退。
5. `tests/unit/knowledge/VersionLedgerRollbackEligibilityTest.cpp`
   - 验证 pending 不得成为 rollback target、显式 supersede 后的 eligibility 与 checksum 失配拒绝。
6. `knowledge/CMakeLists.txt`、`tests/unit/knowledge/CMakeLists.txt`
   - 注册 `VersionLedger` 源文件、头文件与 3 条 unit tests。

## 4. 验证计划

1. build-ci configure：`cmake -S . -B build-ci -G "Unix Makefiles"`
2. 定向构建：
   - `cmake --build build-ci --target dasall_knowledge dasall_version_ledger_unit_test dasall_version_ledger_activation_unit_test dasall_version_ledger_rollback_eligibility_unit_test`
3. 定向 `ctest`：
   - `ctest --test-dir build-ci -R "VersionLedger.*Test" --output-on-failure`

## 5. 完成判定

1. `VersionLedger` 能稳定表达 candidate/active/superseded 三类状态与 lineage 约束。
2. `mark_active()` 会把前一 active 变成 rollback target，而不是丢失 last-known-good。
3. pending candidate 不会被误当成可回退 snapshot。
4. checksum 缺失或失配时，`last_known_good()` 必须 fail-closed。