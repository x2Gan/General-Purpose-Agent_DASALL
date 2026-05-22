# TUI-TODO-005 FTXUI 接入评审

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只校验 FTXUI third-party 来源、版本与 commit pin、submodule/local cache/FetchContent fallback 顺序、`apps/tui` private dependency 边界，以及 Debian install path 的风险口径。
2. 本任务不实现 `FtxuiRendererAdapter`、snapshot harness、CJK/IME/resize manual gate，不提前声明 `dasall_tui_prototype` 或正式安装态 `dasall` target，也不把 `apps/tui` 接入 `apps/CMakeLists.txt`。
3. 本任务只解 `BLK-TUI-005`。`BLK-TUI-006` 的终端样品 gate 仍然存在；`TUI-TODO-019`、`TUI-TODO-020` 仍需后续 focused tests 和 manual review 才能转入 renderer/full-screen 小样。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 8.2~8.4 节已冻结：FTXUI 是首选候选，但必须遵守 DASALL 统一 third-party 治理，并且只能作为 `apps/tui` private dependency，不能泄漏到 access、runtime、contracts、llm、profiles、tools。
2. `third_party/README.md` 与 `docs/architecture/DASALL_架构设计文档.md` 已明确当前仓库 third-party 解析优先级为 `submodule > local cache > FetchContent`，目标是保持离线优先和可重复配置。
3. `cmake/DASALLThirdParty.cmake` 在本轮前已经提供通用 `dasall_resolve_dependency()`，并用 `cpp-httplib` 演示了统一接入入口；缺的不是另一套 ad-hoc FetchContent，而是 FTXUI 专用的受控 resolver 入口与 pin。
4. `apps/CMakeLists.txt` 当前仍未 `add_subdirectory(tui)`；这说明 TUI prototype target 还不属于本任务范围，FTXUI 也不应在本轮被做成全局必拉依赖。
5. `docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md` 已明确：TUI-TODO-005 的完成判定是依赖评审文档明确来源与回退策略，并把 FTXUI 限定在 `apps/tui`，未通过时只阻塞 renderer/snapshot/full-screen 小样，不阻塞 no-daemon skeleton。

## 3. 外部参考

1. FTXUI 官方 README 当前明确把 CMake FetchContent 作为推荐接入方式，并给出 `ftxui::ftxui` 及 `ftxui::component` / `ftxui::dom` / `ftxui::screen` 的链接目标；README 同时标示最新 release 为 `v6.1.9`，并指出已有 Debian / Ubuntu package 分发渠道。
   - 参考：https://github.com/ArthurSonzogni/FTXUI
2. CMake FetchContent 文档明确建议先声明依赖细节再统一 `FetchContent_MakeAvailable()`，并指出使用精确 commit hash 比仅使用 tag/branch 更安全、更可复验；这支持本轮对 FTXUI 做 commit pin，而不是停留在浮动 tag。
   - 参考：https://cmake.org/cmake/help/latest/module/FetchContent.html
3. Debian Policy 4.13 `Embedded code copies` 明确指出：若某段代码已在 Debian archive 中以库形式存在，Debian packaging 不应默认继续使用 convenience copy；若尚未入 archive，也应尽量单独打包，而不是长期依赖私有 vendored copy。结合 FTXUI 官方 README 中已存在 Debian package 的事实，说明 FTXUI 可以在 prototype 阶段通过仓内 resolver 验证，但正式 installed path 仍必须经过 packaging review，不能直接把未评审的 vendored copy 当作 release 策略。
   - 参考：https://www.debian.org/doc/debian-policy/ch-source.html#embedded-code-copies

## 4. 接入方案对比

