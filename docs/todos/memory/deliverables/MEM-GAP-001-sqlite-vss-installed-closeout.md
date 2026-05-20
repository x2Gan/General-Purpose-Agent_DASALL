# MEM-GAP-001 sqlite-vss installed authoritative closeout

日期：2026-05-20
来源任务：MEM-GAP-001
状态：Done

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-001`，不合并 qemu / autopkgtest rerun、release artifact archive 或 L6 soak。
2. 任务目标是确认 `sqlite-vss` production 接线与本机 installed-package authoritative evidence 已由 `MEM-FIX-001` 和 `MEM-FIX-006` 闭合，并补成面向 GAP 的独立 closeout 记录。
3. 本轮不新增 vector 产品能力，不修改 shared contracts，不使用 qemu / kvm。

## 2. 本地证据

| 证据面 | 当前状态 | 判定 |
|---|---|---|
| production vector wiring | `memory/src/vector/SqliteVssVectorBackend.*` 已承载 real sqlite-vss loadable extension driver，`MemoryManagerFactory` 已补 sqlite writer pre-open lifecycle | production manager 不再只停留在 unavailable / seam-only path |
| install layout | 当前树 package/install 流程会产出 `/usr/lib/dasall/sqlite-vss/vector0.so`、`/usr/lib/dasall/sqlite-vss/vss0.so` 与 `/usr/share/dasall/sql/memory/V002__vector_sidecar.sql` | installed package 已携带真实 vector extension assets 与 sidecar migration |
| installed smoke | `scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 已固定 same-session 双轮 run、WAL / turn / summary / sidecar proof，并生成 `memory-proof.json` | 本机 authoritative installed smoke 已覆盖 vector-enabled memory 正向链路 |
| installed DB evidence | `/var/lib/dasall/memory/memory.db` 已记录 `journal_mode=wal`、core tables、`memory_vector_documents` sidecar rows，load extension 后 `vss_search` 可命中 `cli-run-llm-response` | sqlite-vss 不再只是 build-tree fixture，而有本机安装态 real search evidence |

## 3. 外部参考

1. `sqlite-vss` 官方文档说明 Linux 安装态应先加载 `vector0.so` 再加载 `vss0.so`，并通过 `vss_search(...)` 在虚表上执行近邻查询。这与 DASALL 采用 package-installed shared objects + SQLite sidecar table 的接线方式一致。
2. SQLite 的 run-time loadable extensions 文档说明扩展以 shared library 形式分发，通过 `sqlite3_load_extension()` 或 `load_extension()` 装载；因此把 sqlite-vss 作为安装态共享库资产发布，并由应用显式启用加载，是符合 SQLite 官方扩展模型的方案。

## 4. Design -> Build 映射

| Design 判定 | Build 三件套 |
|---|---|
| vector backend 必须以真实 loadable extension 进入 production path | 代码目标：复用 `SqliteVssVectorBackend`、`MemoryManagerFactory`、`V002__vector_sidecar.sql` 与 package install layout；本轮不新增产品代码 |
| 首轮安装态 context 不得因 fresh empty real index 回落为 `vector_query_unavailable` | 测试目标：`MemoryContextAssembleIntegrationTest`、`SqliteVssVectorBackendTest`、`VectorMemoryAdapterTest` |
| GAP closeout 必须以本机 installed authoritative evidence 为准，不再把 qemu guest-side 作为能力 blocker | 验收命令：聚焦 CTest + 本机 `pkg_smoke_install.sh --explicit-start-check`；若需要 SQLite 侧核对，再查询安装态 `memory.db` 与 sqlite-vss search hit |

## 5. D Gate

结果：PASS。

1. 范围单一：只处理 `MEM-GAP-001`。
2. 设计边界清楚：本机 installed authoritative evidence 已闭合；qemu / soak 仅保留给 packaging / release 层，不再回流为 memory owner blocker。
3. Build 三件套已锁定：聚焦测试、installed smoke 与 DB evidence 均可二值判断。

## 6. 验证结果

1. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(SimpleLocalEmbeddingAdapterTest|VectorMemoryAdapterTest|SqliteVssVectorBackendTest|SchemaMigrationTest|MemoryWritebackIntegrationTest|MemoryProfileCompatibilityTest|MemoryContextAssembleIntegrationTest|GatewayRuntimeLiveDependencyCompositionTest|RuntimeProductionHealthCompositionTest)$'`
	- 结果：本轮复验通过；9 项 focused tests 全绿。
2. `DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
	- 结果：本机 installed smoke 通过，安装态 memory/vector evidence 可重复成立。
3. installed DB 只读复核：通过 `sudo` + `python3` 连接 `/var/lib/dasall/memory/memory.db`，验证 `PRAGMA journal_mode=wal`、`memory_vector_documents=2`、`turns=482`、`summaries=3`，并成功 load `/usr/lib/dasall/sqlite-vss/vector0.so` 与 `/usr/lib/dasall/sqlite-vss/vss0.so`；对 `memory_vector_index` 的 `embedding` 列执行最小 `vss_search` 返回命中 `rowid=1`。

## 7. 完成判定

`MEM-GAP-001` 已关闭。

1. `sqlite-vss` production wiring、安装态 asset layout、same-session writeback 与 real search hit 已具备本机 authoritative evidence。
2. `MEM-FIX-001` 的完成条件应固定在 real local installed scope，而不是继续被 qemu guest-side 证据阻塞。
3. 本结论不外推为 qemu / release runner guest-side rerun 或 L6 soak；这些更高层证据继续留在 packaging / release owner。