# KNO-TODO-045 runtime-owned selective refresh automation 口径闭合双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-045 | Runtime-owned selective refresh automation 口径闭合`。
2. 上游前置：`KNO-TODO-041` 已把 runtime-owned timer callback、automation ready/fallback marker 与 periodic full-scan fallback 接线完成；`KNO-FIX-008` 已把 manual `changed_source -> updated_sources` control-plane seam 冻结为 authoritative runtime owner path。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 的 ingest trigger model；`docs/todos/knowledge/deliverables/KNO-TODO-041-runtime-owned-selective-refresh-automation双轨任务包.md`；ADR-006 / ADR-007 / ADR-008 owner boundary；`docs/ssot/HealthCadenceAndEventBoundary.md`。
4. 本轮目标：把 041 的 runtime-owned periodic full-scan automation 收口为“runtime owner 先生成 selective delta，full-scan 只在 fallback 条件下触发”的真实实现；不把 watcher/timer/source ownership 下沉到 Knowledge，不改 `contracts/`。

## 2. 研究证据

### 2.1 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前 `arm_runtime_knowledge_auto_refresh()` 的 timer callback 固定调用 `request_refresh(CorpusChangeSet{})`，因此 runtime 自动刷新虽然存在，但永远是 full-scan，不会生成 selective delta。
2. `knowledge/src/ingest/SourceScanner.cpp` 已有稳定 inventory diff 语义：比较 `content_hash`、`version`、`updated_at_ms` 后输出 `added`、`updated`、`removed_source_ids`，且可复用上一轮 inventory；这意味着 runtime 不需要自造第二套文件 diff 规则。
3. `knowledge/src/ingest/IngestionCoordinator.cpp` 的 `select_target_corpora()` 已按 changed source URI 是否落入 `descriptor.source_uri` 前缀做目标 corpus 选择，Runtime 只要提供 source URI 集合即可；不需要 Runtime 理解 chunk、manifest 或 snapshot 私有实现。
4. `knowledge/src/KnowledgeServiceFactory.cpp` 已冻结 installed asset corpus roots：`docs/architecture`、`docs/adr`、`docs/ssot`、`profiles/*/runtime_policy.yaml` 与 `llm/providers/*`；因此 runtime selective provider 可以围绕这些已知 trusted corpora 做扫描，不新增 profile schema。
5. `tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp` 当前只验证 timer tick 触发 full-scan refresh 后可看到新 token，没有断言 selective delta、busy skip 或 fallback 分层。
6. `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` 已分别锁定 `updated_sources` selective path、empty `CorpusChangeSet` full-scan fallback、busy reject 与 activation rollback；本轮只需要把 runtime auto-refresh 的输入从空集改为 selective-first，就能复用这些既有行为约束。

### 2.2 外部实践

1. Azure Advanced RAG 文档在 update strategies 中明确建议：文档频繁变化时优先做 `trigger-based updates`、`selective reindexing` 与 `delta encoding`，仅在重大变化时回退 full reindex。这与本轮“selective 优先、full-scan 仅 fallback”的方向一致。
2. LlamaIndex ingestion pipeline 的 document management 约定是维护 `doc_id -> document_hash` 映射：hash 不变则跳过，hash 变化才重新处理并 upsert。该实践说明 runtime owner 持有轻量 inventory/hash baseline 是合理的，不必把变更检测下沉到索引 owner 内部。

## 3. 设计结论

### 3.1 可证伪假设

当前缺口不是 Knowledge refresh worker，也不是 timer callback 本身，而是 runtime automation 没有生成 delta。若在 runtime owner 内新增一个基于 `SourceScanner` inventory diff 的 refresh source provider，并让 timer tick 先消费该 provider，再决定 `Skip / Selective / FullScanFallback` 三种计划，则：

1. 单文件变更可以只把目标 source URI 投到 `CorpusChangeSet.updated_sources`；
2. installed selective provider 可以把 `SourceScanner` 产出的相对 source URI 归一成 installed descriptor root 对应的绝对路径，不会因为路径口径漂移漏选 target corpus；
3. provider 不可用、inventory 失配、扫描 quarantine 或 no-change tick 时仍能安全回退到 `request_refresh({})` full-scan，以维持 freshness baseline。

廉价反证：若 runtime auto-refresh focused test 仍只能观察到 empty `CorpusChangeSet`，或者 selective tick 后非目标 source 被错误纳入 refresh plan，则该假设不成立。

### 3.2 owner 边界与不变式

