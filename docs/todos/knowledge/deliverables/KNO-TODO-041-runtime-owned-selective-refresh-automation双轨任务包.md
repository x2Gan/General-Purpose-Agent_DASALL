# KNO-TODO-041 runtime-owned selective refresh automation 双轨任务包

## 1. 输入与目标

1. 来源任务：`KNO-TODO-041 | 建立 Runtime-owned selective refresh automation`。
2. 上游前置：`KNO-TODO-040-B` 已完成，installed local proof / soak 已把 hybrid canary / fallback evidence 固化；但 production freshness 仍长期依赖人工 `knowledge refresh`，尚未形成 runtime-owned 自动刷新闭环。
3. 设计锚点：`docs/architecture/DASALL_knowledge子系统详细设计.md` 中 Ingest 触发模型；`docs/ssot/HealthCadenceAndEventBoundary.md` 中 `ITimer` / fallback 约束；`docs/architecture/platform_linux_detailed_design.md` 中 `PosixTimerProvider` 语义；ADR-006 / ADR-007 / ADR-008 owner boundary；`KNO-TC002`。
4. 本轮目标：在不把 watcher/timer owner 下沉到 Knowledge、不改 `contracts/`、不引入私有轮询 loop 的前提下，把 freshness 从 manual-only 推进到 runtime-owned periodic fallback refresh，并明确保留现有 `changed_source -> updated_sources` selective manual seam。

## 2. 研究证据

