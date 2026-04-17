# TOOL-TODO-042 tools 专项 Gate 与交付证据收敛

日期：2026-04-17
任务：TOOL-TODO-042
状态：Gate PASS / Evidence Written Back

## 1. 本地证据

1. `docs/todos/tools/DASALL_tools子系统专项TODO.md` 已把 042 定义为 tools 专项串行链的最后一个 L2 收口任务，要求回写 Gate、blocker、风险残留、命令证据与后续动作。
2. 025/026/030/035/040/041 已分别收敛 builtin/services smoke、observability、workflow failure、MCP hybrid、skill runtime / plugin skill bundle、profile/discoverability 的 targeted evidence，042 不再扩产品语义，只做全量 Gate 回写。
3. 本轮正式执行了统一 build-ci 命令链：`cmake -S . -B build-ci -G "Unix Makefiles"`、`cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_contract_tests dasall_integration_tests`、`ctest --test-dir build-ci -N`、`ctest --test-dir build-ci --output-on-failure -L unit`、`ctest --test-dir build-ci --output-on-failure -L contract`、`ctest --test-dir build-ci --output-on-failure -L integration`。
4. 最终 Gate 结果为：`ctest -N` 总测试数 `499`，`ctest -L unit` 为 `100% tests passed, 0 tests failed out of 296`，`ctest -L contract` 为 `100% tests passed, 0 tests failed out of 152`，`ctest -L integration` 为 `96% tests passed, 2 tests failed out of 51`。这两条剩余失败仅为既有跨模块 `InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest`；tools 自身 8 条 integration 用例全部通过。
5. 042 首次采样时，若干 tools unit/integration tests 曾显示 `Not Run`。根因是相应 build-ci executable 尚未物化，而不是 source CMake 漏接；补建这些 targets 时又短暂暴露终端 PATH 缺少 `/usr/bin`，导致 `c++` 无法调用 `as`。本轮通过显式 `PATH=/usr/bin:/bin:$PATH` 与定向 target 重建恢复 Gate，不需要修改任何 tools 产品代码。

## 2. Gate 执行证据

| Gate ID | 结论 | 命令证据 | 结果摘要 |
|---|---|---|---|
| Gate-TOOL-01 | PASS | `rg -n "Gate-TOOL-09|Gate-TOOL-10|TOOL-BLK-005|ToolProfileIntegrationTest|ToolServicesSmokeIntegrationTest" docs/architecture/DASALL_tools子系统详细设计.md docs/todos/tools/DASALL_tools子系统专项TODO.md` | tools 详设与专项 TODO 的 gate 编号、blocker 解阻口径、profile/discoverability 收口结论已对齐，且未把 runtime 生产 caller、generic MCP ready、external skill compatibility 误写成已完成事实 |
| Gate-TOOL-02 | PASS | `cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_contract_tests dasall_integration_tests`；`ctest --test-dir build-ci --output-on-failure -L unit` | tools/include、surface tests 与 unit 聚合 target 稳定，unit 296/296 全部通过 |
| Gate-TOOL-03 | PASS | `ctest --test-dir build-ci --output-on-failure -L contract` | ToolRequest / Result / Descriptor / IR 继续由 contract 152/152 全绿托底 |
| Gate-TOOL-04 | PASS | `ctest --test-dir build-ci --output-on-failure -L unit` | Registry / Validator / ConfigAdapter / PolicyGate / RouteSelector / ToolManager 等治理链单测全部通过，fail-closed 行为未回退 |
| Gate-TOOL-05 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | `ToolServicesSmokeIntegrationTest` 在聚合 integration 中通过，Tool -> Services -> Digest 最小闭环保持成立 |
| Gate-TOOL-06 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | `ToolWorkflowFailureIntegrationTest` 在聚合 integration 中通过，workflow failure / delegation sidecar / compensation_hints 证据未回退 |
| Gate-TOOL-07 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | `ToolMCPFallbackIntegrationTest` 与 `ToolPluginStdioMCPIntegrationTest` 在聚合 integration 中通过；generic MCP ready 仍保持 loopback / plugin-stdio hybrid 范围内的 No-Go 边界 |
| Gate-TOOL-08 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | `ToolObservabilityIntegrationTest` 在聚合 integration 中通过，audit / metrics / trace / health 字段完整性未回退 |
| Gate-TOOL-09 | PASS | `ctest --test-dir build-ci --output-on-failure -L integration` | `ToolProfileIntegrationTest` 在聚合 integration 中通过，desktop_full vs edge_minimal 的 timeout / allowed domains / visibility 差异仍被正确投影 |
| Gate-TOOL-10 | PASS | `ctest --test-dir build-ci -N` | build-ci discoverability 总测试数为 499，tools unit/integration 入口可被稳定发现 |

