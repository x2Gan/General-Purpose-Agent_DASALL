# TUI-TODO-036 FTXUI installed-path Debian source/binary strategy review

状态：Done（FTXUI installed-path packaging review = Pass；vendored installed path = Reject）
日期：2026-05-24
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只复核 FTXUI 进入正式 installed-path / Debian release path 时的 source/binary strategy、license/copyright 责任、vendored copy 取舍与对 `TUI-TODO-031~034` 的影响。
2. 本任务不修改 `cmake/`、`apps/tui/`、`apps/cli/`、`debian/` 或 `scripts/packaging/` 的生产实现，也不提前新增正式 TUI installed target。
3. 本任务承接 `TUI-TODO-005` 第 5.4 节留下的 release-path 复核点；`TUI-TODO-005` 只关闭 third-party resolver/pin/private-link blocker，不等于 installed-path packaging review 已通过。

## 2. 本地事实

1. `cmake/DASALLThirdParty.cmake` 已固定 `DASALL_ENABLE_TUI_FTXUI=OFF`、FTXUI upstream URL、`v6.1.9` 对应 commit `5cfed50702f52d51c1b189b5f97f8beaf5eaa2a6`，并且只有打开该 option 才会触发 `dasall_resolve_dependency(ftxui)`。
2. `apps/tui/CMakeLists.txt` 已提供 `dasall_tui_apply_ftxui_private_dependency(target)`，仅允许 `apps/tui` 局部以 `PRIVATE` 方式链接 `ftxui::component`、`ftxui::dom`、`ftxui::screen`。
3. 当前 `debian/` 没有 FTXUI build dependency、binary dependency、copyright stanza 或 vendored source install rule；`grep_search` 对 `debian/**` 的 `ftxui|FTXUI` 没有命中。
4. 当前 `apps/tui` 仍只有 `dasall_tui_prototype`，且 prototype 默认 non-install；正式 installed `dasall` target 尚未由 `TUI-TODO-032` 落地。
5. `TUI-TODO-030` 已证明命令迁移必须同轮处理 CMake target、install 规则、manpage、README.Debian/postinst、autopkgtest、packaging scripts 与 release notes；因此 FTXUI installed-path review 必须在 `TUI-TODO-031~034` 前给出二值结论。

## 3. 外部事实

1. Debian Policy 4.13 `Embedded code copies` 明确：Debian packages 不应使用 convenience copies；如果相关代码已以库形式存在于 Debian archive，packaging 应确保 binary packages 引用 Debian 中的库，而不是使用 convenience copy。
2. Debian Policy 4.2 要求 source package 明确列出直接需要的 build-time dependencies，并确保在满足 build dependency 后能构建 working binaries。
3. Debian Policy 4.5 要求每个 binary package 安装对应 copyright / license 信息；若 DASALL 选择 vendored FTXUI，`debian/copyright` 必须承担额外 upstream license/copyright 归档责任。
4. Debian package tracker 当前存在 `ftxui` source package，版本 `6.1.9-1`，位于 main；其 binaries 包括 `libftxui-component6.1.9`、`libftxui-dom6.1.9`、`libftxui-screen6.1.9` 与 `libftxui-dev`。
5. Ubuntu Launchpad 当前存在 `ftxui` source package，latest upload 为 `6.1.9-1`，且 Ubuntu packages 可见 `libftxui-dev`、`libftxui-component6.1.9`、`libftxui-dom6.1.9`、`libftxui-screen6.1.9` 等 installed packages。
6. FTXUI upstream README 公开列出 Debian package、Ubuntu package、vcpkg、Conan、Arch、OpenSUSE、Nix 等分发渠道，并声明 CMake FetchContent 是一般上游接入推荐；这不覆盖 Debian installed-path 的 policy 约束。

## 4. 策略对比

| 策略 | 结论 | 取舍理由 |
|---|---|---|
| 正式 Debian installed path 使用 package-managed FTXUI（`Build-Depends: libftxui-dev`，runtime 依赖由 shlibs/substvars 或显式 binary dependency 承接） | Accept | 符合 Debian Policy 4.13；FTXUI 6.1.9 已存在 Debian testing/unstable 与 Ubuntu source/binary packages；能把 security update、license/copyright 和 ABI package responsibility 交给 distro package。 |
| prototype / developer build 继续允许 default-off resolver + submodule/local cache/FetchContent fallback | Accept for prototype only | 当前 `DASALL_ENABLE_TUI_FTXUI=OFF` 且 prototype non-install；该路径适合开发验证和离线 fallback，但不得被写成 release path。 |
| 正式 installed path 长期使用 `third_party/ftxui` 或 FetchContent convenience copy | Reject | 与 Debian Policy 4.13 的 embedded code copies 口径冲突；也会把 FTXUI copyright、security update 和 reproducibility 责任转嫁给 DASALL packaging。 |
| 将 FTXUI 变成全仓必经 dependency | Reject | 破坏 `TUI-TODO-005` 已冻结的 default-off / apps-tui-private dependency 边界；非 TUI build 不应因命令迁移而强制解析 FTXUI。 |
| 为 DASALL 自建 bundled static FTXUI binary | Reject | 同时放大 embedded copy、ABI、security 与 license 维护面，不符合当前最小 release path。 |