| 方案 | 结论 | 取舍理由 |
|---|---|---|
| 直接在 `apps/tui/CMakeLists.txt` 手写 `FetchContent_Declare(ftxui ...)` | Reject | 会绕过仓库统一 third-party 治理，也会让 TUI 成为新的依赖入口 owner，破坏 `cmake/DASALLThirdParty.cmake` 的唯一解析口径。 |
| 全局无条件 `dasall_resolve_dependency(ftxui)` | Reject | 当前 `apps/tui` 尚未接入 `apps/CMakeLists.txt`；无条件解析会把 FTXUI 提前拉入非 TUI 构建路径，扩大 configure/build 面，与 TUI-TODO-007 的 no-daemon skeleton 分阶段策略冲突。 |
| 受控的 default-off resolver + `apps/tui` private link helper | Accept | 既保留统一 third-party 入口和 commit pin，又避免在 TUI target 尚未落盘前污染全局构建；同时能把 private dependency 规则提前固化为本地 CMake 约束。 |
| 直接把 Debian/Ubuntu 系统包作为本轮唯一前提 | Reject for this round | 适合后续 packaging/release 评审，但当前阶段 `apps/tui` 尚未进入安装态，且 no-daemon prototype 不应被 Debian packaging 先决条件卡死。 |

## 5. 冻结结论

### 5.1 来源顺序与版本 pin

1. FTXUI 的仓内解析入口冻结在 `cmake/DASALLThirdParty.cmake`，继续复用现有优先级：`third_party/ftxui` submodule -> `third_party/.cache/ftxui` local cache -> `FetchContent` fallback。
2. FTXUI upstream 仓库固定为 `https://github.com/ArthurSonzogni/FTXUI.git`。
3. 本轮锁定的 release 锚点为 `v6.1.9`；对应精确 commit 为 `5cfed50702f52d51c1b189b5f97f8beaf5eaa2a6`。
4. CMake 侧使用 commit pin，而不是只用 tag 名称，原因是 FetchContent 官方文档明确建议精确 commit 以提高安全性和可复验性。

### 5.2 CMake 落盘策略

1. `cmake/DASALLThirdParty.cmake` 新增 `DASALL_ENABLE_TUI_FTXUI`，默认值固定为 `OFF`。
2. 只有在 TUI target 真正需要 renderer 依赖时，后续任务才允许显式打开 `DASALL_ENABLE_TUI_FTXUI=ON` 并触发 `dasall_resolve_dependency(ftxui)`。
3. 这一定义保证了：TUI-TODO-005 可以把 FTXUI resolver 和 version pin 固化下来，但不会把 FTXUI 提前变成当前全部 build 的 configure side effect。
4. 若开发者处于严格离线环境，submodule/local cache 缺失且 `DASALL_ALLOW_FETCHCONTENT=OFF`，则 FTXUI resolver 应保持 fail-closed，不允许改写为隐式网络回退或新的 ad-hoc 下载脚本。

### 5.3 `apps/tui` private dependency 规则

1. `apps/tui/CMakeLists.txt` 在本轮新增 `dasall_tui_apply_ftxui_private_dependency(target)` helper，用于把 `ftxui::component`、`ftxui::dom`、`ftxui::screen` 以 `PRIVATE` 方式链接到后续 TUI target。
2. 本 helper 在目标不存在或 FTXUI 尚未解析时都会 fail-fast；这保证 TUI target 接线时不会静默退化为“看起来链接了、实际没有 FTXUI”。
3. FTXUI target 只能在 `apps/tui` 局部被引用。access、runtime、contracts、llm、profiles 等模块不得直接 `target_link_libraries(... ftxui::...)`，也不得 include `ftxui/*` 头。
4. 后续 `TuiFtxuiDependencyBoundaryTest` 的职责已冻结：扫描 include/link 边界，确保 FTXUI 不泄漏到 `apps/tui` 之外；本轮只锁测试目标和规则，不提前声明测试拓扑。

### 5.4 Debian policy 与 release 风险口径

1. prototype/no-daemon skeleton 阶段可以继续使用仓内 third-party resolver 验证 FTXUI build path，因为此阶段不进入 installed `dasall` 迁移，也不构成 Debian release 结论。
2. 进入 `TUI-TODO-030~034` 命令迁移与 packaging 阶段前，必须单独复核 FTXUI 的 Debian source/binary strategy、copyright/license 安排和是否继续允许 vendored copy。
3. 因为 FTXUI upstream 已明确存在 Debian/Ubuntu package 分发渠道，正式 release path 不能默认假设“长期 vendored copy 一定可接受”；若 packaging review 未通过，bare `dasall` 命令迁移必须继续 Blocked。

## 6. Design -> Build 映射

