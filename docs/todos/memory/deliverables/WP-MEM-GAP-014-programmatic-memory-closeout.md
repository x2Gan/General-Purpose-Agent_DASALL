# WP-MEM-GAP-014 ProgrammaticMemory closeout

来源任务：WP-MEM-GAP-014
关联缺口：GAP-P3-A / MEM-E06 ProgrammaticMemory 持久化
完成日期：2026-06-23

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-014`，不把 `WP-MEM-GAP-015` 的 mini-store 收敛、`WP-MEM-GAP-016` 的 shared contracts 提升或 `WP-MEM-GAP-018` 的 soak gate 混入同一轮。
2. authoritative 问题定义固定为：Memory 需要为 Prompt/Skill 之类 programmatic assets 持久化稳定 `asset_ref + digest + lease`，但不能复制 Prompt 正文，也不能让 memory 直接依赖 llm 私有实现。
3. owner 边界保持不变：Prompt 资产内容与 catalog owner 仍归 llm；Runtime 负责把已选中的 Prompt release 投影为 writeback request；Memory 只负责 ProgrammaticMemory 的持久化、lease 更新与后续查询。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-014` 固定为“`IProgrammaticMemoryStore` + `ProgrammaticMemoryRecord` + V005 `programmatic_assets` + `ProgrammaticMemoryAssetRefTest` + `SchemaMigrationV005Test`”。
2. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.5.1a / §12.3 明确 `ProgrammaticMemory` 只能保留 `asset ref / lease` 语义，禁止复制 Prompt/Skill 正文；这直接约束了本轮 schema 和 public surface 的最小字段集。
3. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm子系统详细设计.md) §6.15.5 将 `PromptAssetRepository` 冻结为“资产装载 owner，不是运行态选择 owner”，因此 Runtime/Memory 只能消费经过 llm public seam 暴露的只读 asset metadata，不能 include `llm/src/prompt/PromptAssetDescriptor`。
4. Kubernetes Lease 文档把 Lease 定义为“用于锁定共享资源和在成员之间协调活动的 time-bounded metadata”，并通过 `renewTime` 更新来判断可用性；这支撑 DASALL 在 ProgrammaticMemory 中只保存 `lease_expires_at` 这类时限信息，而不是把完整 Prompt payload 再复制一份。
5. OCI FAQ 强调格式和运行时规范应保持 composable、minimal，并把 cryptographic primitives 用于 trust / auditing / identity；这与本轮“保存 `content_digest` 作为稳定审计锚点，而不是复制正文”保持一致。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| ProgrammaticMemory public surface 必须只暴露 `asset_ref / digest / lease / source refs`，不能回扩 shared contracts | [memory/include/IProgrammaticMemoryStore.h](../../../../memory/include/IProgrammaticMemoryStore.h)、[memory/include/IMemoryStore.h](../../../../memory/include/IMemoryStore.h)、[tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp) |
| SQLite schema 需要为 programmatic assets 提供独立表与索引，并保持与既有 session/turn 关联 | [sql/memory/V005__programmatic_assets.sql](../../../../sql/memory/V005__programmatic_assets.sql)、[tests/unit/memory/SchemaMigrationV005Test.cpp](../../../../tests/unit/memory/SchemaMigrationV005Test.cpp) |
| SqliteMemoryStore 必须支持 query / upsert / lease renew 三条基础持久化路径 | [memory/src/store/sqlite/SqliteMemoryStore.h](../../../../memory/src/store/sqlite/SqliteMemoryStore.h)、[memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)、[tests/unit/memory/SqliteMemoryStoreTest.cpp](../../../../tests/unit/memory/SqliteMemoryStoreTest.cpp) |
| ProgrammaticMemory 写回必须作为 derived write，不能回滚 core session/turn/summary transaction | [memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h)、[memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp)、[tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp) |
| Runtime 需要把已选中的 Prompt release 投影成 ProgrammaticMemory candidate，但不能越过 llm boundary | [runtime/src/PromptAssetWritebackProjector.h](../../../../runtime/src/PromptAssetWritebackProjector.h)、[runtime/src/PromptAssetWritebackProjector.cpp](../../../../runtime/src/PromptAssetWritebackProjector.cpp)、[runtime/src/AgentOrchestrator.cpp](../../../../runtime/src/AgentOrchestrator.cpp)、[tests/unit/runtime/PromptAssetWritebackProjectorTest.cpp](../../../../tests/unit/runtime/PromptAssetWritebackProjectorTest.cpp) |
| llm 必须先提供只读 prompt asset metadata seam，作为 ProgrammaticMemory 的唯一 digest/source 来源 | [llm/include/prompt/PromptAssetMetadata.h](../../../../llm/include/prompt/PromptAssetMetadata.h)、[llm/include/ILLMManager.h](../../../../llm/include/ILLMManager.h)、[tests/unit/llm/PromptAssetMetadataLookupTest.cpp](../../../../tests/unit/llm/PromptAssetMetadataLookupTest.cpp) |

