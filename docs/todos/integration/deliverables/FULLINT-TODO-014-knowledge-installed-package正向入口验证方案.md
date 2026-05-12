# FULLINT-TODO-014 knowledge installed-package 正向入口验证方案

日期：2026-05-11
来源任务：FULLINT-TODO-014
范围：installed Debian package 下 `dasall knowledge refresh/retrieve/health` 正向入口、重复 refresh 幂等性、package smoke 回归、Knowledge installed-package blocker 解阻

## 1. D 阶段结论

本轮只推进 `FULLINT-TODO-014`，目标是让安装态 Knowledge 能通过真实 CLI/daemon/package 路径独立证明 `refresh`、`retrieve`、`health` 正向可用，不再只依赖 build-tree `Gate-INT-04` 或文档完成状态。

任务可执行性判定：PASS。

1. 前置 `FULLINT-TODO-002` 已冻结 installed-package 与 build-tree 证据分层；`FULLINT-TODO-013` 已完成 fresh package build / reinstall / explicit daemon start 的 L4 local package 矩阵。
2. `FULLINT-BLK-002` 的最小解阻条件是安装态有独立 Knowledge 正向入口，并由真实 package 运行结果证明 `retrieve/refresh/health`。
3. 本轮发现两个验证阻塞并在同一任务内最小修复：SQLite FTS search step 状态误判导致 `retrieve` rejected；installed factory inventory 缺失导致重复 `refresh` 在 active DB seed 后重复插入 chunks。
4. `FULLINT-BLK-001` 仍是 release runner / qemu / secret / network 的 L5 阻塞；本轮只声明 L4 local installed-package Knowledge positive path ready。

## 2. 研究输入

### 2.1 本地证据

| 输入 | 本轮采用方式 |
|---|---|
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | 读取 `FULLINT-TODO-014`、`FULLINT-BLK-002` 和 BC-08/BC-16 的 owner 与完成判定。 |
| `docs/todos/integration/deliverables/FULLINT-TODO-013-installed-package控制面主功能矩阵.md` | 继承 fresh package / installed daemon 的 L4 验证边界，不复用其旧缺口结论。 |
| `access/src/AccessGatewayFactory.cpp` | 确认 `dasall knowledge` daemon dispatch owner，并修复 refresh 失败时的 domain error ref 暴露。 |
| `knowledge/src/KnowledgeServiceFactory.cpp` | 确认 installed asset service factory 是 package runtime composition owner，并补齐 installed inventory state。 |
| `knowledge/src/index/IndexWriter.cpp` | 定位 SQLite FTS sparse search 的 step 状态误判根因。 |
| `scripts/packaging/pkg_smoke_install.sh` | 将 explicit-start package smoke 作为 installed `knowledge refresh/retrieve/health` 正向验收入口。 |
| installed CLI probes | 用真实 `sudo -n dasall knowledge refresh/retrieve/health --json` 验证 package 行为。 |

### 2.2 外部参考

