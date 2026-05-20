# MEM-GAP-005 reader pool concurrency closeout

来源任务：MEM-GAP-005
完成日期：2026-05-20
关联修复：MEM-FIX-004

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-005`，不合并 `MEM-GAP-002` 的 version gate，也不把 TSAN / soak 之类可选增强混入本轮。
2. authoritative 问题定义固定为：SQLite reader pool 是否已经从“裸 `sqlite3*` 跨线程复用”收口到 atomic round-robin + per-slot lease，并在 store / manager 两个切面有 focused evidence。
3. 若本轮复验通过，则 `MEM-GAP-005` 保持已闭合；若复验失败，才回到 `SqliteMemoryStore` reader lease 与 `prepare_context()` 并发读取路径。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| reader lease | `SqliteMemoryStore::select_reader_connection() const` 已收口为 atomic round-robin + per-slot lease mutex | 多线程只读路径不再裸共享同一 SQLite 连接 |
| store-level gate | `SqliteMemoryStoreConcurrencyTest` 已覆盖单 reader pool 下第二个借用者等待前一个 lease 释放 | reader pool 并发语义有直接测试门 |
| manager-level gate | `MemoryContextIntegrationTest` 已覆盖 `reader_pool_size=1` 下并发 `prepare_context()` | manager 级调用链不会绕过 store lease 保护 |

## 3. 外部参考

1. SQLite 官方 threading 文档说明：在 multi-thread 模式下，多个线程可以安全使用 SQLite，但“同一个 database connection 及其派生对象不能同时被两个或更多线程使用”。Memory 本轮通过 reader slot lease 显式避免并发共享同一连接，与这一约束一致。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| reader pool 必须按次借用 slot，而不是跨线程复用裸连接 | 测试目标：`SqliteMemoryStoreConcurrencyTest` |
| `prepare_context()` 在单 reader pool 配置下也必须并发稳定 | 测试目标：`MemoryContextIntegrationTest` |
| GAP closeout 必须以当前树 focused concurrency validation 为准，不依赖 qemu / soak | 验收命令：聚焦 CTest |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-005`。
2. 依赖方向不变：并发防护停留在 Memory SQLite store / manager 内部，不向 runtime / llm 泄漏额外 owner 语义。
3. 本轮不修改产品代码；若验证失败，才回到并发实现面补修。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_sqlite_store_concurrency_unit_test dasall_memory_context_integration_test -j4`
	- 结果：通过；2 个 focused targets 均构建成功。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(SqliteMemoryStoreConcurrencyTest|MemoryContextIntegrationTest)$'`
	- 结果：通过；`100% tests passed, 0 tests failed out of 2`。

## 7. 完成判定

1. `MEM-GAP-005` 已关闭。
2. SQLite reader pool lease 与 manager 级并发调用门在当前树上复验通过，多线程 `prepare_context()` 与只读查询不再裸共享同一连接。
3. 本结论不外推到 TSAN / soak；这些属于可选增强或更高层验证，不属于 `MEM-GAP-005` 的功能闭环定义。