## 4. 设计决策

1. `ProgrammaticMemoryRecord` 只保存 `asset_ref`、`session_id`、`source_turn_id`、`content_digest`、`lease_expires_at` 和 tags；不复制 Prompt 正文，也不把 `PromptRelease` / `PromptSpec` 推入 memory public surface。
2. ProgrammaticMemory 持久化仍走 module-local public seam：`IProgrammaticMemoryStore` 作为 `IMemoryStore` 的一部分，继续沿用 memory 既有 store 聚合模式，不额外引入新的 facade。
3. `programmatic_assets` 通过 `session_id` / `source_turn_id` 关联既有 `sessions` / `turns` 表，确保资产引用仍挂在具体回合证据上，而不是变成无来源全局缓存。
4. ProgrammaticMemory 写回被放入 `WritebackCoordinator::persist_derived_data()`：核心 `Turn/Session/Summary` 事务提交后再写 programmatic assets；失败只标记 `partial_writeback_warning`，不回滚 core transaction。
5. lease 续约采用两级语义：SQLite store 公开 `renew_programmatic_asset_lease()` 作为显式原语，而 Runtime 的正常直接响应路径通过重复 `upsert_programmatic_asset()` 自动刷新 `lease_expires_at`，从而实现与 PromptAssetRepository 选中 release 的联动续约。

## 5. D Gate

1. 设计边界明确：Memory 不复制 Prompt 正文、不 include llm 私有实现；Runtime 只做 request projection；llm 只暴露只读 metadata seam。
2. Build 三件套完整：代码目标、测试目标、验收命令均已锁定在 public surface、V005 migration、SQLite roundtrip、derived writeback 与 runtime asset-ref projection。
3. blocker 结论：本轮先行完成的 llm blocker-fix `feat(llm): expose prompt asset metadata lookup seam` 已将 `PromptAssetRepository` 的 `package_id/content_hash/source_*` 通过 public seam 暴露，主任务不再需要额外 blocker round。

## 6. 代码结果

1. 新增 [memory/include/IProgrammaticMemoryStore.h](../../../../memory/include/IProgrammaticMemoryStore.h)，冻结 `ProgrammaticMemoryRecord`、`ProgrammaticMemoryQuery`、`ProgrammaticMemoryLease` 与 `query/upsert/renew` 接口；[memory/include/IMemoryStore.h](../../../../memory/include/IMemoryStore.h) 现聚合该窄接口。
2. 新增 [sql/memory/V005__programmatic_assets.sql](../../../../sql/memory/V005__programmatic_assets.sql)，落盘 `programmatic_assets` 表及 session/lease 索引；[tests/unit/memory/SchemaMigrationTest.cpp](../../../../tests/unit/memory/SchemaMigrationTest.cpp)、[tests/unit/memory/SchemaMigrationV003Test.cpp](../../../../tests/unit/memory/SchemaMigrationV003Test.cpp)、[tests/unit/memory/SchemaMigrationV004Test.cpp](../../../../tests/unit/memory/SchemaMigrationV004Test.cpp)、[tests/unit/memory/SchemaMigrationV006Test.cpp](../../../../tests/unit/memory/SchemaMigrationV006Test.cpp) 已同步更新 migration ledger 计数到当前 V001..V006 reality。
3. 更新 [memory/src/store/sqlite/RowMappers.h](../../../../memory/src/store/sqlite/RowMappers.h) / [memory/src/store/sqlite/RowMappers.cpp](../../../../memory/src/store/sqlite/RowMappers.cpp) 与 [memory/src/store/sqlite/SqliteMemoryStore.h](../../../../memory/src/store/sqlite/SqliteMemoryStore.h) / [memory/src/store/sqlite/SqliteMemoryStore.cpp](../../../../memory/src/store/sqlite/SqliteMemoryStore.cpp)，新增 ProgrammaticMemory 的 row mapper、query、upsert 与 lease renew 路径。
4. 更新 [memory/include/writeback/MemoryWritebackRequest.h](../../../../memory/include/writeback/MemoryWritebackRequest.h)、[memory/src/writeback/WritebackCoordinator.h](../../../../memory/src/writeback/WritebackCoordinator.h)、[memory/src/writeback/WritebackCoordinator.cpp](../../../../memory/src/writeback/WritebackCoordinator.cpp) 与 [memory/src/MemoryManagerFactory.cpp](../../../../memory/src/MemoryManagerFactory.cpp)，新增 `programmatic_candidates` 写回入口，并把 programmatic asset 持久化纳入 derived write path。
5. 新增 [runtime/src/PromptAssetWritebackProjector.h](../../../../runtime/src/PromptAssetWritebackProjector.h) / [runtime/src/PromptAssetWritebackProjector.cpp](../../../../runtime/src/PromptAssetWritebackProjector.cpp)，并更新 [runtime/src/AgentOrchestrator.cpp](../../../../runtime/src/AgentOrchestrator.cpp) 与 [runtime/CMakeLists.txt](../../../../runtime/CMakeLists.txt)，让 direct LLM response path 能把 `prompt_id@prompt_version` 通过 llm public metadata seam 投影为 ProgrammaticMemory candidate。
6. 更新 [tests/mocks/include/FakeMemoryStore.h](../../../../tests/mocks/include/FakeMemoryStore.h)、[tests/unit/memory/WritebackCoordinatorCoreTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorCoreTest.cpp)、[tests/unit/memory/WritebackCoordinatorPartialTest.cpp](../../../../tests/unit/memory/WritebackCoordinatorPartialTest.cpp) 与 [tests/unit/memory/MemoryInterfaceCompileTest.cpp](../../../../tests/unit/memory/MemoryInterfaceCompileTest.cpp)，锁定新 store seam、derived writeback 行为与 public compile surface。
7. 新增 [tests/unit/memory/SchemaMigrationV005Test.cpp](../../../../tests/unit/memory/SchemaMigrationV005Test.cpp) 与 [tests/unit/runtime/PromptAssetWritebackProjectorTest.cpp](../../../../tests/unit/runtime/PromptAssetWritebackProjectorTest.cpp)，并更新 [tests/unit/memory/CMakeLists.txt](../../../../tests/unit/memory/CMakeLists.txt) 与 [tests/unit/runtime/CMakeLists.txt](../../../../tests/unit/runtime/CMakeLists.txt)；其中 runtime projector executable 同时注册为 `ProgrammaticMemoryAssetRefTest` 验收名。

