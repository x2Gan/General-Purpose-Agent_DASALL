# WP-MEM-GAP-018 release soak metrics closeout

来源任务：WP-MEM-GAP-018
关联缺口：GAP-P3-E
完成日期：2026-07-13

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-018 / GAP-P3-E`，不把 installed gate、runtime daemon readiness 回归修复或新的 memory 业务逻辑改造混入同一轮。
2. authoritative 问题定义固定为：infra owner 已有 local release/soak harness，但 Memory 维度仍缺少可归档的 soak 指标面，导致 release-runner 无法直接回答 store latency、wal size、maintenance lag、writeback partial rate、vector recall@k 与 summary fallback rate 是否回退。
3. owner 边界保持不变：指标采样逻辑继续停留在 memory module-local / test-probe seam；infra release-soak 只负责编排与归档，不接管 memory 主链控制权。

## 2. 研究与设计依据

1. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%90%BD%E5%9C%B0%E8%AF%84%E4%BC%B0%E4%B8%8E%E7%94%9F%E4%BA%A7%E7%BA%A7%E7%BC%BA%E5%8F%A3%E6%B2%BB%E7%90%86%E4%BB%BB%E5%8A%A1%E8%A7%84%E5%88%92.md) 已将 `WP-MEM-GAP-018` 固定为“在 infra release-soak 套件内加 memory 维度采样（store latency / wal size / maintenance lag / writeback partial rate / vector recall@k / summary fallback rate）；soak 报告归档”。
2. [docs/todos/memory/deliverables/WP-MEM-GAP-020-memory-quality-slo-closeout.md](./WP-MEM-GAP-020-memory-quality-slo-closeout.md) 已冻结 `writeback_partial_rate` 与 `compression_fallback_rate` 采用 rolling rate，并明确后续 soak gate 应直接消费这些 metric identity，而不是另起第二套质量口径。
3. [docs/todos/infrastructure/deliverables/INF-FIX-006-infra-release-soak-gate收口.md](../../infrastructure/deliverables/INF-FIX-006-infra-release-soak-gate%E6%94%B6%E5%8F%A3.md) 已冻结 infra owner local soak 的 authoritative artifact contract：`infra-release-soak-summary.json`、installed iteration JSON 与 focused binary logs；本轮只在同一 contract 内追加 memory summary，不改 qemu / machine-isolation 边界。
4. Pinecone retrieval evaluation 与 Ragas faithfulness baseline 已在 `WP-MEM-GAP-020` 中作为现有质量指标依据收口；本轮不再重新定义 recall/fallback 指标，只把它们嵌入 release-soak artifact。
5. [SQLite WAL documentation](https://www.sqlite.org/wal.html) 说明重叠的长读事务会阻止 checkpoint 完整推进并使 WAL 无界增长；因此 artifact 保留 WAL 峰值和 maintenance lag，而不是只报告 writeback 成功率。
6. [Google SRE canary guidance](https://sre.google/workbook/canarying-releases/) 建议 gate 选择少量、可归因且能体现用户影响的指标；因此本轮直接复用既有 quality metric identity，并把 summary 固定在同一 release-runner artifact contract 内。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| release-soak 需要一个可直接归档的 memory 指标 artifact，而不是临时拼装散落日志 | [tests/integration/memory/MemoryReleaseSoakProbeTest.cpp](../../../../tests/integration/memory/MemoryReleaseSoakProbeTest.cpp)、[tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) |
| infra owner 继续持有 local soak harness，但应把 memory summary 一并归档到现有 infra summary contract | [scripts/packaging/infra_release_soak_gate.sh](../../../../scripts/packaging/infra_release_soak_gate.sh)、[.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) |
| gate 文档必须把新增的 memory artifact、focused binary 与 release-runner 归档路径写成 SSOT | [scripts/packaging/README.md](../../../../scripts/packaging/README.md) |
| 即使 installed daemon 当前环境不可稳定 ready，本轮也需要有一条不依赖 systemd readiness 的 wiring guard 锁住 target/script/workflow/README 接线 | [tests/integration/memory/MemoryReleaseSoakWiringTest.cpp](../../../../tests/integration/memory/MemoryReleaseSoakWiringTest.cpp)、[tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) |

## 4. 设计决策

1. 新增 `MemoryReleaseSoakProbeTest` 作为 focused integration binary，而不是直接复用 `MemoryLongRunningSoakTest`、`MemoryQualityProbeIntegrationTest` 与 `MemoryVectorRecallQualityTest` 的 stdout。release-runner 需要 machine-readable summary，而不是多条测试各自打印的临时输出。
2. `MemoryReleaseSoakProbeTest` 直接复用已存在的 owner seams：
   - `MemoryLongRunningSoakTest` 的 SQLite WAL / retention / maintenance loop 语义用于 store latency、wal size、maintenance lag；
   - `WP-MEM-GAP-020` 已落地的 quality metrics 用于 `writeback_partial_rate`、`summary_fallback_rate` 与 context-level `retrieval_recall_at_k`；
   - `MemoryVectorRecallQualityTest` 的 semantic embedding fixture 用于 `vector_recall_at_k` baseline vs uplift。
3. `infra_release_soak_gate.sh` 继续是 infra owner authoritative harness；本轮只在现有 summary 中嵌入 `memory_release_soak_summary` 与对应 log/artifact 名，不改 qemu 边界，也不把 memory soak 提升成独立第二个 workflow。
4. Gate 以脚本内的 `run_root` 调用 installed daemon；服务 socket 目录对普通用户不可遍历是既有服务权限边界，不构成 Memory 或 runtime 缺陷。本轮以该 authoritative 权限路径复验，10 次 readiness / diagnostics iteration 与所有 focused binary 均通过，因此不需要扩张到 daemon 或权限模型改造。

## 5. D Gate

1. 边界清晰：本轮只增加 memory soak metrics artifact、infra harness 集成与 wiring regression guard，不处理 runtime daemon readiness 根因。
2. Build 三件套完整：代码目标、测试目标、验收命令均已锁定在新 probe、新 wiring test、infra script/workflow/README 接线与 focused build/run。
3. Gate 结论：PASS。前置依赖 `GAP-P0-C`、`GAP-P1-B`、`GAP-P0-A`、`GAP-P0-B` 已闭合；本轮 end-to-end installed soak 已通过，不存在设计、实现或验证 blocker。

## 6. 代码结果

1. 新增 [tests/integration/memory/MemoryReleaseSoakProbeTest.cpp](../../../../tests/integration/memory/MemoryReleaseSoakProbeTest.cpp)，输出 `memory-release-soak-summary.json`，固定以下指标：
   - `store_latency_ms`：320 次 writeback 的 min/avg/p95/max/last；
   - `wal_size_bytes.max`：长跑期间观测到的 WAL 峰值；
   - `maintenance_lag_ms`：40 次 maintenance loop 的 min/avg/p95/max/last；
   - `writeback_partial_rate`：来自现有 quality probe 的 rolling rate；
   - `summary_fallback_rate`：来自现有 quality probe 的 rolling rate；
   - `vector_recall_at_k`：`k=1` 下 local baseline vs semantic embedding uplift。
2. 更新 [tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt)，注册 `dasall_memory_release_soak_probe_integration_test` / `MemoryReleaseSoakProbeTest`，并为其补齐 `runtime_support`、`llm`、`infra`、`sqlite3` 依赖与 `DASALL_SQL_MEMORY_DIR` compile definition。
3. 新增 [tests/integration/memory/MemoryReleaseSoakWiringTest.cpp](../../../../tests/integration/memory/MemoryReleaseSoakWiringTest.cpp)，并更新 [tests/integration/memory/CMakeLists.txt](../../../../tests/integration/memory/CMakeLists.txt) 注册 `dasall_memory_release_soak_wiring_integration_test` / `MemoryReleaseSoakWiringTest`，锁定 CMake、`infra_release_soak_gate.sh`、release workflow 与 packaging README 的接线不回退。
4. 更新 [scripts/packaging/infra_release_soak_gate.sh](../../../../scripts/packaging/infra_release_soak_gate.sh)，新增 `run_memory_release_soak_probe()` helper，执行新 probe 并把 `memory-release-soak-summary.json` 嵌入 `infra-release-soak-summary.json` 的 `memory_release_soak_summary` 字段，同时记录 `memory-release-soak.log` 与 `memory_release_soak_summary_artifact`。
5. 更新 [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml)，在 local infra soak focused build 列表中加入 `dasall_memory_release_soak_probe_integration_test`，确保 release-runner 会先把新 probe 编出来。
6. 更新 [scripts/packaging/README.md](../../../../scripts/packaging/README.md)，把 infra local release/soak gate 的功能矩阵、artifact contract 与 release-runner 归档路径扩展为同时包含 `memory-release-soak-summary.json`。

## 7. 验证

1. `cmake -S . -B build-ci && cmake --build build-ci --target dasall_memory_release_soak_probe_integration_test && ./build-ci/tests/integration/memory/dasall_memory_release_soak_probe_integration_test --artifact-dir /tmp/dasall-mem-gap-018-probe`
   - 结果：通过；生成 `memory-release-soak-summary.json`，其中 `store_latency_ms`、`wal_size_bytes.max`、`maintenance_lag_ms`、`writeback_partial_rate`、`summary_fallback_rate`、`vector_recall_at_k` 与 `retrieval_recall_at_k` 全部存在。
2. `sh -n scripts/packaging/infra_release_soak_gate.sh`
   - 结果：通过；shell 语法未回退。
3. `cmake -S . -B build-ci && cmake --build build-ci --target dasall_memory_release_soak_wiring_integration_test && ./build-ci/tests/integration/memory/dasall_memory_release_soak_wiring_integration_test`
   - 结果：通过；CMake、soak 脚本、release workflow 与 packaging README 的新增接线全部被锁定。
4. `cmake --build build-ci --target dasall_infra_diagnostics_smoke_integration_test dasall_infra_diagnostics_integration_test dasall_health_wiring_integration_test dasall_infra_health_cadence_integration_test dasall_memory_release_soak_probe_integration_test dasall_secret_failure_injection_integration_test dasall_plugin_lifecycle_state_unit_test dasall_metrics_failure_injection_integration_test`
   - 结果：通过；增强后的 local infra soak focused build 列表完整可构建。
5. `DASALL_INFRA_RELEASE_SOAK_ITERATIONS=10 bash scripts/packaging/infra_release_soak_gate.sh --artifact-dir /tmp/dasall-mem-gap-018-soak-current --build-dir build-ci`
   - 结果：通过。脚本经既有 `run_root` 路径完成 10 次 installed readiness / diagnostics iteration、全部 8 个 focused binaries 与 `MemoryReleaseSoakProbeTest`，并生成 `infra-release-soak-summary.json` / `memory-release-soak-summary.json`。本次 artifact 记录：writeback 320 次，`store_latency_ms.p95=9.104`、`wal_size_bytes.max=86552`（ceiling 2097152）、maintenance 40 次且 `maintenance_lag_ms.p95=12.301`、`vector_recall_at_k.semantic_adapter=1.0`。

## 8. 完成判定

1. `WP-MEM-GAP-018 / GAP-P3-E` 的代码目标已闭合：infra release-soak 套件现具备可归档的 memory soak summary artifact，并覆盖 `store latency / wal size / maintenance lag / writeback partial rate / vector recall@k / summary fallback rate` 六类指标。
2. release-runner 归档 contract 已闭合：focused build 列表、infra summary、memory summary 与 packaging README 均已同步接线，且有 dedicated wiring guard 防止回退。
3. authoritative local soak 绿色记录已闭合；后续长时或 release-runner 复验可直接复用同一 artifact contract，不需要再次设计第二套 soak 指标口径。