### 2.1 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前只在 `validate_installed_knowledge_positive_probe()` 中做一次启动期 `request_refresh(CorpusChangeSet{})`；positive probe 结束后没有任何持续调度路径，说明 041 的缺口不在 Knowledge refresh worker，而在 Runtime 组合根没有接上长期 cadence。
2. 同一文件虽然已经组合了 `runtime_event_bus`、`runtime_telemetry_bridge` 与 `background_maintenance_hooks`，但 `runtime/src/maintenance/BackgroundMaintenanceHooks.h` / `.cpp` 只负责发布 idle maintenance event，本身不拥有 scheduler，也没有 knowledge refresh callback；因此 041 不能把它误当成现成 refresh loop。
3. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h` 的 `RuntimeLiveDependencyCompositionOptions` 当前没有任何 timer / scheduler seam；`apps/daemon/src/main.cpp` 与 `apps/gateway/src/main.cpp` 也都没有把平台 timer provider 透传给 runtime composition。
4. `access/src/AccessGatewayFactory.cpp` 已经把 repeated `changed_source` 映射到 `CorpusChangeSet.updated_sources` 后调用 `IKnowledgeService::request_refresh(changes)`；`tests/integration/access/KnowledgeRuntimeRefreshTriggerIntegrationTest.cpp` 也已锁定这条 manual selective refresh seam。因此 041 不需要重写 control-plane payload，只需保证自动化不会破坏它。
5. `knowledge/src/config/KnowledgeConfigProjector.cpp` 已把 `catalog_refresh_interval_ms` 直接投影为 `snapshot.capability_cache_policy().refresh_interval_ms`；`docs/architecture/DASALL_knowledge子系统详细设计.md` 也明确写明“定时策略刷新”应由 `Runtime timer / scheduler` 以 `request_refresh({})` 触发。这意味着 041 不需要新增 profile schema，只需消费现有 cadence 投影。
6. `docs/architecture/DASALL_knowledge子系统详细设计.md` 同时明确：v1 production 现在只收口为 manual control-plane seam，`Knowledge` 不自建 file watcher、timer 或独立调度主循环；`CorpusChangeSet` 为空时表示 full-scan fallback。这给 041 提供了最小范围：先闭合 runtime-owned periodic fallback，不在本轮扩张到新 watcher。
7. `platform/src/linux/PosixTimerProvider.cpp` 当前 `start_periodic()` / `start_once()` 只校验 spec、分配 handle、记录 state，不会实际触发 callback；现有 `tests/unit/platform/linux/PosixTimerProviderTest.cpp` 也只锁定“能 start / cancel”骨架。这是 041 的真实 blocker：若不先补齐 periodic callback 语义，任何 runtime auto-refresh controller 都只能停在测试桩路径，不能宣称 production automation 已完成。
8. `infra/src/watchdog/DeadlineWheel.cpp` 已展示 post-unary 组件可以直接沿 `platform::ITimer::start_periodic()` 组织调度，而不是绕回私有 sleep loop 或强行复用 health 专用 `ProbeScheduler`；041 可以沿用这类 `ITimer` consume 模式，但不能误把 health 双分组 scheduler 套到 knowledge refresh 上。
9. `apps/daemon/CMakeLists.txt` 已链接 `dasall_platform`，而 `apps/gateway/CMakeLists.txt` 当前还没有；若 041 选择入口注入 `PosixTimerProvider`，则 gateway 的 app wiring 也要同步补齐平台依赖。

### 2.2 外部实践

1. RFC 5861 的 `stale-while-revalidate` 实践强调：当系统没有推送式 invalidation 信号时，可以用受控周期 revalidate 作为保底策略，但必须清楚区分“fresh window”“stale window”和“hard stop”，而不是把缺少 watcher 等价成永久人工操作。对 DASALL 而言，这意味着 041 的最小自动化应是周期性 revalidate，而不是继续让 freshness 只靠 CLI。
2. 平台 timer 语义的一般工程实践要求：callback delivery 与 cancel 必须是同一 owner 的显式契约；若 timer provider 不可用，系统应停用周期调度并留下 fallback evidence，而不是偷偷切到裸线程轮询。该原则与现有 `HealthCadenceAndEventBoundary` SSOT 完全一致。

## 3. 设计结论

### 3.1 边界与不变式

1. 041 的 refresh trigger owner 仍是 Runtime / apps；Knowledge 只继续提供 `request_refresh(const CorpusChangeSet&)` 这一写入口，不新增 watcher、timer、sleep loop 或第二套调度 abstraction。
2. 本轮 Build 切片收敛为“runtime-owned periodic full-scan fallback refresh”。原因是：production 当前不存在已冻结的 runtime-owned file watcher / delta source，唯一稳定的 selective source 输入仍是现有 manual `changed_source` control-plane seam。041 的完成判定只要求 freshness 不再长期依赖人工 CLI，而不是在同轮偷渡新的 watcher 子系统。
3. 因此，自动化周期 tick 统一调用 `request_refresh(CorpusChangeSet{})`；manual `changed_source -> updated_sources` selective path 保持不变，继续作为更精细的显式 refresh 入口。
4. cadence 来源固定为现有 `KnowledgeConfigSnapshot.catalog_refresh_interval_ms`，也即 profile 的 `capability_cache_policy.refresh_interval_ms`。041 不新增 profile schema、deployment key 或 runtime hardcoded interval。
5. timer seam 只允许沿 `platform::ITimer` 实现。若 timer provider 缺失、`start_periodic()` 失败或 callback 语义不可用，041 必须停在“禁用自动调度 + 保留 fallback evidence”的路径，不允许回退到 `std::this_thread::sleep_for`、裸后台轮询或第二套私有 timer abstraction。
6. auto refresh 不能破坏已有 busy contract：timer tick 在触发前先看 `health_snapshot().refresh_in_flight`；即使 race 下 `request_refresh({})` 返回 `Busy`，也只把本次 tick 记为 benign skip，不把 Busy 转译成 hard failure，更不吞掉 manual path 的 Busy 可观测性。
7. auto refresh 也不能改变 lexical-only / hybrid rollout 语义。041 只驱动 refresh，不触碰 retrieve mode、hybrid canary allowlist 或 Access payload schema。

### 3.2 owner 拆分与 evidence 设计

| 设计点 | Owner | 方案 | 理由 |
|---|---|---|---|
| periodic callback blocker | platform | 把 `PosixTimerProvider` 从“handle-only skeleton”补到真实 callback delivery + cancel 语义 | 若没有真实 callback，041 的 production automation 不成立 |
| refresh automation 生命周期 | apps/runtime_support | 在 runtime composition 内新增 runtime-owned auto-refresh controller / decorator，包装 `IKnowledgeService` 并在析构时 cancel timer handle | 保持 owner 在 Runtime 组合根，不把生命周期清理推回 Knowledge |
| timer 注入 | daemon / gateway entrypoint | 通过 `RuntimeLiveDependencyCompositionOptions` 透传 `std::shared_ptr<platform::ITimer>`；daemon 与 gateway 在入口创建 `PosixTimerProvider` | 避免把 Linux 具体 provider 硬编码进 runtime_support，同时让 focused tests 可注入 fake / recording timer |
| cadence 语义 | knowledge config projector -> runtime | runtime controller 直接消费 `catalog_refresh_interval_ms`，按该周期触发 full-scan refresh | 复用已有 profile 投影，不新造配置口径 |
| fallback evidence | runtime composition | 在 `external_evidence` 中新增 `runtime:<owner>:knowledge-refresh-automation-ready`；timer 不可用或 arm 失败时写入 `runtime:<owner>:knowledge-refresh-automation-fallback:<reason>` | 让 failure matrix 可区分“knowledge ready 但 automation fallback”与“knowledge 本身 degraded” |
| control-plane selective seam | access / daemon | `AccessGatewayFactory` 的 manual `changed_source -> updated_sources` 路径继续直达 `request_refresh(changes)`，不在 041 重写为后台队列 | 041 的目标是 freshness automation，不是扩张 control-plane 契约；保留现有 selective manual path 更安全 |

### 3.3 最小实现面

1. `platform/include/linux/PosixTimerProvider.h`、`platform/src/linux/PosixTimerProvider.cpp`
   - 补真实 one-shot / periodic callback delivery、cancel / repeated cancel 语义与析构清理；保持 `ITimer` surface 不变。
2. `tests/unit/platform/linux/PosixTimerProviderTest.cpp`
   - 从“只看 start/cancel skeleton”升级为验证 callback 确实可触发，cancel 能停止后续触发且 repeated cancel 仍幂等。
3. `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`
   - 为 `RuntimeLiveDependencyCompositionOptions` 增加 timer seam，使 focused integration 可以注入 `RecordingTimer`，真实 app 可以注入 `PosixTimerProvider`。
4. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 在 knowledge positive probe 成功后创建 auto-refresh controller / decorator：按 `catalog_refresh_interval_ms` arm periodic timer；timer tick 走 `request_refresh({})`；对 timer unavailable / arm failure 写 fallback evidence；对 timer-ready path 写 ready evidence。
5. `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` 与 `apps/gateway/CMakeLists.txt`
   - 注入真实 `PosixTimerProvider`；gateway 需补 `dasall_platform` link，确保与 daemon 同样具备平台 timer owner。
6. 测试面
   - 扩展 `tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp`，锁定“timer tick 触发 refresh 不破坏既有 busy reject / refresh loop contract”。
   - 新增 `tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp`，用 real knowledge refresh loop + recording timer 验证 source 变更后仅靠 runtime tick 就能完成 refresh/retrieve 闭环，不再需要手工 CLI。
   - 扩展 `tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`，锁定 `knowledge-refresh-automation-ready` 与 fallback evidence marker 的 stratified 语义。
7. 非目标
   - 041 不新增 file watcher、不重写 `AccessGatewayFactory` payload、不把 manual changed-source trigger 改造成后台队列、不扩大到 release / installed soak。

## 4. Design 原子清单

| D 原子项 | 设计目标 | 输入依据 | 产出 | 完成判定 | 风险与回退 |
|---|---|---|---|---|---|
| D1 | 确认 041 的真实 blocker 是 platform timer callback，而不是 knowledge refresh worker | 本地证据 1/7/8 | 本文 §2.1、§3.1、§3.2 | Build 首项明确先解 timer blocker | 若 timer 仍停留在 handle-only skeleton，则 041 不得宣称 production automation 完成 |
| D2 | 锁定 runtime-owned periodic fallback refresh 的最小 owner 切片 | 本地证据 1/2/5/6 | 本文 §3.1 / §3.2 / §3.3 | automation 只做 periodic full-scan fallback，manual selective seam 保持不变 | 若必须引入 watcher / 新 payload 才能继续，则 D Gate 失败 |
| D3 | 锁定 app wiring 与 evidence marker | 本地证据 3/7/9 | 本文 §3.2 / §5 / §7 | timer 注入点、gateway 依赖补齐与 marker 口径明确 | 若 app 无法透传 timer，则必须回到 design 重新拆 blocker |
| D4 | 锁定 focused test matrix 与非目标 | 本地证据 4/6/8 | 本文 §3.3 / §5 / §7 | 平台单测 + knowledge/access focused integration 的三件套明确 | 若测试需要扩面到 watcher/release soak 才能表达完成判定，则 D Gate 失败 |

## 5. Design -> Build 映射

| Design 项 | Build 原子项 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| D1 timer blocker | B1 补齐 `PosixTimerProvider` callback delivery | `platform/include/linux/PosixTimerProvider.h`、`platform/src/linux/PosixTimerProvider.cpp`、`tests/unit/platform/linux/PosixTimerProviderTest.cpp` | `PosixTimerProviderTest` | `cmake --build build/vscode-linux-ninja --target dasall_posix_timer_provider_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R PosixTimerProviderTest --output-on-failure` |
| D2 runtime-owned periodic fallback | B2 增加 timer seam 与 auto-refresh controller | `apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` | `KnowledgeRefreshLoopTest`、`KnowledgeRuntimeAutoRefreshIntegrationTest` | `cmake --build build/vscode-linux-ninja --target dasall_knowledge_refresh_loop_integration_test dasall_knowledge_runtime_auto_refresh_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRefreshLoopTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRuntimeAutoRefreshIntegrationTest --output-on-failure` |
| D3 app wiring 与 marker | B3 注入 app timer provider 并锁定 stratified evidence | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`apps/gateway/CMakeLists.txt`、`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` | `RuntimeLiveCompositionFailureMatrixTest` | `cmake --build build/vscode-linux-ninja --target dasall_access_runtime_live_composition_failure_matrix_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure` |
| D4 discoverability 与 focused gate | B4 注册新 integration target / CMake | `tests/integration/knowledge/CMakeLists.txt`、必要时 `tests/integration/access/CMakeLists.txt` | `ctest -N` discoverability | `ctest --test-dir build/vscode-linux-ninja -N | rg "PosixTimerProviderTest|KnowledgeRefreshLoopTest|KnowledgeRuntimeAutoRefreshIntegrationTest|RuntimeLiveCompositionFailureMatrixTest"` |

## 6. D Gate 结果

结论：PASS。

通过依据：

1. 041 的行为 owner 已经缩到两个直接控制点：platform timer callback seam 与 runtime composition refresh controller；不需要再广扫 watcher / event bus / access contract。
2. `catalog_refresh_interval_ms` 已有真实配置投影，manual `changed_source` selective seam 也已冻结，因此本轮无需新增 profile schema 或 control-plane payload。
3. blocker 已被明确识别为 `PosixTimerProvider` callback 语义缺失；只要先解这个 blocker，就能把 runtime-owned periodic fallback refresh 作为独立、可测试的 build slice 落地。
4. focused 验收出口清晰：平台 timer unit、knowledge refresh loop、new auto refresh integration、runtime live composition failure matrix 即可覆盖本轮完成判定。

进入 `KNO-TODO-041-B` 的前提：

1. timer provider blocker 必须作为 B1 同轮先解，不能继续用 fake-only timer 误报 production automation 完成。
2. 不引入新 watcher / new payload / `contracts/` 扩面。
3. manual `changed_source -> updated_sources` selective seam 必须保持兼容，Busy 语义不能被自动化吞掉。
4. timer unavailable 路径必须有 fallback evidence，而不是静默停掉自动刷新。

## 7. Build 原子清单

1. B1：补齐 platform periodic timer callback
   - 代码目标：`platform/include/linux/PosixTimerProvider.h`、`platform/src/linux/PosixTimerProvider.cpp`、`tests/unit/platform/linux/PosixTimerProviderTest.cpp`
   - 测试目标：`PosixTimerProviderTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_posix_timer_provider_unit_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R PosixTimerProviderTest --output-on-failure`
   - 风险与回退：若 callback delivery 需要引入额外线程/同步原语，只允许收口在 provider 内部；不得修改 `ITimer` public surface 或让 runtime_support 发明第二套 timer API
2. B2：实现 runtime-owned auto-refresh controller
   - 代码目标：`apps/runtime_support/include/RuntimeLiveDependencyComposition.h`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
   - 测试目标：`KnowledgeRefreshLoopTest`、`KnowledgeRuntimeAutoRefreshIntegrationTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_knowledge_refresh_loop_integration_test dasall_knowledge_runtime_auto_refresh_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRefreshLoopTest --output-on-failure`；`ctest --test-dir build/vscode-linux-ninja -R KnowledgeRuntimeAutoRefreshIntegrationTest --output-on-failure`
   - 风险与回退：若 controller 在 timer tick 时撞上 in-flight refresh，只允许 benign skip / Busy，不允许在 runtime_support 内自行排队或重写 knowledge worker 语义
3. B3：补 app timer wiring 与 evidence stratification
   - 代码目标：`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、`apps/gateway/CMakeLists.txt`、`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp`
   - 测试目标：`RuntimeLiveCompositionFailureMatrixTest`
   - 验收命令：`cmake --build build/vscode-linux-ninja --target dasall_access_runtime_live_composition_failure_matrix_integration_test -j2`；`ctest --test-dir build/vscode-linux-ninja -R RuntimeLiveCompositionFailureMatrixTest --output-on-failure`
   - 风险与回退：若 gateway 无法安全接入平台 timer，则至少必须在 failure matrix 中显式记录 `knowledge-refresh-automation-fallback:*`，不能默认假装与 daemon 等价 ready
