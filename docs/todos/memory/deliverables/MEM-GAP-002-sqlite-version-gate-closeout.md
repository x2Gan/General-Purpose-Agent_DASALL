# MEM-GAP-002 SQLite version gate closeout

来源任务：MEM-GAP-002
完成日期：2026-05-20
关联修复：MEM-FIX-002

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-002`，不合并 `MEM-GAP-001` 的 installed sqlite-vss evidence，也不外推到 qemu / soak / release guest-side rerun。
2. authoritative 问题定义固定为：Memory 对 SQLite 最低版本的编译 pin、运行时 `sqlite3_libversion_number()` gate 与 fail-closed 错误映射是否仍与详设一致。
3. 若本轮复验通过，则 `MEM-GAP-002` 保持已闭合；若复验失败，才回到 `StorageConfig.sqlite_min_version`、`SqliteMemoryStore::open()` 与 SQLite pin 的实现面。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| SQLite pin | `memory/CMakeLists.txt` 已在 `MEM-FIX-002` 中升级到 `sqlite-autoconf-3510300`，并把 pin 注入 `dasall_sqlite3` 编译命令以触发重编译 | 版本要求不再只停留在文档或缓存构建假设 |
| runtime gate | `StorageConfig.sqlite_min_version`、`encode_sqlite_version_number()` 与 `SqliteMemoryStore::open()` 已对齐 3.51.3 / 3051003，并在低版本场景 fail-closed 映射为 `ConfigInvalid` | 运行时不会静默接受错误或过低的 SQLite 动态库 |
| focused validation | `MEM-FIX-002` 已记录 `SqliteVersionGateTest`、`SqliteMemoryStoreTest`、`MemoryFailureInjectionTest` 与 `gdb` 运行时版本复核 | 本轮只需要做最小当前树复验并把 gap 级交付件补齐 |

## 3. 外部参考

1. SQLite 官方 `changes.html` 说明 `3.51.3` 于 2026-03-13 发布。
2. SQLite 官方 `wal.html` 的 WAL-reset bug 说明明确指出：该问题影响 `3.7.0` 到 `3.51.2`，并在 `3.51.3` 及以上版本修复。因此 Memory 将最低基线固定为 `3.51.3` 不是随意抬升，而是 WAL 模式下的生产安全下限。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| SQLite 最低版本必须固定为 3.51.3，且版本变更能够触发真正重编译 | 测试目标：`SqliteVersionGateTest` |
| `SqliteMemoryStore::open()` 必须在运行时对低版本库 fail-closed，而不是把错误降级成 retryable storage failure | 测试目标：`SqliteMemoryStoreTest`、`MemoryFailureInjectionTest` |
| GAP closeout 必须以当前树 focused validation 与本机真实开发机 runtime probe 为准，不依赖 qemu / kvm | 验收命令：聚焦 CTest + 运行时 `sqlite3_libversion_number()` 只读复核 |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-002`。
2. 依赖方向不变：Memory 自行约束 SQLite store/version gate，不向 runtime / llm / tools 反向泄漏 owner 权。
3. 本轮不修改产品代码；若验证失败，才回到对应实现面补修。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_sqlite_version_gate_unit_test dasall_memory_sqlite_store_unit_test dasall_memory_failure_injection_integration_test -j4`
	- 结果：通过；3 个 focused targets 均构建成功。
2. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(SqliteVersionGateTest|SqliteMemoryStoreTest|MemoryFailureInjectionTest)$'`
	- 结果：通过；`100% tests passed, 0 tests failed out of 3`。
3. `gdb -batch -ex "start" -ex "print (int)sqlite3_libversion_number()" build/vscode-linux-ninja/tests/unit/memory/dasall_memory_sqlite_version_gate_unit_test`
	- 结果：运行时 `sqlite3_libversion_number()` 打印为 `3051003`，与文档约束的 SQLite `3.51.3` 完全一致。

## 7. 完成判定

1. `MEM-GAP-002` 已关闭。
2. SQLite 最低版本 pin、运行时 version gate 与 fail-closed 行为在当前树上复验通过，不再依赖历史 worklog 单点描述。
3. 本结论不外推为 qemu / soak / release guest-side evidence；这些不属于 `MEM-GAP-002` 的 owner 边界。