## 5. 冻结结论

1. 正式 Debian installed path 必须采用 package-managed FTXUI：在进入 `TUI-TODO-032/033` 时，Debian packaging 应增加 `libftxui-dev` 作为 build dependency，并让正式 TUI target 消费系统包提供的 component/dom/screen targets 或等价 CMake/pkg-config 表达。
2. 正式 release path 不允许长期使用 `third_party/ftxui`、local cache 或 FetchContent convenience copy；这些只允许保留为 developer/prototype fallback，且必须继续受 `DASALL_ENABLE_TUI_FTXUI=OFF` 默认值保护。
3. 若目标发行版缺少足够版本的 FTXUI package，正式命令迁移不能自动回退到 vendored installed path；正确处理是保持 Gate-TUI-08/Gate-TUI-09 Blocked，或单独开 packaging exception / backport 任务并更新 `debian/copyright`、`debian/README.source` 与 package smoke。
4. `apps/tui` 的 private dependency 边界不变：即便 installed path 改为 package-managed，FTXUI 也只能被正式 TUI target private link，不能泄漏到 contracts/access/runtime/llm/profiles/tools。
5. 当前 review 只让 B.0 第 5 项中的“FTXUI 依赖来源与 Debian 打包策略”转为 Pass；B.0 第 5 项仍包含 snapshot/IME/CJK 质量门，后者继续由 `BLK-TUI-006` 承接。

## 6. 对后续任务的约束

| 后续任务 | 新约束 | 验收入口 |
|---|---|---|
| `TUI-TODO-031` | formal sign-off 已完成；仅当 Gate-TUI-08 的 `BLK-TUI-006` manual gate 也满足时，才允许释放 CLI 产物名；本 review 本身不改 `apps/cli` | `cmake --build --preset vscode-linux-ninja --target dasall-cli` |
| `TUI-TODO-032` | 新增正式 TUI target 时不得复用 prototype installed path；必须显式保持 FTXUI private link，且 Debian/release 构建走 package-managed dependency | `cmake --build --preset vscode-linux-ninja --target dasall-tui` |
| `TUI-TODO-033` | 更新 `debian/control`、`debian/copyright`、install/manpage/README.Debian/postinst/autopkgtest/package smoke 时必须写入 package-managed FTXUI 结论；不得引入 vendored FTXUI install rule | `rg -n "ftxui|libftxui|Build-Depends|embedded code copies|vendored" debian docs/todos/tui/deliverables` |
| `TUI-TODO-034` | command routing tests 只能证明 `dasall`/`dasall-cli` 分流，不得绕过 packaging dependency review | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` |

## 7. Gate 回写

| Gate / Blocker | 当前状态 | 结论 |
|---|---|---|
| FTXUI installed-path packaging review | Pass | package-managed FTXUI 被采纳，vendored installed path 被拒绝；review 缺口已闭合。 |
| Gate-TUI-08 B.0 第 5 项：FTXUI 依赖来源与 Debian 打包策略 | Pass for packaging strategy | FTXUI release-path 来源与 Debian 策略已有结论；仍需独立完成 snapshot/IME/CJK quality gate。 |
| `BLK-TUI-006` | Open | 本任务不提供真实终端 IME / resize / composer human gate；该 blocker 继续作为 Gate-TUI-08 残余阻塞。 |
| `BLK-TUI-008` | Open | formal sign-off 已完成；因 `BLK-TUI-006` 未闭合，命令迁移任务 031~034 继续保持 Blocked。 |

## 8. 验证证据

1. `grep_search("ftxui|FTXUI", includePattern="debian/**")`
   - 结果：无命中；当前 Debian packaging 尚未声明 FTXUI dependency 或 vendored install surface。
2. `grep_search("ftxui|FTXUI|DASALL_ENABLE_TUI_FTXUI", includePattern="cmake/DASALLThirdParty.cmake")`
   - 结果：命中 default-off option、upstream URL、commit pin 与 resolver 入口。
3. `grep_search("ftxui|DASALL_ENABLE_TUI_FTXUI", includePattern="apps/tui/CMakeLists.txt")`
   - 结果：命中 `dasall_tui_apply_ftxui_private_dependency()` 与 `ftxui::component/dom/screen` private link helper。
4. 外部核验：Debian tracker 显示 `ftxui` source package `6.1.9-1`，binary packages 包含 `libftxui-dev` 与 versioned component/dom/screen libraries；Ubuntu Launchpad 显示 `ftxui` latest upload `6.1.9-1`，Ubuntu packages 可见 `libftxui-dev` 与对应 runtime libraries。

## 9. 结果

1. FTXUI installed-path Debian source/binary strategy review 已闭合：正式 release path 采纳 package-managed FTXUI，拒绝 vendored installed path。
2. 本任务没有解锁 `TUI-TODO-031`；formal sign-off 已完成，但 Gate-TUI-08 仍因 `BLK-TUI-006` 的真实终端人工质量门保持 Blocked。
3. 后续一旦 `BLK-TUI-006` 也有正式人工证据，`TUI-TODO-031~034` 必须按本 review 结论同步处理 Debian dependency/copyright/install/smoke，而不能只改 CMake target 名。