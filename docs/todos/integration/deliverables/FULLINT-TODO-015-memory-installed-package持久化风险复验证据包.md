# FULLINT-TODO-015 memory installed-package 持久化风险复验证据包

日期：2026-05-12
来源任务：FULLINT-TODO-015
范围：installed Debian package 下 memory context / writeback 的 SQLite state path、迁移资产、package smoke 与真实用户态运行结果

## 1. Phase -1 任务确认

本轮只推进 `FULLINT-TODO-015`。

可执行性判定：PASS with same-round blocker fix。

1. 前置 `FULLINT-TODO-013` 已完成 fresh package build / reinstall / explicit daemon start；`FULLINT-TODO-014` 已完成 installed Knowledge 正向入口，当前工作树干净且位于 `master`。
2. `FULLINT-TODO-015` 关联阻塞项为无，但本轮真实代码检查发现一个 validation blocker：installed live dependency composition 当前把 `MemoryConfig.storage.backend` 强制设为 `Memory`，不会产生 `/var/lib/dasall` 下的持久化数据库。
3. 同一 blocker 的第二个落点是 Debian common 包当前只安装 profiles 与 LLM assets，未安装 `sql/memory` 迁移文件；即使切到 SQLite，也会在 installed daemon 初始化时因 migrations_dir 不存在而 fail-closed。
4. 该 blocker 可在本轮最小修复：使用现有 `InstallLayout.state_root=/var/lib/dasall` 和 read-only assets root 组装 `memory/memory.db` 与 `sql/memory`，并把 migration asset 纳入 package。

## 2. 研究输入

### 2.1 本地证据