## 3. 阻塞变化与最小解阻

1. TOOL-BLK-001 ~ TOOL-BLK-005 已全部解除，042 启动时 tools 专项范围内已经没有未解阻的显式 blocker。
2. 首次 full gate 的 unit / integration 采样出现 `Not Run` 时，先检查了 `tests/unit/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/unit/tools/CMakeLists.txt` 与 build-ci 生成的 `build.make`；结论是 aggregate targets 已声明对缺失 tools executables 的依赖，问题在于产物尚未物化，而不是 source 接线遗漏。
3. 本轮最小解阻动作是在 build-ci 下定向重建缺失的 tools unit/integration targets，并在终端显式补齐 `PATH=/usr/bin:/bin:$PATH`，避免 `c++: fatal error: cannot execute 'as': execvp: No such file or directory` 的环境噪声再次干扰采样。
4. 解阻动作严格限制在 build/tests 侧 target 物化与环境 PATH 修正；没有修改 tools 公共 ABI、治理链语义、workflow/MCP/skill 行为，也没有放宽 integration 断言。
5. full integration 仍保留两条既有跨模块失败：`InfraDiagnosticsSmokeTest` 与 `InfraDiagnosticsIntegrationTest`。这两条失败不登记新的 tools blocker ID，也不改变 tools 专项 Gate PASS 结论。

## 4. 评审结论

1. TOOL-TODO-042 通过。tools 专项 Gate 的 configure/build/discoverability/unit/contract/integration 结果已全部回写到专项 TODO，并形成独立 deliverable 与 worklog 证据。
2. tools 专项 001~042 的串行链已在子系统范围内闭合；当前专项范围内不存在未解阻的 Build-ready 缺口。
3. 042 的完成态不等于允许把 generic MCP ready、external skill compatibility、runtime 生产 caller adapter 写成系统级已交付事实。相应结论仍分别受 loopback/plugin-stdio hybrid 证据、sample scope + feature flag、TOOL-TODO-023 fixture 边界约束。

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 9.2 Gate / 11.1 blocker / 12.2 next steps 收口 | 本文件、docs/todos/tools/DASALL_tools子系统专项TODO.md |
| full gate 期间的最小 unblock 记录 | build-ci 命令链与显式 PATH 重建过程 |
| 执行记录与后续边界说明 | docs/worklog/DASALL_开发执行记录.md |

## 6. Build 三件套

1. 代码目标：回写 tools 专项 Gate 的命令证据、阻塞变化、风险残留与完成态结论；如果证据采样被 build/tests 侧噪声阻塞，只允许做最小环境/产物解阻，不改写 tools 产品语义。
2. 测试目标：`ctest -N` 保持 discoverability，`ctest -L unit` / `-L contract` 恢复全绿，`ctest -L integration` 至少要把 tools 自身用例全部跑通，并把跨模块残余失败显式记录。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_tools dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci --output-on-failure -L integration`

## 7. 风险与回退

1. full integration 中的跨模块失败可能掩盖 tools 自身 Gate 结论；后续若再出现聚合失败，必须先区分 tools regression 与外部 baseline 噪声，再决定是否回退 tools 证据。
2. generic MCP ready 仍不是“任意 MCP server 已 ready”的同义词；若 future 要扩大兼容边界，必须新增独立任务扩展 loopback / launch / capability evidence，而不是复用 035/042 的结论外推。
3. external skill importer 仍只对 sample scope 与 feature flag 负责；若 future 要宣称更广的 dialect compatibility，必须以新的 sample matrix 与 integration gate 重新立题。