4. B4：注册 discoverability 与 focused gate
   - 代码目标：`tests/integration/knowledge/CMakeLists.txt`、必要时 `tests/integration/access/CMakeLists.txt`
   - 测试目标：`ctest -N` discoverability
   - 验收命令：`ctest --test-dir build/vscode-linux-ninja -N | rg "PosixTimerProviderTest|KnowledgeRefreshLoopTest|KnowledgeRuntimeAutoRefreshIntegrationTest|RuntimeLiveCompositionFailureMatrixTest"`
   - 风险与回退：若聚合 `ctest` 再次命中仓库已知泛化噪音，则回退 direct binary，但 discoverability 仍必须可见

## 8. 回退与后继

1. 回退基线：manual `dasall-cli knowledge refresh [--changed-source <path>]...` selective seam 保持 authoritative control-plane owner；041 只做 additive periodic fallback automation。
2. 兼容基线：Knowledge refresh worker、Busy 语义、hybrid canary allowlist 与 retrieve explain surface 都不在 041 改动范围内。
3. fallback 策略：若 app 没有可用 `ITimer` provider 或 arm 失败，runtime 继续保留现有 knowledge ready / degraded 语义，只额外落 `knowledge-refresh-automation-fallback:*` evidence，不切换私有轮询。
4. 后继顺序：`KNO-TODO-041-B` 完成后，若仍需要基于文件 delta 的真正 selective watcher automation，应以新原子任务单列推进；本轮不偷渡 watcher。
5. 禁区：041 不改 `contracts/`、不新增 daemon payload、不中途把 `AccessGatewayFactory` manual route 改造成后台合并队列、不把平台 timer 改动外推为全局 platform soak 完成。

