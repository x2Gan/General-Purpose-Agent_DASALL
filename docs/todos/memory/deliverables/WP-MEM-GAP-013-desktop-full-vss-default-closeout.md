# WP-MEM-GAP-013 desktop_full sqlite-vss default closeout

来源任务：WP-MEM-GAP-013
关联缺口：GAP-P2-E
完成日期：2026-06-18

## 1. 任务边界

1. 本轮只收口 `WP-MEM-GAP-013 / GAP-P2-E`，不把 installed gate、ProgrammaticMemory、分层递归摘要、质量 SLO 或 reflection feedback loop 混入同一轮。
2. authoritative 问题定义固定为：在 `GAP-P0-B` 外部 embedding 注入与 `GAP-P0-C` 并发/长跑证据闭合后，memory 评估文档不应继续把 `desktop_full` 记作“默认 `vector.enabled=false`”；当前工作树已经在 profile、projector、runtime composition 与 focused gates 上满足“desktop_full 默认开启 sqlite-vss，并保留 fail-closed `none` 回退”的目标。
3. owner 边界保持不变：默认开向量的 authoritative 配置继续留在 profiles，`MemoryConfigProjector` 只做 manifest->config 投影，`RuntimeLiveDependencyComposition` 只负责 runtime-owned embedding glue 与 sqlite-vss 资产缺失时的 fail-closed 回退；本轮不扩 shared contracts，也不新增 runtime 控制面配置键。

## 2. 研究与设计依据

1. [docs/architecture/DASALL_memory子系统详细设计.md](../../../architecture/DASALL_memory子系统详细设计.md) §6.10.2 已把 `desktop_full` 冻结为“开启（默认 `sqlite-vss`，失败回退 `none`）”；§10.2 又明确第三阶段只在 `desktop_full/cloud_full` 灰度打开 `sqlite-vss` 主链，并保留所有 profile 的 `none` 回退。
2. [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md) 已将 `WP-MEM-GAP-013` 固定为“desktop_full 默认开启 sqlite-vss 灰度”，代码目标明确要求 `vector.enabled=true` 且保留 fail-closed `none` 回退。
3. 本地源码证据表明实现已在当前工作树落地：[profiles/desktop_full/runtime_policy.yaml](../../../../profiles/desktop_full/runtime_policy.yaml) 已固定 `enabled_modules.memory_vector: true`；[memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp) 以 `manifest.enables_module("memory_vector")` 投影 `config.vector.enabled=true`、`backend_type=sqlite-vss`；[apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp](../../../../apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp) 已注入 `embedding_adapter_factory`，并在 `vector0` / `vss0` 运行时资产缺失时 fail-closed 回退到 `VectorBackend::None`。
4. 外部参考采用 [sqlite-vss README](https://github.com/asg017/sqlite-vss) 与 [SQLite run-time loadable extensions 文档](https://www.sqlite.org/loadext.html)：`sqlite-vss` 明确以 `.load ./vector0` / `.load ./vss0` 的共享扩展形态启用向量能力，SQLite 官方也明确 loadable extension 可以按需装载共享库；这直接支持 DASALL 采用“desktop_full 默认请求 sqlite-vss，但缺扩展资产时回退到 `none`”的 fail-closed 灰度策略，而不是把向量能力硬编码成不可降级的必需项。

## 3. Design -> Build 映射

| Design 目标 | Build / Validation 目标 |
|---|---|
| `desktop_full` 默认向量能力必须冻结在 profile 资产，而不是散落在 memory/runtime 临时 override | [profiles/desktop_full/runtime_policy.yaml](../../../../profiles/desktop_full/runtime_policy.yaml)、[profiles/src/BuildProfileResolver.cpp](../../../../profiles/src/BuildProfileResolver.cpp) |
| `MemoryConfigProjector` 必须把 `memory_vector` manifest 位投影成 `vector.enabled=true` 与 `sqlite-vss` backend | [memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp)、[tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp) |
| runtime live composition 必须继续注入 runtime-owned embedding factory，同时在 sqlite-vss 资产缺失时 fail-closed 回退到 `none` | [apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp](../../../../apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp)、[tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp](../../../../tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp) |
| 规划文档 / 总账 / worklog 不应再把 `WP-MEM-GAP-013` 记为未完成 | 本轮更新 [docs/deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md](../../../deliverables/MEM-EVAL-2026-05-31-memory子系统落地评估与生产级缺口治理任务规划.md)、[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../../todos/DASALL_子系统查漏补缺专项记录.md)、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_开发执行记录.md) |

