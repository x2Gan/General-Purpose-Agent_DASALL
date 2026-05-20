# TOOL-FIX-012 runtime production skill bridge 收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-GAP-010`。
2. 本轮目标：把 tools 子域已有的 `SkillRegistry` / `SkillRuntime` / importer 能力接入 runtime production live composition，使最小 internal skill 样本能经 `ToolManager -> WorkflowEngine -> BuiltinExecutorLane -> services` 热路径被导入、可见并执行。
3. 完成判定：`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 已装配 runtime-facing skill import 与 workflow `plan_loader`；desktop profile 已允许最小 skill workflow visible rule；`skills/` 已进入 install layout；daemon / gateway access tests 与 profile projection tests 已共同证明 `skill.runtime-state-snapshot` 的 visible surface、workflow route 和成功执行；本轮不依赖 qemu / kvm，也不把结果外推为 payload golden regression 或 machine-isolated release-ready。

## 2. 本地证据

1. `tools/src/skills/SkillRuntime.*`、`tools/src/skills/PluginSkillBundleImporter.*` 与 `tests/integration/tools/ToolSkillRuntimeIntegrationTest.cpp` 早已证明 module-local skill import / match / instantiate 可用，但 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 和 runtime `IToolManager` hot path 之前并未消费这些能力。
2. 本轮已更新 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`：新增 runtime skill asset import/filter helper、`RuntimeToolManagerComposition`、runtime-visible workflow descriptor 生成与基于 `SkillRuntime::instantiate()` 的 `WorkflowEngine` `plan_loader`；一旦最小 skill surface 接通，runtime evidence 会追加 `runtime:<owner>:skill-runtime-production-bridge`。
3. 本轮已新增 `skills/specs/runtime-state-snapshot.skill.yaml`、`skills/workflows/runtime-state-snapshot.workflow.yaml` 与 `skills/evals/runtime-state-snapshot.eval.yaml`：该样本只依赖当前 runtime live surface 已稳定存在的 `agent.dataset`，避免把尚无 concrete backend 的 canonical runtime / knowledge tool 样本误拉入 production proof。
4. 本轮已更新 `profiles/desktop_full/runtime_policy.yaml`，新增 `workflow` allowed domain 与 `workflow:skill.runtime-state-snapshot` visible rule；`tests/unit/tools/ToolConfigAdapterTest.cpp`、`tests/integration/tools/ToolProfileIntegrationTest.cpp` 与 `tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` 已同步固定 projection / compatibility contract。
5. 本轮已更新 `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 与 `tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp`：focused app composition tests 现在复制 `skills/` 资产、断言 `skill.runtime-state-snapshot` visible surface / workflow route / successful execution / evidence marker，并显式从本轮复制的 assets 根加载 profile，避免在本机存在旧安装态 profile 时误触发 `policy.domain_denied`。
6. 本轮已更新 `tools/CMakeLists.txt` 与 `debian/dasall-common.install`，把 `skills/` 纳入安装布局；runtime skill proof 不再只依赖源码树临时文件存在。

## 3. 设计结论

### 3.1 根因收口

1. `TOOL-GAP-010` 的真正缺口不是 skill importer 缺实现，而是 runtime production hot path 从未把 skill asset 变成 `ToolManager` 可见的 workflow descriptor，也没有给 `WorkflowEngine` 提供 `SkillRuntime` 驱动的 `plan_loader`。
2. 仅有 `ToolSkillRuntimeIntegrationTest` 并不能推出 runtime production 可用，因为它只覆盖 module-local import / instantiate，不覆盖 runtime live composition、profile policy、install layout 或 app composition。
3. 如果只在 runtime composition 里接 skill helper，而不同时补 profile workflow contract 与 `skills/` 安装布局，skill 仍会在 outer policy gate 或安装态验证中 fail-closed。
4. access tests 在本机已有旧安装态 profile 时，会通过 `resolve_install_layout()` 误读系统 profile 并假性命中 `policy.domain_denied`；因此 focused runtime tests 必须从本轮复制的 assets 根显式加载 profile，才能和当前源码变更保持同一 authoritative contract。

### 3.2 本轮决定

1. 采用最小 internal skill 样本 `skill.runtime-state-snapshot`，以 dataset-only workflow 证明 runtime production skill bridge，而不是把 richer skill catalog 一次性全部拉入 runtime proof。
2. runtime composition 继续拥有 skill visible surface 的裁定权；Tools 仍只负责 governed execution pipeline 和 module-local skill runtime，不接管 runtime owner。
3. desktop profile 仅对 `skill.runtime-state-snapshot` 打开 workflow visible rule，避免把整个 workflow domain 泛化成“任意 skill 都已 production-ready”。
4. `skills/` 安装布局与 access/profile tests 同步升级，确保 build-tree、profile 投影和 install contract 三者口径一致。

### 3.3 边界与不外推项

1. 本轮不把 `ResultProjector` complex payload golden regression 混入 skill bridge proof；`TOOL-GAP-011` 继续独立保留。
2. 本轮不新增 qemu / autopkgtest / soak 结论；`TOOL-GAP-012` 继续保留 machine-isolated release hardening 范围。
3. 本轮不把所有 canonical skill 样本都宣称为 runtime-ready；当前只对 `skill.runtime-state-snapshot` 给出 authoritative production bridge 结论。
4. 本轮不改变 Runtime / ToolManager / WorkflowEngine 的 owner 边界：Runtime 负责 live composition，Tools 负责受治理的 workflow execution，profiles 负责 workflow visible contract。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | runtime live composition 必须把 internal skill asset 变成 runtime-visible workflow descriptor，并为 WorkflowEngine 提供 skill-driven `plan_loader` | `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D2 | runtime production proof 需要最小 internal skill 样本，而不是依赖当前 runtime live surface 尚不完整的 canonical sample | `skills/specs/runtime-state-snapshot.skill.yaml`、`skills/workflows/runtime-state-snapshot.workflow.yaml`、`skills/evals/runtime-state-snapshot.eval.yaml` |
| D3 | desktop profile 必须显式允许最小 workflow skill，否则 outer policy gate 会 fail-closed | `profiles/desktop_full/runtime_policy.yaml`、`tests/unit/tools/ToolConfigAdapterTest.cpp`、`tests/integration/tools/ToolProfileIntegrationTest.cpp`、`tests/integration/agent_loop/RuntimeProfileCompatibilityTest.cpp` |
| D4 | access/runtime focused tests 必须证明 skill visible surface、workflow route、successful invoke 与 evidence marker，并避免误读本机旧安装态 profile | `tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp`、`tests/integration/access/GatewayRuntimeLiveDependencyCompositionTest.cpp` |
| D5 | install layout 必须把 `skills/` 纳入 common data assets | `tools/CMakeLists.txt`、`debian/dasall-common.install` |
| D6 | gap closeout 结论必须回写总账、deliverable 索引与工作日志 | 本文档、`docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：runtime live composition 接通 `SkillRuntime -> WorkflowEngine -> ToolManager` 的 runtime-facing bridge，并把 `skills/` 纳入 desktop profile / install layout contract。
2. 测试目标：daemon / gateway access tests 证明 runtime skill visible + invoke + evidence；profile projection / compatibility tests 证明 workflow domain 与 visible rule；安装布局变更不引入 compile/backward regression。
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_access_gateway_runtime_live_dependency_composition_integration_test","dasall_tool_config_adapter_unit_test","dasall_tool_profile_integration_test","dasall_runtime_profile_compatibility_integration_test"])`
   - `RunCtest_CMakeTools(tests=["DaemonRuntimeLiveDependencyCompositionTest","GatewayRuntimeLiveDependencyCompositionTest","ToolConfigAdapterTest","ToolProfileIntegrationTest","RuntimeProfileCompatibilityTest"])`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_gateway_runtime_live_dependency_composition_integration_test`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_tool_config_adapter_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/tools/dasall_tool_profile_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/agent_loop/dasall_runtime_profile_compatibility_integration_test`