## 9. 完成判定

`KNO-TODO-041-B` 仅当以下条件同时满足时完成：

1. `PosixTimerProvider` 已具备真实 periodic callback 语义，focused unit test 能证明 callback delivery 与 cancel 行为，而不再只是 handle skeleton。
2. runtime live composition 在 timer-ready path 上能稳定写出 `runtime:<owner>:knowledge-refresh-automation-ready` evidence，并在 timer unavailable / arm failure 时写出 `runtime:<owner>:knowledge-refresh-automation-fallback:<reason>`；该 marker 不得与 `knowledge-degraded:*` 混写。
3. `KnowledgeRuntimeAutoRefreshIntegrationTest` 能证明 source 更新后，仅靠 runtime-owned timer tick 就能触发 refresh 并让后续 retrieve 命中新内容，不再需要人工 CLI refresh。
4. `KnowledgeRefreshLoopTest` 继续保持 busy reject 与 refresh loop 闭环绿色，说明自动化没有破坏既有 manual / busy contract。
5. 未引入 Knowledge-owned watcher/timer、未新增 `contracts/` 或 payload schema、未把 timer 缺失时的 fallback 偷换成私有轮询。

## 10. Build 完成证据

1. `platform/include/linux/PosixTimerProvider.h`、`platform/src/linux/PosixTimerProvider.cpp` 与 `tests/unit/platform/linux/PosixTimerProviderTest.cpp` 已把 Linux timer provider 从 handle-only skeleton 收口为真实 callback delivery + cancel 语义；focused unit gate 现可验证 periodic callback 至少触发一次且 cancel 后不再继续回调。
2. `apps/runtime_support/CMakeLists.txt`、`apps/runtime_support/include/RuntimeLiveDependencyComposition.h` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已新增 `platform::ITimer` 注入 seam，并把 installed knowledge service 包装成 runtime-owned periodic full-scan auto-refresh controller：timer tick 固定走 `request_refresh(CorpusChangeSet{})`，timer-ready path 写出 `runtime:<owner>:knowledge-refresh-automation-ready`，timer 缺失或 arm 失败时写出 `runtime:<owner>:knowledge-refresh-automation-fallback:*`。
3. `apps/daemon/src/main.cpp`、`apps/gateway/CMakeLists.txt` 与 `apps/gateway/src/main.cpp` 已把 `PosixTimerProvider` 接到真实入口 wiring；`tests/integration/knowledge/KnowledgeRefreshLoopTest.cpp` 已新增 empty `CorpusChangeSet` full-scan fallback regression；新增 `tests/integration/knowledge/KnowledgeRuntimeAutoRefreshIntegrationTest.cpp` 并更新 `tests/integration/knowledge/CMakeLists.txt` 后，real knowledge refresh loop + recording timer 已能证明 source 更新后仅靠 runtime tick 即可完成 refresh / retrieve 闭环；`tests/integration/access/RuntimeLiveCompositionFailureMatrixTest.cpp` 与 `tests/integration/access/CMakeLists.txt` 已同步锁定 automation ready/fallback marker 的 stratified 语义。
4. 2026-05-26 已通过 `Build_CMakeTools(buildTargets=["dasall_posix_timer_provider_unit_test"])`、`Build_CMakeTools(buildTargets=["dasall_knowledge_refresh_loop_integration_test","dasall_knowledge_runtime_auto_refresh_integration_test"])` 与 `Build_CMakeTools(buildTargets=["dasall-daemon","dasall_gateway","dasall_access_runtime_live_composition_failure_matrix_integration_test"])`；`RunCtest_CMakeTools` 对 `PosixTimerProviderTest`、`KnowledgeRefreshLoopTest`、`KnowledgeRuntimeAutoRefreshIntegrationTest` 与 `RuntimeLiveCompositionFailureMatrixTest` 仍命中仓库已知泛化 `生成失败`，因此按仓库回退口径直接执行 `./build/vscode-linux-ninja/tests/unit/platform/linux/dasall_posix_timer_provider_unit_test && echo PASS`、`./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_refresh_loop_integration_test && ./build/vscode-linux-ninja/tests/integration/knowledge/dasall_knowledge_runtime_auto_refresh_integration_test && echo PASS` 与 `./build/vscode-linux-ninja/tests/integration/access/dasall_access_runtime_live_composition_failure_matrix_integration_test && echo PASS`，结果均为 `PASS`。