## 7. 测试结果

1. `SchemaMigrationV005Test` 覆盖 fresh DB 与 V004 -> V005 upgrade 两条路径，证明 `programmatic_assets` 表与 lease/session 索引都能进入 migration ledger。
2. `SqliteMemoryStoreTest` 现覆盖 ProgrammaticMemory 的 upsert/query/lease renew roundtrip，证明 `asset_ref/content_digest/source_turn_id/lease_expires_at/tags` 会被真实 SQLite 持久化。
3. `WritebackCoordinatorCoreTest` 证明 programmatic candidate 会在 core transaction 成功后经 derived write path 持久化；`WritebackCoordinatorPartialTest` 保持 derived failure partial 语义不回退。
4. `ProgrammaticMemoryAssetRefTest` 证明 Runtime 能把 `LLMResponse.prompt_id/prompt_version` 与 llm public metadata seam 组合为 `prompt:<release_id>` 资产引用，并保留 content digest 与 lease expiry。
5. `MemoryInterfaceCompileTest` 把 ProgrammaticMemory public surface 纳入 compile regression，锁定 `IMemoryStore` 聚合关系与 supporting type 一致性。

## 8. 验收证据

1. `ctest --test-dir build-ci -R "^(ProgrammaticMemoryAssetRefTest|SchemaMigrationV005Test|SqliteMemoryStoreTest|WritebackCoordinatorCoreTest|WritebackCoordinatorPartialTest|MemoryInterfaceCompileTest)$" --output-on-failure`
   - 结果：通过，6/6。
2. `ctest --test-dir build-ci -R "^(SchemaMigrationTest|SchemaMigrationV003Test|SchemaMigrationV004Test|SchemaMigrationV005Test|SchemaMigrationV006Test)$" --output-on-failure`
   - 结果：通过，5/5。
3. blocker-fix 验证：`ctest --test-dir build-ci -R "^PromptAssetMetadataLookupTest$" --output-on-failure`
   - 结果：通过，1/1。

## 9. 结果

1. `WP-MEM-GAP-014` 已闭合；Memory 现在具备不复制 Prompt 正文的 ProgrammaticMemory 持久化基线，能够保存稳定 `asset_ref + content_digest + lease_expires_at + source_turn_id`。
2. 本轮保持了 ADR-006/007/008 边界：llm 继续拥有 Prompt 资产正文与 catalog；Runtime 只做已选中 release 的投影；Memory 只负责持久化与后续查询。
3. `GAP-P3-A / MEM-E06` 不再是 open gap；memory 当前剩余 P3 焦点收敛为 `WP-MEM-GAP-015 / -016 / -017 / -018`，其中 ProgrammaticMemory 已不再阻塞 V3 资产化路线。