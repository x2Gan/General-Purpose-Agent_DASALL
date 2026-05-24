# TUI-TODO-035 TUI 交付证据回写

状态：Done
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界与前置检查

1. 本任务只处理 TUI 阶段 deliverable、Gate 状态、worklog 与专项 TODO 的长期证据闭环，不新增 `apps/tui`、`apps/cli`、`debian/` 或 `scripts/packaging/` 的生产实现。
2. 前置依赖复核：`TUI-TODO-020` 已闭合 fake-only `TuiApp` 小样基线，`TUI-TODO-029` 已闭合 next preference submit echo focused integration evidence，`TUI-TODO-030` 已闭合 bare `dasall` 迁移门禁证据与旧入口 inventory。
3. 本任务不会把命令迁移实现误写成已完成。`TUI-PROTO-017` 已补齐采纳 / 延后 / 废弃清单，`TUI-PROTO-017-formal-sample-signoff.md` 已完成 Product / Engineering formal sign-off，`TUI-TODO-036` 已补齐 FTXUI installed-path packaging review，`BLK-TUI-006` 已由用户确认 Full pass 关闭；Gate-TUI-08 已转 Pass，但 `TUI-TODO-031~034` 的代码、安装、文档和 smoke 迁移仍需逐项执行。
4. 本任务属于 `TUI-TODO-031` 的 blocker recovery 分支：目标是先把阶段证据与 gate 状态滚动回写，再重新判断原任务 031 是否恢复可执行。

## 2. 长期输出面

本任务的正式输出面固定为四类长期资产：

1. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md`
2. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
3. `docs/worklog/DASALL_开发执行记录.md`
4. `docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md`

## 3. 命令证据

> 说明：本任务回写的 Build / Test 结果均来自前置已完成任务的实际执行记录；若 `RunCtest_CMakeTools` 命中仓库已知泛化 `生成失败`，继续沿用仓库既有回退口径，以显式 `ctest` 或直接测试二进制作为 authoritative 结果。

1. `Build_CMakeTools(buildTargets=["dasall_tui_prototype"])`
2. `Build_CMakeTools(buildTargets=["dasall_tui_app_startup_integration_test","dasall_tui_prototype_smoke_integration_test"])`
3. `ctest --test-dir build/vscode-linux-ninja --output-on-failure -R '^(TuiAppStartupTest|TuiPrototypeSmokeTest)$'`
4. `Build_CMakeTools(buildTargets=["dasall_tui_next_preference_integration_test","dasall_tui_integration_topology_smoke_integration_test"])`
5. `./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_next_preference_integration_test && ./build/vscode-linux-ninja/tests/integration/tui/dasall_tui_integration_topology_smoke_integration_test`
6. `rg -n "B.0|权限模型|projection|selector|packaging smoke|dasall-cli|/usr/bin/dasall|debian|scripts/packaging|inventory" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md`
7. `rg -n "Gate-TUI-PROT-06|采纳|延后|废弃|formal|BLK-TUI-006" docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md`
8. `rg -n "package-managed|vendored|Debian Policy 4.13|embedded code copies|Gate-TUI-08|BLK-TUI-006|libftxui-dev" docs/todos/tui/deliverables/TUI-TODO-036-ftxui-installed-path-packaging-review.md`

结果摘要：

1. `TUI-TODO-020` 已提供 fake-only prototype 的 focused build + smoke 证据，证明 `dasall_tui_prototype` 可构建、可运行，并保持 non-install / no-command-release 边界。
2. `TUI-TODO-029` 已提供 selector draft -> `submit_turn` payload -> owner route/receipt echo 的 focused integration 证据；`Auto`、`PreferDepth`、`PinModel` 三模式均有二值结果。
3. `TUI-TODO-030` 已提供 Gate-TUI-08 所需的 B.0 判定、旧入口 inventory 与命令迁移兼容矩阵；在 `BLK-TUI-006` Full pass 后，Gate-TUI-08 已转 Pass，但这仍不是 `TUI-TODO-031~034` 已实现。
4. `TUI-PROTO-017` 已提供样品采纳 / 延后 / 废弃清单，`TUI-PROTO-017-formal-sample-signoff.md` 已完成 formal sign-off；`TUI-TODO-036` 已提供 FTXUI installed-path Debian source/binary strategy review；`BLK-TUI-006-manual-terminal-evidence.md` 已提供用户确认的真实终端 Full pass。
4. 当前仓库针对 TUI 的工具态回退口径已经稳定：`RunCtest_CMakeTools` 若命中泛化 `生成失败`，权威结果回退到显式 `ctest` 或直接二进制执行；本轮继续沿用这一口径，不重复宣称工具态问题等于测试失败。

## 4. 采纳、延后与回退

### 4.1 采纳

1. 采纳 `TUI-TODO-020` 的 fake-only `dasall_tui_prototype` 作为 TUI 当前阶段唯一被接受的 app baseline：允许 startup / smoke / renderer snapshot 证据进入正式 TUI TODO 与 worklog，但不外推为 installed `dasall` ready。
2. 采纳 `TUI-TODO-029` 的 next preference submit echo 作为 Gate-TUI-07 的正式 focused integration 证据：后续 route selector 真实链路不再只依赖 unit/contract 片段推断。
3. 采纳 `TUI-TODO-030` 的 command release gate evidence 作为 Gate-TUI-08 的唯一 inventory / compat matrix 口径：后续 `TUI-TODO-031~034` 必须以该交付物列出的 CMake、Debian、autopkgtest 与 packaging scripts 影响面为准。
4. 采纳 `TUI-PROTO-017` 的等价样品评审资产：主布局、transcript、composer、selector、status panel 与场景基线已有采纳 / 延后 / 废弃清单，且 formal product/engineering sign-off 已由 `TUI-PROTO-017-formal-sample-signoff.md` 完成。
5. 采纳 `TUI-TODO-036` 的 FTXUI installed-path packaging review：正式 Debian release path 使用 package-managed FTXUI，拒绝长期 vendored installed path。

### 4.2 延后

1. 不再延后 Gate-TUI-08 复检；`BLK-TUI-006` 已关闭，Gate-TUI-08 已转 Pass。
2. 不再延后 `BLK-TUI-006` 的 CJK/IME/resize manual gate；当前证据以用户确认 Full pass + 自动化 self-check/snapshot 闭合。
3. 继续延后 `TUI-TODO-031~034` 的命令迁移实现到各自原子任务；本文件只关闭 gate，不改动 `apps/cli/CMakeLists.txt`、Debian 文件或 packaging scripts。

### 4.3 回退

1. 当 `RunCtest_CMakeTools` 对 TUI focused tests 返回仓库已知泛化 `生成失败` 时，验证回退到显式 `ctest --test-dir build/vscode-linux-ninja ...` 或直接执行对应测试二进制，并把该回退路径作为 authoritative evidence 记录在 deliverable / worklog。
2. 对原任务 `TUI-TODO-031` 的执行判断仍采用 gate-driven 策略：`BLK-TUI-006` 已闭合后，`TUI-TODO-031` 恢复为下一原子任务；释放 `dasall-cli` 产物名仍必须在 031 中单独落代码、测试和验收。

## 5. Gate 回写结论

| Gate | 当前状态 | 正式命令 / 证据 | 长期交付物路径 | 当前状态说明 | 后继动作 | 残余风险 |
|---|---|---|---|---|---|---|
| Gate-TUI-05 | Pass | `TUI-TODO-020` deliverable + `TUI-PROTO-017` 等价样品评审资产 + `TUI-PROTO-017-formal-sample-signoff.md` + `BLK-TUI-006-manual-terminal-evidence.md` | `docs/todos/tui/deliverables/TUI-PROTO-017-样品评审证据.md` | fake-only prototype、采纳/延后/废弃清单、formal sign-off 与真实终端 Full pass 均已完成 | 进入 Gate-TUI-08 复检与 `TUI-TODO-031` | 不得把 prototype 结论外推为 `TUI-TODO-031~034` 已实现 |
| Gate-TUI-07 | Pass | `Build_CMakeTools(buildTargets=["dasall_tui_next_preference_integration_test","dasall_tui_integration_topology_smoke_integration_test"])` + 显式二进制回退执行 | `docs/todos/tui/deliverables/TUI-TODO-029-next-preference提交回显.md` | selector truth chain 已有 focused integration evidence，`PreferDepth` advisory 与 `PinModel` fail-closed 语义已被 owner echo 固定 | 后续仅在 app-level interactive submit UX 成熟时补更高层 smoke | 当前证据仍是 focused integration，不等于 full app submit UX 已 ready |
| Gate-TUI-08 | Pass | `rg -n "B.0|权限模型|projection|selector|packaging smoke|dasall-cli|/usr/bin/dasall|debian|scripts/packaging|inventory" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` + `TUI-PROTO-017` + `TUI-PROTO-017-formal-sample-signoff.md` + `TUI-TODO-036` + `BLK-TUI-006-manual-terminal-evidence.md` | `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` | B.0 第 1/2/3/4/5 项均有证据，样品清单、formal sign-off、FTXUI packaging review 与 `BLK-TUI-006` Full pass 已补齐 | 进入 `TUI-TODO-031`，先释放 `dasall-cli` 产物名 | 若跳过 031~034 的逐项实现，`dasall` / `dasall-cli` breaking change 仍会把半截 build/install/docs/smoke 迁移带入主分支 |
| Gate-TUI-10 | Pass | 本任务 deliverable + worklog + TODO / 总账回写 | `docs/todos/tui/deliverables/TUI-TODO-035-交付证据回写.md` | 020/029/030 的 focused command、结果、残余风险与后继动作现已形成长期证据闭环 | 后续仅在 Gate 状态或正式命令发生变化时再开新 closeout 任务 | 若后续 TODO / worklog / deliverable 只改其一，TUI Gate 口径会再次漂移 |

## 6. 原任务复检

1. 原始目标 `TUI-TODO-031` 的 blocker 类型属于依赖阻塞：专项 TODO 明确把 031 挂在 `BLK-TUI-008` 后；本轮关闭 `BLK-TUI-006` 后，`BLK-TUI-008` 可关闭，031 恢复为下一原子任务。
2. 本轮最小 blocker 修复动作是完成 `TUI-TODO-035`，把 020/029/030 与 Gate-TUI-07/08 的阶段证据回写为长期资产；该动作现在已完成。
3. 修复后重新判断：`TUI-TODO-031` 已恢复可执行。原因已收敛：
   - `TUI-PROTO-017` 样品评审证据包与 `TUI-PROTO-017-formal-sample-signoff.md` 已完成，B.0 第 1 项可转为 Pass。
   - FTXUI installed-path Debian source/binary strategy review 已由 `TUI-TODO-036` 闭合，`BLK-TUI-006-manual-terminal-evidence.md` 已选择 Full pass，B.0 第 5 项可转为 Pass。
4. 因此 `apps/cli/CMakeLists.txt` 可由下一原子任务 `TUI-TODO-031` 修改；本轮文档 closeout 仍不直接改动 `set_target_properties(dasall-cli PROPERTIES OUTPUT_NAME dasall)`。

## 7. 结果与下一步

1. `TUI-TODO-035` 已完成：TUI 当前阶段的关键 Gate 证据现已集中回写到 deliverable、专项 TODO、总账与 worklog，不再依赖口头结论或分散记录。
2. `TUI-TODO-031` 在本轮 blocker recovery 后恢复为下一原子任务；本轮不会推进 `dasall-cli` 产物名释放。
3. 最小下一步现已收敛为一条：执行 `TUI-TODO-031`，释放 `dasall-cli` 产物名并补对应命令名测试；formal sample sign-off、FTXUI installed-path Debian source/binary strategy review 与 `BLK-TUI-006` manual terminal gate 均已不再是 031 的阻塞项。