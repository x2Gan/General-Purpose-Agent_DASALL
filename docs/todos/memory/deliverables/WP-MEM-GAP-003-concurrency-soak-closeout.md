# WP-MEM-GAP-003 concurrency / soak closeout

来源任务：WP-MEM-GAP-003
关联缺口：GAP-P0-C
完成日期：2026-06-02

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-003 / GAP-P0-C`，不把 installed / qemu gate、MaintenanceTicker 或后续 quality 演进项混入同一轮。
2. authoritative 问题定义固定为：Memory 是否已经补齐 manager 级并发压力测试、长跑 soak 测试、tsan preset 与 CI / 本地统一执行入口。
3. 若 tsan 失败点落在 Memory 自身锁序或 shared state，则回到 memory 代码修复；若失败点停在 SQLite WAL 共享内存已知路径，则只允许以最窄 suppression 固定第三方假阳性，不修改 Memory owner 语义。

## 2. 本轮代码结果

| 目标 | 落盘结果 | 对 closeout 的意义 |
|---|---|---|
| manager 并发压力门 | 新增 [tests/unit/memory/MemoryConcurrencyStressTest.cpp](../../../tests/unit/memory/MemoryConcurrencyStressTest.cpp) 并接入 [tests/unit/memory/CMakeLists.txt](../../../tests/unit/memory/CMakeLists.txt) | 对 `prepare_context()` / `write_back()` / `run_maintenance()` 三线程并发给出 1k+ 轮 focused evidence |
| 长跑 soak 门 | 新增 [tests/integration/memory/MemoryLongRunningSoakTest.cpp](../../../tests/integration/memory/MemoryLongRunningSoakTest.cpp) 并接入 [tests/integration/memory/CMakeLists.txt](../../../tests/integration/memory/CMakeLists.txt) | 对 WAL 增长、checkpoint、retention 与 quarantine cleanup 给出压缩长跑证据 |
| tsan preset | 更新 [CMakePresets.json](../../../CMakePresets.json) 新增 `tsan` configure/build/test presets | 统一本地与 CI 的 ThreadSanitizer 构建入口 |
| CI / 本地入口 | 新增 [scripts/ci/memory_tsan_stress.sh](../../../scripts/ci/memory_tsan_stress.sh)、[scripts/ci/memory_tsan.supp](../../../scripts/ci/memory_tsan.supp) 并更新 [.github/workflows/ci.yml](../../../.github/workflows/ci.yml) | `memory_tsan_stress` 现可作为统一脚本入口供 CI job 与本地复验复用 |

## 3. 外部与第三方依据

1. ThreadSanitizer manual 明确：TSAN 只对运行时实际执行到的路径给出报告，且第三方或暂不可修代码可通过 suppression 文件暂时收口。
2. SQLite WAL 文档明确：WAL 允许 reader 与单 writer 并发，checkpoint 需要与长期 reader 协调。
3. SQLite 3.51.3 源码在 [third_party/.cache/dasall_sqlite_autoconf-src/sqlite3.c](../../../third_party/.cache/dasall_sqlite_autoconf-src/sqlite3.c) 已明确注释 `walIndexTryHdr` / `walIndexWriteHdr` 一类共享内存双读 + barrier 路径可能触发 TSAN false-positive；本轮实际报告也停在同一条 WAL header 读取调用链。因此 suppression 只匹配 `walTryBeginRead` / `walIndexReadHdr` / `walIndexTryHdr` / `walIndexWriteHdr` 四个 SQLite WAL 函数名，不覆盖 Memory 自身栈帧。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| `prepare_context()` / `write_back()` / `run_maintenance()` 三线程并发必须有 manager 级证据 | `MemoryConcurrencyStressTest` |
| WAL / retention / quarantine 长跑必须有压缩 soak 证据 | `MemoryLongRunningSoakTest` |
| 并发压力门必须有统一 tsan 入口 | `CMakePresets.json` `tsan` + `scripts/ci/memory_tsan_stress.sh` + CI job |
| 第三方 SQLite WAL 共享内存假阳性不能污染 Memory owner 结论 | `scripts/ci/memory_tsan.supp` 仅匹配 SQLite WAL 函数名 |

## 5. D Gate

1. 并发 owner 不变：Memory 仍只证明自身 manager / store / maintenance 语义，不把 runtime recovery admission 或 qemu installed gate 混进来。
2. TSAN 收口边界明确：suppression 仅用于第三方 SQLite WAL 共享内存路径；若后续报告栈进入 `memory/src/*`，不得沿用该 suppression 掩盖。
3. 产物复用单一：CI job 与手工复验共用 `scripts/ci/memory_tsan_stress.sh`，避免多份不一致命令链。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_memory_concurrency_stress_unit_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["MemoryConcurrencyStressTest"])`
   - 结果：通过。
3. `Build_CMakeTools(buildTargets=["dasall_memory_long_running_soak_integration_test"])`
   - 结果：通过。
4. `RunCtest_CMakeTools(tests=["MemoryLongRunningSoakTest"])`
   - 结果：通过。
5. `bash scripts/ci/memory_tsan_stress.sh`
   - 结果：通过；`MemoryConcurrencyStressTest` 与 `MemoryLongRunningSoakTest` 在 `tsan` preset 下均为 green，`100% tests passed, 0 tests failed out of 2`。

## 7. 完成判定

1. `WP-MEM-GAP-003 / GAP-P0-C` 已闭合。
2. Memory 现在同时具备 build-tree focused 并发压力门、长跑 soak 门，以及 CI / 本地统一 `memory_tsan_stress` 入口。
3. TSAN 结论现只排除 SQLite WAL 共享内存已知假阳性；任何后续进入 `memory/src/*` 的并发报告都仍应视为真实缺口重新打开本任务。