| 参考 | 对本任务的约束 |
|---|---|
| SQLite `sqlite3_step()` C API 文档 | `SQLITE_ROW` 表示一行可读，`SQLITE_DONE` 表示语句成功执行完成；因此 FTS search 循环不能把最终 `SQLITE_DONE` 当成执行失败。 |
| Debian `autopkgtest(1)` man page | `autopkgtest` 面向 testbed 中已安装 binary package；本轮 local installed smoke 只能作为 L4，不冒充 L5 qemu / release runner。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 冻结 installed Knowledge 正向入口 owner | FULLINT-BLK-002、Access gateway dispatch、Knowledge installed factory | owner 固定为 CLI -> daemon AccessGateway -> installed `IKnowledgeService` | 若 daemon 未接真实 service，保持 blocker，不用 Gate-INT-04 外推 |
| D2 | 修复 retrieve 的 SQLite FTS search 成功判定 | SQLite C API、`IndexWriter::perform_search()` | `SQLITE_DONE` 结束被视为成功，installed retrieve 返回 evidence slices | 若仍失败，暴露具体 sparse error ref 后继续定位 |
| D3 | 修复重复 refresh 幂等性 | installed factory、SourceScanner、IndexWriter active DB seed | 第二次 installed `knowledge refresh` accepted，空 change batch 不重复插入 chunks | 若只能 fresh refresh，一律不宣称 package ready |
| D4 | 将 package smoke 纳入正向入口验收 | `pkg_smoke_install.sh --explicit-start-check` | explicit package smoke 覆盖 refresh/retrieve/health 并 exit 0 | 默认 smoke 不外推；必须带 `--explicit-start-check` |
| D5 | 记录 degraded health 预期语义 | installed lexical-only factory、health output | health `freshness_state=fresh` 且 `state=degraded` / `vector_backend_disabled` 记录为预期 | 不把 degraded 写成 failure，也不伪造 vector backend ready |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| installed Knowledge owner 走 daemon dispatch | `access/src/AccessGatewayFactory.cpp` | `refresh/retrieve/health` 返回具体 payload；refresh rejected 时保留 service error ref |
| SQLite FTS retrieve 成功语义按 `sqlite3_step()` 返回值判断 | `knowledge/src/index/IndexWriter.cpp` | focused probe 与 installed `retrieve "DeepSeek Chat"` 均返回 `ok=true` / positive slices |
| installed factory 维护 source inventory | `knowledge/src/KnowledgeServiceFactory.cpp` | 重复 `request_refresh(CorpusChangeSet{})` / installed `knowledge refresh` 均 accepted |
| build-tree 复现 installed factory path | `tests/integration/knowledge/KnowledgeInstalledAssetProbeIntegrationTest.cpp` | 测试覆盖 refresh、重复 refresh、health fresh active snapshot、DeepSeek FTS evidence 与 retrieve |
| package smoke 覆盖 positive path | `scripts/packaging/pkg_smoke_install.sh` | `--explicit-start-check` 下 refresh/retrieve/health assertions 全部通过 |
| 真实 installed package 作最终验收 | `dpkg-buildpackage` + `pkg_smoke_install.sh --explicit-start-check` + manual CLI | commands exit 0，manual JSON 字段满足 refresh/retrieve/health 正向条件 |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-014` 与其直接 blocker `FULLINT-BLK-002`。 |
| 前置依赖 | PASS | `FULLINT-TODO-002`、`FULLINT-TODO-013` 已 Done。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令均在 §4 锁定。 |
| L4/L5 边界 | PASS | 本轮只声明 local installed-package；qemu/release runner 仍归 019。 |

## 6. B 阶段执行结果

### 6.1 Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 结果 |
|---|---|---|---|---|
| B1 | `knowledge/src/index/IndexWriter.cpp` | installed retrieve sparse search | focused build/test + installed `knowledge retrieve` | PASS，SQLite `SQLITE_DONE` 不再误判为 `search_execution_failed` |
| B2 | `knowledge/src/KnowledgeServiceFactory.cpp` | installed factory repeated refresh | `KnowledgeInstalledAssetProbeIntegrationTest` + manual refresh twice | PASS，重复 refresh accepted |
| B3 | `access/src/AccessGatewayFactory.cpp` | refresh failure diagnostics | package/CLI refresh path | PASS，非 Busy refresh failure 可透出 service `source_ref.ref_id` |
| B4 | `tests/integration/knowledge/KnowledgeInstalledAssetProbeIntegrationTest.cpp` | build-tree installed asset positive probe | `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest"])` | PASS，覆盖 refresh、重复 refresh、health、FTS evidence、retrieve |
| B5 | `scripts/packaging/pkg_smoke_install.sh` | package explicit smoke positive path | `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PASS，脚本 explicit path 覆盖 knowledge checks |
| B6 | Debian package artifacts + installed CLI | true installed-package validation | `dpkg-buildpackage -us -uc -b`、manual refresh/retrieve/health | PASS，真实 package 四包重装后 knowledge 正向入口通过 |

### 6.2 代码修复摘要

| 文件 | 变更 | 结果 |
|---|---|---|
| `knowledge/src/index/IndexWriter.cpp` | `perform_search()` 直接跟踪 `sqlite3_step()` 状态，允许 successful iteration 以 `SQLITE_DONE` 结束 | installed `retrieve` 不再被误报为 sparse search failure |
| `knowledge/src/KnowledgeServiceFactory.cpp` | installed asset factory 新增 in-memory inventory state，通过 `SourceScanner` 和 catalog refresh 同步 source records | 重复 full refresh 不再把全部 installed asset 当作新增并向 seeded active DB 重复插入 chunks |
| `access/src/AccessGatewayFactory.cpp` | refresh rejected 时优先返回 `ErrorInfo.source_ref.ref_id`，Busy 仍返回 `knowledge_refresh_busy` | 后续 refresh failure 不再被统一遮蔽为 generic `knowledge_refresh_failed` |
| `knowledge/src/retrieve/RecallCoordinator.cpp` | sparse recall failure 保留具体 failure reason | retrieve 调试和 regression 断言可见具体 sparse error ref |
| `scripts/packaging/pkg_smoke_install.sh` | explicit-start path 增加/校准 knowledge refresh、retrieve、health positive assertions | package smoke 可直接证明 installed Knowledge 正向入口 |

### 6.3 focused build / test

| 命令 | 结果 |
|---|---|
| `Build_CMakeTools(buildTargets=["dasall_knowledge_installed_asset_probe_integration_test","dasall_recall_coordinator_degraded_unit_test"])` | PASS，result code 0 |
| `RunCtest_CMakeTools(tests=["KnowledgeInstalledAssetProbeIntegrationTest","RecallCoordinatorDegradedTest"])` | PASS，2/2 passed |
| `Build_CMakeTools(buildTargets=["dasall-daemon","dasall_knowledge_installed_asset_probe_integration_test"])` | PASS，result code 0 |

### 6.4 package build 与 explicit smoke

| 命令 | 结果 |
|---|---|
| `dpkg-buildpackage -us -uc -b` | PASS，exit 0；生成 `/home/gangan/dasall-cli_0.1.0-1_amd64.deb`、`/home/gangan/dasall-common_0.1.0-1_all.deb`、`/home/gangan/dasall-daemon_0.1.0-1_amd64.deb`、`/home/gangan/dasall_0.1.0-1_all.deb` |
| `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PASS，exit 0；fresh reinstall 后 explicit daemon path 覆盖 knowledge refresh/retrieve/health assertions |