1. Refresh trigger owner 继续属于 Runtime / apps；Knowledge 仍只消费 `CorpusChangeSet`，不新增 watcher、sleep loop 或 scheduler。
2. Runtime selective provider 只负责从 runtime 可见的 installed assets 生成 delta，不接管 corpus catalog、chunking、snapshot swap 或 activation。
3. installed selective provider 输出的 change set 必须与 installed descriptor root 使用同一绝对路径口径；manual control-plane 的相对 `changed_source` seam 继续保持既有 contract，不在 045 重写。
4. full-scan 不再是默认“有变化”timer 行为，只在以下 fallback 条件触发：provider 缺失、provider 返回 fallback、inventory 基线失效、扫描结果整体 quarantine 或 tick 未观测到 delta。
4. Busy 语义必须保持：若 tick 触发前 `health_snapshot().refresh_in_flight=true`，或 race 下 `request_refresh()` 返回 `Busy`，本次 tick 只做 benign skip，不补发第二个 refresh，也不把 Busy 转译成失败。
5. `AccessGatewayFactory` 现有 manual `changed_source -> updated_sources` seam 保持 authoritative，不与自动化路径合并队列，不扩 payload schema。

### 3.3 最小实现面

1. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`
   - 新增 runtime-owned refresh plan / provider seam，允许 focused test 注入 recording provider；production path 默认使用 filesystem selective provider。
2. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 新增 default selective provider：复用 `knowledge::ingest::SourceScanner` 与 installed asset corpus roots 建 baseline inventory，并在每次 tick 生成 `RuntimeKnowledgeRefreshPlan`；写入 change set 前会把相对 source URI 归一为 installed absolute path。
   - timer callback 逻辑从“固定 empty change set”改为“plan 驱动”：`refresh_in_flight=true` 时直接 skip；`Selective` 传 `CorpusChangeSet`；`FullScanFallback` 才传 empty change set。
   - `AutoRefreshKnowledgeService` 同时持有 timer handle 与 refresh provider，确保 provider 生命周期与 runtime composition 一致。
3. 测试面
   - 扩展 `tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp`：新增 selective positive、fallback-to-full-scan 与 busy fail-safe focused case。
   - 扩展 `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`：新增 selective change set 只影响目标 source / 非目标 source 不误刷新回归，继续守住 full-scan fallback。

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 锁定真实缺口是 runtime 缺少 delta provider，而非 Knowledge refresh worker | 本地证据 1 / 5 / 6 | 本文 §3.1 | Build 首项明确是 provider seam，不重写 Knowledge | 若需要修改 Knowledge private scheduler 才能继续，则 D Gate 失败 |
| D2 | 锁定 default provider 复用 `SourceScanner` inventory diff | 本地证据 2 / 3 / 4；外部实践 1 / 2 | 本文 §3.2 / §3.3 | runtime selective provider 可只产出 source URI delta | 若必须复制第二套 hash/diff 规则，则设计范围过大 |
| D3 | 锁定 timer tick 的三态计划：skip / selective / full-scan fallback | 本地证据 1 / 6 | 本文 §3.2 / §5 | full-scan 从默认路径降级为 fallback | 若 tick 仍需每次 full-scan 才能通过验收，则 045 不成立 |
| D4 | 锁定 focused 测试矩阵与验收出口 | 本地证据 5 / 6 | 本文 §5 / §7 | selective positive、busy fail-safe、full-scan fallback 都有二值验证 | 若只能用 release/package soak 才能表达完成判定，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 | B1 新增 runtime refresh provider seam | `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `KnowledgeRuntimeAutoRefreshIntegrationTest` focused provider injection path | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_runtime_auto_refresh_integration_test -j2` |
| D2 | B2 实装 filesystem selective provider | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `KnowledgeRuntimeAutoRefreshIntegrationTest` selective positive / fallback case | `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_runtime_auto_refresh_integration_test` |
| D3 | B3 收口 busy 与 full-scan fallback 语义 | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`、`tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp` | busy fail-safe、fallback-to-full-scan | `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_runtime_auto_refresh_integration_test` |
| D4 | B4 扩展 refresh loop 回归 | `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` | selective target-only + full-scan fallback | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_refresh_loop_integration_test -j2`；`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_refresh_loop_integration_test` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 当前控制行为的唯一缺口已经定位到 `arm_runtime_knowledge_auto_refresh()` callback，切口足够局部。
2. runtime selective provider 可直接复用 `SourceScanner` 的 hash / inventory 语义，无需扩大到 Knowledge private implementation。
3. 既有 `updated_sources`、busy reject 与 full-scan fallback 测试资产已经存在，只需在 runtime timer 输入层补 selective plan。
4. focused 验收出口清晰且低成本：`dasall_knowledge_runtime_auto_refresh_integration_test` 与 `dasall_knowledge_refresh_loop_integration_test` 足以证伪本轮假设。

进入 `KNO-TODO-045-B` 的前提：

1. 不改 `contracts/`，不新增 Knowledge-owned watcher/timer。
2. selective provider 默认只依赖 runtime 可见 installed assets；manual control-plane seam 继续独立。
3. full-scan 仅用于 fallback，不再作为“有变化 tick”的默认行为。
4. busy race 保持 benign skip，不新增后台排队层。

## 7. Build 原子清单

1. B1：新增 runtime refresh plan / provider seam
   - 代码目标：`apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 测试目标：`KnowledgeRuntimeAutoRefreshIntegrationTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_runtime_auto_refresh_integration_test -j2`
   - 风险与回退：若 public header 需要暴露过多 Knowledge 私有类型，优先收口为 runtime_support 自有 plan type