| 输入 | 本轮采用方式 |
|---|---|
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | 锁定 `FULLINT-TODO-015` 的代码目标、验收命令和完成判定：不能把 memory writeback 假定为 package-ready。 |
| `docs/architecture/DASALL_memory子系统详细设计.md` | 采用方案 B：MemoryManager facade + ContextOrchestrator + WritebackCoordinator + 单 SQLite 逻辑主库 `memory.db`；ContextOrchestrator 不直接持久化，WritebackCoordinator 承担 Turn/Summary/Fact/Experience 落盘。 |
| `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | 发现 installed live path 当前强制 `StorageBackend::Memory`，是 package persistence 风险根因。 |
| `infra/src/config/InstallLayout.cpp` | 确认 package layout 的 state root 是 `/var/lib/dasall`，read-only asset root 是 `/usr/share/dasall`。 |
| `memory/include/config/MemoryConfig.h` | 确认 SQLite backend 默认支持 `db_path`、`migrations_dir`、WAL、busy timeout 和 reader pool。 |
| `sql/memory/V001__initial_schema.sql` | 确认 Session/Turn/Summary/Fact/Experience 等 core tables 已有迁移资产。 |
| `runtime/src/AgentOrchestrator.cpp` | 发现 production LLM direct path 会进入 `Persisting/PersistenceConfirmed`，但当前只保存 runtime checkpoint/session，没有调用 memory `write_back`。 |
| `scripts/packaging/pkg_smoke_install.sh` | 作为 installed package explicit daemon path 的真实用户态验收入口，需要增加 memory state path / SQLite table / run writeback 断言。 |

### 2.2 外部参考

| 参考 | 对本任务的约束 |
|---|---|
| FHS 3.0 `/var/lib` | `/var/lib/<name>` 是 package/subsystem variable state data 的规范位置，状态应在 invocation/reboot 之间保持有效；因此 installed memory state 应落到 `/var/lib/dasall` 下，而不是 repo cwd 或 `/tmp`。 |
| SQLite WAL 文档 | WAL 允许 readers 与 writer 并发，但依赖同主机共享内存、`-wal`/`-shm` 文件与 checkpoint；因此 package smoke 需要把 database 与 WAL side files 视作同一 state directory 下的持久化集合，不把单次进程内成功当作落盘证明。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 冻结 installed memory state owner | FHS `/var/lib`、`InstallLayout.state_root`、memory 详设单 SQLite 主库 | owner 固定为 `/var/lib/dasall/memory/memory.db` | 若 state root 不可写，daemon 应 fail-closed，不回退 in-memory 冒充成功 |
| D2 | 补齐 package migration asset | `sql/memory/V001__initial_schema.sql`、SQLite migrator | `/usr/share/dasall/sql/memory/V001__initial_schema.sql` 随 `dasall-common` 安装 | 若资产未安装，SQLite init 不能宣称 ready |
| D3 | 让 live dependency composition 使用 SQLite memory | `RuntimeLiveDependencyComposition.cpp`、`MemoryConfig` | daemon/gateway live composition 初始化 SQLite memory manager 成功 | 若 SQLite 初始化失败，readiness/submit 保持 fail-closed |
| D4 | 让 production LLM direct path 写回 memory | `AgentOrchestrator` terminalize path、WritebackCoordinator | installed `dasall run` 后 SQLite `turns`/`summaries` 至少有对应记录 | 若写回失败，run 不应继续宣称 PersistenceConfirmed |
| D5 | 用 package smoke 验证真实 installed state | `pkg_smoke_install.sh --explicit-start-check` | fresh reinstall + explicit start + run 后查询 `/var/lib/dasall/memory/memory.db` schema/row count | 不用 build-tree CTest 或旧文档状态替代 installed package result |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| `/var/lib/dasall/memory/memory.db` 是 installed memory state owner | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `MemoryConfig.storage.backend=Sqlite`、`db_path=state_root/memory/memory.db`、`migrations_dir=readonly_assets_root/sql/memory` |
| memory migrations 是 package read-only asset | `CMakeLists.txt`、`debian/dasall-common.install`、package smoke | package 内存在 `/usr/share/dasall/sql/memory/V001__initial_schema.sql` |
| direct LLM path 需要真实 memory writeback | `runtime/src/AgentOrchestrator.cpp` | completed run 在 memory SQLite 中落 `turns` 与 `summaries`，写回失败时 fail-closed |
| package smoke 是 L4 installed proof owner | `scripts/packaging/pkg_smoke_install.sh` | `--explicit-start-check` 查询 database schema、journal mode、row count、request payload fragment |
| build-tree tests 只作补充回归，不作 installed 结论 | `tests/integration/access/*RuntimeLiveDependencyCompositionTest.cpp` + memory focused targets | CTest 证明代码路径可构建/可发现；最终结论仍以后续 package smoke 与 manual installed probes 为准 |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-015` 与同轮最小 blocker：memory SQLite live path + migration asset + package smoke。 |
| 前置依赖 | PASS | `FULLINT-TODO-013`/`014` 已完成，当前任务没有未完成前置 BLOCK。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令均在 §4 锁定。 |
| installed proof 边界 | PASS | 不把现有 memory CTest 或旧 worklog 当作任务完成证据；必须执行 fresh package build/smoke 和 installed SQLite 查询。 |

## 6. B 阶段执行结果

### 6.1 代码落点

| Build 项 | 文件 | 结果 |
|---|---|---|
| installed memory state owner | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`apps/runtime_support/include/RuntimeLiveDependencyComposition.h` | live dependency composition 改为 SQLite memory backend，默认 state owner 为 `/var/lib/dasall/memory/memory.db`；测试可用 options 覆盖 read-only assets root / state root。 |
| migration asset package 化 | `memory/CMakeLists.txt`、`debian/dasall-common.install` | `sql/memory/*.sql` 安装到 `/usr/share/dasall/sql/memory/`，归 `dasall-common`。 |
| direct LLM path writeback | `runtime/src/AgentOrchestrator.cpp` | production LLM direct response 成功后调用 `memory_manager->write_back(...)` 写入 turn + summary；写回失败、degraded 或 partial 时 fail-closed。 |
| installed smoke 断言 | `scripts/packaging/pkg_smoke_install.sh` | `--explicit-start-check` 增加 migration asset、SQLite DB、WAL、core tables、`turns`/`summaries` LLM writeback rows 检查；并补齐 `DASALL_DEEPSEEK_API_KEY_FILE` import path。 |
| build-tree 回归 | `tests/integration/access/*RuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/CMakeLists.txt` | daemon/gateway live composition 使用 source read-only assets + temp state root 验证 SQLite `memory.db` 创建，不写入 `/var/lib/dasall`。 |
| 知识服务 factory header blocker | `knowledge/include/KnowledgeServiceFactory.h` | 补齐 public header，使 `KnowledgeServiceFactory.cpp`、runtime_support 与测试在 CMake build 下可一致编译。 |

### 6.2 本轮验证证据

| 验证项 | 命令 / 证据 | 结果 |
|---|---|---|
| CMake build | `Build_CMakeTools(buildTargets=["dasall_memory_context_assemble_integration_test","dasall_memory_writeback_integration_test","dasall_access_daemon_runtime_live_dependency_composition_test","dasall_access_gateway_runtime_live_dependency_composition_test","dasall-daemon"])` | PASS，result code `0`；`KnowledgeServiceFactory.cpp` 有非阻塞 missing initializer warning。 |
| focused CTest | `RunCtest_CMakeTools(tests=["MemoryContextAssembleIntegrationTest","MemoryWritebackIntegrationTest","DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest"])` | PASS，4/4 passed。 |
| first-turn direct LLM writeback regression | `Build_CMakeTools(buildTargets=["dasall_runtime_cognition_loop_smoke_unit_test","dasall-daemon","dasall-cli"])` + `RunCtest_CMakeTools(tests=["RuntimeCognitionLoopSmokeTest"])` | PASS；新增 first-turn 空 session context fallback 仍进入 LLM 并写回 SQLite turn/summary 的回归。 |
| full CMake build | `Build_CMakeTools()` | PASS，result code `0`；最新 runtime、runtime_support、daemon、gateway 与测试 target 均重新构建。 |
| shell syntax | `sh -n scripts/packaging/pkg_smoke_install.sh` | PASS。 |
| Debian package build | `/tmp/dasall-fullint015/dpkg-build-nc-final.exit` | PASS，exit `0`；已生成四包 `dasall-common`、`dasall-cli`、`dasall-daemon`、`dasall`。先前 `/tmp/dasall-fullint015/dpkg-build-after-cmake-tools.exit=141` 来自 `tee` 输出管道 SIGPIPE，不作为验收 exit。 |
| package smoke | `DASALL_DEEPSEEK_API_KEY_FILE=<redacted local key file> bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`；`/tmp/dasall-fullint015/pkg-smoke-context-fix.exit` | PASS，exit `0`；fresh reinstall、secret import/preserve、explicit daemon start、ping/readiness、真实 installed `dasall run`、status/cancel/diag、Knowledge refresh/retrieve/health、LLM asset 与 memory row proof 断言均通过。 |
| installed DB row proof | `/tmp/dasall-fullint015/db-proof-final.log` | PASS；`memory_db_path=/var/lib/dasall/memory/memory.db`、`migration_asset_exists=True`、`journal_mode=wal`、`core_table_count=5`、`turns_total=1`、`summaries_total=1`、`turns_package_smoke_llm_origin=1`、`summaries_llm_origin=1`。 |

### 6.3 installed row proof 摘要

`FULLINT-TODO-015` 的最终完成判定要求 fresh installed package 下真实执行 `dasall run '{"prompt":"package smoke"}' --json --timeout-ms 120000`，并在 `/var/lib/dasall/memory/memory.db` 中证明：

1. `turns.user_input LIKE '%package smoke%'`。
2. `turns.agent_response LIKE 'llm.origin=deepseek-prod/%'`。
3. `summaries.summary_text LIKE 'llm.origin=deepseek-prod/%'`。

本轮使用用户提供的测试 DeepSeek key 写入本地临时 key 文件（路径不入档），只作为 `DASALL_DEEPSEEK_API_KEY_FILE` 注入，不把 key 值写入仓库文档、日志或提交。真实 installed smoke 之后的独立只读 SQLite 证明如下：

```text
memory_db_path=/var/lib/dasall/memory/memory.db
db_exists=True
migration_asset_exists=True
journal_mode=wal
core_table_count=5
turns_total=1
summaries_total=1
turns_package_smoke_llm_origin=1
summaries_llm_origin=1
turn_sample_turn_id=cli-run-llm-response
turn_sample_session_id=sess:cli-run:2:1
turn_sample_user_input_prefix={"prompt":"package smoke"}
turn_sample_agent_response_prefix=llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop
summary_sample_summary_id=summary-sess-cli-run-2-1-cli-run-llm-response
summary_sample_session_id=sess:cli-run:2:1
summary_sample_text_prefix=llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop
```

结论：`turns` 与 `summaries` 均含真实 installed `dasall run` 写入的 `llm.origin=deepseek-prod/deepseek-reasoner` rows，`FULLINT-TODO-015` L4 row proof 已闭合。

## 7. Build 合规复核

| 合规项 | 判定 | 说明 |
|---|---|---|
| 代码目标 | PASS | installed live memory 从 in-memory 改为 SQLite state owner；migration asset package 化；direct LLM path 增加 memory writeback fail-closed。 |
| 测试目标 | PASS | build-tree memory context/writeback 与 daemon/gateway live composition focused tests 已通过；这些只作为代码路径回归，不替代 installed-package 结论。 |
| 验收命令 | PASS | `dpkg-buildpackage -us -uc -b -nc` 复验 exit `0`；`pkg_smoke_install.sh --explicit-start-check` exit `0`；独立 SQLite row proof 证明 `turns`/`summaries` 均有 `llm.origin=deepseek-prod/` rows。 |
| 不外推 | PASS | 未用既有单测/集测、旧文档状态或 DB 初始化结果冒充 installed LLM writeback 成功。 |
| secret 管理 | PASS | 文档和日志只记录 secret URI / 文件注入条件，不记录 secret 值。 |

## 8. Gate 判定与残余风险

Gate 判定：`Done`。

已闭合：

1. D1 `/var/lib/dasall/memory/memory.db` owner 与 SQLite live composition 已落地。
2. D2 `/usr/share/dasall/sql/memory/V001__initial_schema.sql` package asset 已落地。
3. D3 daemon/gateway live composition 在 build-tree temp state root 下可创建 SQLite DB。
4. D4 direct LLM path 已接入 memory `write_back` 并 fail-closed。
5. D5 package smoke 已具备 installed DB / row proof 断言逻辑，并支持 `DASALL_DEEPSEEK_API_KEY_FILE` 导入路径。

同轮追加闭合：

1. 用户提供 DeepSeek 测试 key 后，已通过 `DASALL_DEEPSEEK_API_KEY_FILE` 完成 fresh installed secret 注入。
2. 真实 installed `dasall run '{"prompt":"package smoke"}' --json --timeout-ms 120000` 已在 package smoke 中通过，响应包含 `llm.origin=deepseek-prod/deepseek-reasoner`，未出现 `agent.dataset` fallback。
3. `/var/lib/dasall/memory/memory.db` 已独立证明 `turns` / `summaries` 均含 matching rows，BC-09 / BC-10 的 L4 local installed memory writeback 结论可升级为 passed。

残余边界：

1. 本轮仍不声明 L5 qemu / release runner / production release-ready；该结论继续归 `FULLINT-TODO-019`。
2. 用户提供的测试 key 只用于本地复验，提交前必须清理临时 key 与 curl probe 文件；仓库内不得记录 key 值。