## 4. 设计决策

1. `desktop_full` 的向量默认值继续由 profile 资产声明，避免把 capability default 下沉到 `MemoryConfigProjector` 或 `RuntimeLiveDependencyComposition` 的特判逻辑。
2. `MemoryConfigProjector` 继续保持 manifest-driven：有 `memory_vector` 则投影 `sqlite-vss` 与较宽 `search_top_k`，无该模块则统一回落 `none`，不把灰度策略散落成多处布尔判断。
3. runtime composition 继续保留 sqlite-vss 共享扩展文件存在性检查；即使 `desktop_full` 默认请求向量能力，只要 `vector0` / `vss0` 缺失，仍立刻 fail-closed 回退到 `VectorBackend::None`，不让缺资产路径拖垮主链。
4. 当前工作树已经满足代码目标，所以本轮不再改动 C++ 实现或测试逻辑；本轮只补 focused 验收与 traceability closeout，避免为“已完成实现”制造无意义的重复代码改动。

## 5. D Gate

1. 设计边界明确：不新增 shared surface，不改 runtime / llm owner 分层，不把默认值写成 build-tree 局部 override。
2. Build 三件套完整：代码目标是 profile/projector/composition 的默认向量语义，测试目标是 `MemoryProfileCompatibilityTest` + live composition focused gate，验收命令保持 memory profile compatibility 主门。
3. blocker 结论：当前不存在必须先执行的前置 BLOCK 任务；`GAP-P0-B` 与 `GAP-P0-C` 均已在前序轮次闭合，本轮可以直接做 closeout 与证据回写。

## 6. 代码结果

1. [profiles/desktop_full/runtime_policy.yaml](../../../../profiles/desktop_full/runtime_policy.yaml) 当前已固定 `enabled_modules.memory_vector: true`；`BuildProfileResolver` 会把该位保留进 build manifest，因此 `desktop_full` 不再是“默认 vector disabled”的 profile。
2. [memory/src/config/MemoryConfigProjector.cpp](../../../../memory/src/config/MemoryConfigProjector.cpp) 当前已基于 `manifest.enables_module("memory_vector")` 投影 `config.vector.enabled`、`config.vector.backend_type=sqlite-vss` 与更宽的 `search_top_k` / scoring defaults。
3. [apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp](../../../../apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp) 当前已同时满足两条语义：
   - 注入 runtime-owned `embedding_adapter_factory`，让 `desktop_full` 的向量路径可以复用 `GAP-P0-B` 已闭合的 external embedding glue。
   - 当 `vector0` / `vss0` 共享库缺失时，将 `vector.enabled`、`backend_type` 与 `search_top_k` fail-closed 回退到 `none`，不把 sqlite-vss 资产缺失传播成主链路故障。
4. [tests/integration/memory/MemoryProfileCompatibilityTest.cpp](../../../../tests/integration/memory/MemoryProfileCompatibilityTest.cpp) 当前已锁定 `desktop_full` 的 manifest `memory_vector`、`config.vector.enabled`、`backend_type=sqlite-vss` 与 `search_top_k=8`；本轮未再修改该测试代码，因为当前工作树已满足任务要求。

## 7. 验证

1. `RunCtest_CMakeTools(tests=["MemoryProfileCompatibilityTest"])`
   - 结果：通过，1/1。
2. `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest"])`
   - 结果：通过，1/1。

## 8. 结果

1. `WP-MEM-GAP-013 / GAP-P2-E` 已闭合；`desktop_full` 默认开启 sqlite-vss 且保留 fail-closed `none` 回退的语义已在当前工作树中成立，并有 focused profile + live composition 自动化证据支撑。
2. 本轮没有新增 C++ 代码改动；实际完成内容是把“已落地但未正式收口”的实现状态回写到主规划文档、总账与 worklog，避免后续评审继续把 `WP-MEM-GAP-013` 误判为未完成。
3. Memory 当前剩余 V2 焦点从 `WP-MEM-GAP-013` 转移为 `WP-MEM-GAP-019 / -020 / -021` 与更高层 installed / soak / quality SLO 证据，desktop_full 默认开向量已不再是未闭合缺口。