2. B2：实现 default filesystem selective provider
   - 代码目标：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 测试目标：`KnowledgeRuntimeAutoRefreshIntegrationTest` selective positive / fallback-to-full-scan
   - 验收命令：`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_runtime_auto_refresh_integration_test`
   - 风险与回退：若某一 corpus root 扫描失败，不切换私有重试 loop，只返回 full-scan fallback
3. B3：锁定 busy fail-safe 与 target-only selective regression
   - 代码目标：`tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp`、`tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`
   - 测试目标：busy skip、single-source selective、non-target not refreshed、full-scan fallback
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_runtime_auto_refresh_integration_test dasall_knowledge_refresh_loop_integration_test -j2`；随后直接执行两个 binaries
   - 风险与回退：若 selective assert 只能靠内部状态证明，则优先注入 recording provider / recording service 做 focused evidence，而不是扩大生产 surface

## 8. 回退与后继

1. 回退基线：041 的 periodic full-scan automation 是本轮兜底路径；若 selective provider 无法安全生成 delta，系统仍可回退到 full-scan fallback，但不得继续声称“selective automation 已闭合”。
2. 非目标：045 不新增 OS watcher、inotify 封装、daemon 新 payload、release/package soak 证据，也不触碰 production embedding provider。
3. 后继顺序：045-B 完成后，knowledge/installed 主链的 freshness automation 口径闭合；若后续需要更细粒度的 OS watcher，再以新原子任务单列。

## 9. 完成判定

`KNO-TODO-045-B` 仅当以下条件同时满足时完成：

1. runtime timer tick 默认先尝试 selective delta，而不是固定 full-scan；
2. 单文件变更时，timer 触发的 refresh plan 能只把目标 source URI 送入 `CorpusChangeSet`；
3. installed selective provider 发给 Knowledge 的 source path 与 installed descriptor root 口径一致；provider 无 delta、无 provider / baseline 失配 / quarantine 时才回退 full-scan；
4. busy refresh 不会被自动化吞掉或叠加排队；
5. `KnowledgeRuntimeAutoRefreshIntegrationTest` 与 `KnowledgeRefreshLoopTest` 都能用 focused binary 证明上述语义。

## 10. Build 完成证据

1. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h` 已新增 `RuntimeKnowledgeRefreshPlanKind`、`RuntimeKnowledgeRefreshPlan` 与 `IRuntimeKnowledgeRefreshSourceProvider`，让 runtime selective provider 可被 production path 默认接线，也可被 focused integration test 注入 recording provider。
2. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已新增 default installed selective provider：围绕 `docs/architecture`、`docs/adr`、`docs/ssot`、`profiles/*/runtime_policy.yaml` 与 `llm/providers/*` 建 baseline inventory，并在 timer tick 中按 `Selective / FullScanFallback` 输出 refresh plan；provider 会把 `SourceScanner` 产出的相对 source URI 归一为 installed absolute path，从而匹配 installed descriptor roots。
3. `tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp` 已新增四类 focused case：default provider changed-asset 正例、注入 selective provider 的 target-only 正例、provider full-scan fallback 正例，以及通过扩展 ADR corpus 体量拉长窗口后的 busy skip 回归。
4. `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` 已新增 multi-corpus selective regression：当 `updated_sources` 只包含 ADR source 时，新的 active snapshot 只引入 ADR 更新，SSOT 非目标更新不会被误刷新；既有 busy reject、full-scan fallback 与 activation rollback 回归保持不变。
5. 2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_knowledge_runtime_auto_refresh_integration_test","dasall_knowledge_refresh_loop_integration_test"])`；`RunCtest_CMakeTools(tests=["KnowledgeRuntimeAutoRefreshIntegrationTest","KnowledgeRefreshLoopTest"])` 继续命中仓库已知泛化 `生成失败`，因此按仓库既有回退口径直接执行 `./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_runtime_auto_refresh_integration_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_refresh_loop_integration_test && printf "%s\n" PASS`，结果为 `PASS`。