## 6. Rollout Checklist

1. runtime live dependency set 已可同时广告 builtin tool surface 与最小 runtime skill workflow surface。
2. `skill.runtime-state-snapshot` 已可经 governed workflow route 成功执行，不再停留在 tools module-local integration。
3. desktop profile 已显式允许该 workflow skill，profile projection / compatibility tests 未回退。
4. `skills/` 已进入安装布局，避免 runtime skill proof 只存在于源码树。
5. access tests 已从本轮复制的 assets 根显式加载 profile，避免受本机旧安装态 profile 漂移干扰。
6. 本轮未使用 qemu / kvm，也未把结果外推为 richer skill catalog、payload golden regression 或 machine-isolated release PASS。

## 7. 风险与回退

1. 若只接通 runtime composition 而不更新 profile / install layout，skill 会在 outer policy gate 或安装态验证中继续 fail-closed。
2. 若 access tests 继续依赖 `resolve_install_layout()` 读取全局 profile，本机已安装旧 package 时会假性命中 `policy.domain_denied`，导致源码树已修复却无法收敛 focused validation。
3. 若把本轮最小 skill bridge 误写成“所有 skill 已 production-ready”，后续 richer workflow sample 的缺口会被错误吞并。
4. 若把本轮 direct-binary fallback 误写成 `RunCtest_CMakeTools` 已正常可用，会掩盖仓库当前已知的泛化 `生成失败` 工具态问题。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 runtime composition、skill assets、profile policy、install layout、access/profile tests 与文档回写。
3. Build 三件套已在本机完成；`RunCtest_CMakeTools` 的已知泛化失败已通过 direct-binary fallback 收口 focused validation。
4. 范围保持在 runtime production skill bridge，不扩张到 payload golden regression 或 qemu/soak。

结论：D Gate = PASS；`TOOL-GAP-010` 已按 runtime production skill bridge、profile/install contract 与 focused build-tree evidence 收口。