| 后续任务 | 锁定的代码目标 | 锁定的测试目标 | 锁定的验收命令 |
|---|---|---|---|
| `TUI-TODO-007` | 可继续创建 no-daemon/mock-renderer skeleton；若该轮仍不接 FTXUI，可保持 `DASALL_ENABLE_TUI_FTXUI=OFF` | `TuiPrototypeBuildSmokeTest` | `cmake --build --preset vscode-linux-ninja --target dasall_tui_prototype` |
| `TUI-TODO-019` | renderer adapter target 接线必须经 `dasall_tui_apply_ftxui_private_dependency()`，不得绕回 ad-hoc FetchContent | `TuiFtxuiDependencyBoundaryTest`、`TuiMainLayoutSnapshotTest` | `ctest --preset vscode-linux-ninja -R "Tui(FtxuiDependencyBoundary|MainLayoutSnapshot)" --output-on-failure` |
| `TUI-TODO-020` | full-screen fake-only app 只有在 `TUI-TODO-019` 与 `BLK-TUI-006` 通过后才允许打开 `DASALL_ENABLE_TUI_FTXUI` 进入 renderer loop | `TuiPrototypeSmokeTest` | `ctest --preset vscode-linux-ninja -R "Tui(AppStartup|PrototypeSmoke)" --output-on-failure` |
| `TUI-TODO-030~033` | 正式命令迁移和 Debian 安装态必须额外复核 FTXUI 是否继续采用 vendored/submodule/local cache，还是切换到 package-managed 依赖 | package smoke / command routing | `rg -n "ftxui|Debian|embedded code copies|package" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md debian scripts` |

## 7. 本轮最小 Build 落地

本任务不是纯文档冻结，还包含一组最小 CMake 锚点，用于把评审结论沉淀到仓内可复用规则，而不提前展开 TUI scaffold：

1. `cmake/DASALLThirdParty.cmake` 已新增 `DASALL_ENABLE_TUI_FTXUI`、FTXUI upstream URL、精确 commit pin，以及受控的 `dasall_resolve_dependency(ftxui)` 入口。
2. `apps/tui/CMakeLists.txt` 已新增 `dasall_tui_apply_ftxui_private_dependency(target)` helper，冻结 `ftxui::component` / `ftxui::dom` / `ftxui::screen` 只能被 `apps/tui` 以 `PRIVATE` 方式链接。
3. `apps/CMakeLists.txt` 仍未接入 `apps/tui`；这保持了 TUI-TODO-007 对 prototype target 的 owner 边界，也保证本轮不会把 FTXUI 误做成全局必经依赖。

## 8. 验证证据

1. `git ls-remote https://github.com/ArthurSonzogni/FTXUI.git refs/tags/v6.1.9 refs/tags/v6.1.9^{}`
   - 结果：命中 `5cfed50702f52d51c1b189b5f97f8beaf5eaa2a6`；用于锁定本轮 FTXUI version/commit 锚点。
2. `rg -n "FTXUI|ftxui|FetchContent|submodule|local cache|DASALL_ENABLE_TUI_FTXUI" cmake/DASALLThirdParty.cmake`
   - 结果：通过；命中 FTXUI resolver 入口、source priority 文案、default-off 开关与 commit pin。
3. `rg -n "ftxui|private|apps/tui|DASALL_ENABLE_TUI_FTXUI" apps/tui/CMakeLists.txt`
   - 结果：通过；命中 `apps/tui` 局部 helper、private-link 规则与 dependency-ready fail-fast 提示。
4. `get_errors([cmake/DASALLThirdParty.cmake, apps/tui/CMakeLists.txt])`
   - 结果：通过；两份 CMake 文件均无新的诊断错误。

## 9. Gate 结果

1. FTXUI third-party 来源、版本/commit、offline fallback 与 `apps/tui` private dependency 规则已经形成唯一口径。
2. `BLK-TUI-005` 可以关闭；后续 `TUI-TODO-019`、`TUI-TODO-020` 不再因第三方接入策略未冻结而保持 Blocked。
3. `BLK-TUI-006` 仍保持 Open；本轮没有把 third-party 评审误写成 renderer ready、snapshot ready、CJK/IME ready 或 bare `dasall` release ready。

结论：TUI-TODO-005 D Gate = PASS，最小 B 锚点 = PASS。本任务完成条件已满足，但后续 renderer/full-screen 路径仍需终端样品 gate 与 focused tests。