说明：默认 `bash scripts/packaging/pkg_smoke_install.sh` 不进入 explicit daemon start path，因此不能作为 Knowledge positive path 的充分验收；本轮采信带 `--explicit-start-check` 的执行结果。

### 6.5 manual installed-package Knowledge 矩阵

| 探针 | 命令 | 实际结果 | 判定 |
|---|---|---|---|
| refresh #1 | `sudo -n dasall knowledge refresh --json --timeout-ms 30000` | exit 0；`operation=refresh`、`disposition=completed`、`status=accepted`、`refresh_id=batch:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| refresh #2 | `sudo -n dasall knowledge refresh --json --timeout-ms 30000` | exit 0；重复空 batch refresh accepted，`refresh_id=batch:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS，幂等性闭合 |
| retrieve | `sudo -n dasall knowledge retrieve "DeepSeek Chat" --json --timeout-ms 30000` | exit 0；`ok=true`、`slice_count=3`；首条 citation `llm/providers/deepseek/models.yaml#char=1296-2056`，snippet 包含 `models. deepseek-chat. supports_tools=true` | PASS |
| health | `sudo -n dasall knowledge health --json --timeout-ms 30000` | exit 0；`state=degraded`、`freshness_state=fresh`、active snapshot present，reason `vector_backend_disabled` | PASS，degraded 为 lexical-only installed path 的预期语义 |

## 7. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码注释 | 本轮未新增叙事性注释；新增 inventory helper 与 step handling 采用局部函数/变量表达语义。 |
| 正负例覆盖 | 正例：refresh、重复 refresh、retrieve、health；负例/退化：`RecallCoordinatorDegradedTest` 保留 sparse failure reason，health degraded reason `vector_backend_disabled` 明确记录。 |
| 测试发现性 / 门禁入口 | focused target 与 CTest 已通过；package explicit smoke 已覆盖真实 installed CLI/daemon path。 |
| TODO / 交付物 / worklog 回写 | 本文件记录 D/B gate、命令和真实 package 结果；来源 TODO 与 worklog 同步回写。 |
| 无关改动隔离 | `obj-x86_64-linux-gnu/`、打包副产物与其他生成文件不纳入提交。 |

## 8. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | 见 §5。 |
| B Gate | PASS | focused build/test、Debian package build、explicit package smoke、manual installed CLI matrix 均通过。 |
| `FULLINT-BLK-002` | RESOLVED for L4 local | installed package 能独立证明 `knowledge refresh/retrieve/health`；重复 refresh accepted。 |
| L5 qemu / release-ready | BLOCKED / out of scope | 仍归 `FULLINT-TODO-019`，需要 release runner / qemu image / secret / network。 |

## 9. 残余风险与后继任务

1. Health `state=degraded` 是当前 installed lexical-only path 的预期结果；只有 `freshness_state=fresh`、active snapshot 与 retrieve positive slices 同时成立时才可视为本轮通过。
2. `pkg_smoke_install.sh --explicit-start-check` 是本轮 package smoke owner；默认 smoke 不覆盖 Knowledge positive path，不能被单独写为 014 验收通过。
3. L5 qemu / release runner 权威 installed-package gate 仍由 `FULLINT-TODO-019` 执行。
4. tools/services runtime production caller installed boundary 仍由 `FULLINT-TODO-016` 执行。