# TUI-TODO-001 启动身份与权限模型决策

状态：Done
日期：2026-05-22
来源 TODO：docs/todos/tui/DASALL_TUI客户端专项TODO-2026-05-13.md

## 1. 任务边界

1. 本任务只冻结 TUI 启动身份、权限不足行为、是否引入 user-level daemon/socket，以及这些结论对命令迁移的直接约束。
2. 本任务不实现 `apps/tui` 生产代码，不修改 daemon socket policy，不提前释放 bare `dasall`，也不新增 user-level daemon/socket。
3. 本任务只在 TUI 范围内继承既有 owner 已冻结的安装态事实，不重开 runtime、access、packaging 的 owner 边界。

## 2. 本地事实与证据

1. `docs/architecture/DASALL_TUI客户端设计方案.md` 第 5.7 节已明确：普通用户无权访问 socket 时，TUI 必须展示 `permission denied` 与当前 operator 模型说明，且不允许在 TUI 中要求用户输入 sudo 密码。
2. `docs/todos/cli/deliverables/CLCFG-TODO-002-socket与operator-model冻结.md` 已冻结安装态 canonical socket=`/run/dasall/daemon.sock`、访问模型=`0600 root/sudo-only`，并要求 package/postinst/operator docs 维持非交互、不可隐式提权的 operator 路径。
3. `docs/worklog/DASALL_开发执行记录.md` 已记录普通用户执行 `dasall ping/readiness` 会因 `/run/dasall/daemon.sock` 返回 `Permission denied`，而 `sudo -n dasall ping/readiness` 返回 completed/READY；这说明当前 installed local control-plane 事实是 root/sudo-only operator path，而不是普通用户主路径。
4. `docs/ssot/BinaryEntrypointReadinessV1.md` 已冻结 entrypoint readiness 只能按 accepted/degraded/stub/default/bridge/health 分层投影。TUI startup 不能把 transport permission denied 涂抹成 ready，也不能把 prototype/fake path 冒充 daemon-backed availability。
5. 本轮对仓内 `apps/**`、`services/**`、`runtime/**` 的检索没有发现 `XDG_RUNTIME_DIR`、user-level daemon 或 per-user control socket 的现成实现。user-level daemon/socket 不是“补一句文案”即可启用的现有路径。

## 3. 外部参考

1. Debian Policy 第 6 章要求 maintainer scripts 保持 idempotent，并指出 maintainer scripts 不保证拥有 controlling terminal，通常必须能够非交互运行。这意味着 Debian 安装态不能把“为普通用户临时提权、交互式改组、在安装期为 TUI 偷开访问”作为默认路径。
   - 参考：https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html
2. XDG Base Directory Specification 规定 `$XDG_RUNTIME_DIR` 是用户专属 runtime 目录，所有者必须是该用户，权限必须是 `0700`，AF_UNIX sockets 等 runtime file objects 应放在这里。这说明未来如果要支持普通用户 full-function TUI，对应的 user-level daemon/socket 应是独立的 user-runtime 设计流，而不是继续复用系统级 `/run/dasall/daemon.sock`。
   - 参考：https://specifications.freedesktop.org/basedir-spec/latest/

## 4. 冻结结论

1. TUI v1 的 daemon-backed 路径继承当前安装态 operator 模型：canonical socket 固定为 `/run/dasall/daemon.sock`，访问策略固定为 `0600 root/sudo-only`。
2. 普通用户可以运行 fake/no-daemon 的 `dasall_tui_prototype` 或其他仅供样品评审的本地 UI 壳层，但这些路径必须明确标注为 prototype，不得宣称具备真实 daemon/operator 能力。
3. 当普通用户启动 daemon-backed TUI 且无权访问 `/run/dasall/daemon.sock` 时，TUI 必须返回可判定的 `permission denied` 启动问题或 explanatory limited mode；文案必须说明当前 operator 模型，而不是把错误伪装成“系统暂时繁忙”或“未知失败”。
4. 在上述 `permission denied` 场景下，TUI 明确禁止以下行为：
   - 在 TUI 内要求用户输入 sudo 密码。
   - 自动调用 `sudo`、`pkexec` 或其他提权链路。
   - 自动启动 system daemon、修改 system config、修改 group membership。
   - 静默切换到未冻结的 user-level daemon/socket。
5. TUI v1 不引入 user-level daemon/socket。若未来要支持普通用户无 sudo 的 full-function TUI，必须单独冻结：`$XDG_RUNTIME_DIR` 路径、用户态 daemon 生命周期、Access session seam、packaging/onboarding、reason code、测试门禁与安全说明。
6. TUI-TODO-001 只冻结权限模型，不授权 bare `dasall` 命令迁移。正式把 `dasall` 交给 TUI，仍需等待 TUI-TODO-024、030 及后续命令/packaging gate 完成。

## 5. 启动场景矩阵

| 场景 | 冻结行为 | 明确禁止 | 对命令迁移的影响 |
|---|---|---|---|
| 普通用户启动 fake/no-daemon prototype | 允许进入本地 prototype；必须显式标注 fake/prototype，不宣称 operator ready | 连接系统 daemon socket 后再伪装 ready | 不影响 bare `dasall`；仅服务小样评审 |
| 普通用户启动 daemon-backed TUI 且命中 `permission denied` | 返回稳定的 `permission denied` reason，展示 root/sudo-only operator 说明和文档引导 | TUI 内提权、改组、偷开 user-level daemon/socket | bare `dasall` 继续 Blocked；不能把该路径包装成默认 end-user ready |
| root/sudo operator 启动 daemon-backed TUI | 允许继续走真实 daemon/access projection 路径 | 绕过 projection/health/readiness gate 直接宣称 production ready | 仅说明权限模型闭合，不等于命令迁移已放行 |
| daemon 未运行 | 进入 `daemon unavailable` 启动问题或等价 degraded explanation | 自动启动 system daemon | 仍需 TUI-TODO-024 定义 startup mode 与 next step |
| 未来 user-level daemon/socket 方案 | 本任务不承诺，保持 future-only | 以“与当前 `/run/dasall/daemon.sock` 共用同一设计”名义偷渡上线 | 需要独立冻结后，才允许重开 bare `dasall` end-user path 讨论 |

## 6. 对后续任务的直接约束

1. TUI-TODO-024 必须把 `permission denied` 作为单独 reason code / startup issue 处理，不得并入泛化的 daemon unavailable。
2. TUI-TODO-030 必须继续把“普通用户默认主路径是否成立”视为命令迁移 gate，而不是因 TUI-TODO-001 已 Done 就提前释放 bare `dasall`。
3. TUI-TODO-031~034 后续进行命令迁移时，operator 脚本路径要按迁移方案切到 `dasall-cli`；因此 permission denied 文案应引用 operator 文档/受控帮助文本，不应把某个过渡期命令名硬编码成永恒事实。
4. Debian/postinst/README/manpage 不得为了“让普通用户直接进 TUI”而引入交互式提权、默认 group 变更或隐式 user-level daemon 启动。

## 7. Design -> Build 映射

| 后续任务 | 锁定的代码目标 | 锁定的测试目标 | 锁定的验收命令 |
|---|---|---|---|
| TUI-TODO-024 | `apps/tui/src/app/TuiApp.cpp`、`apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp` 需要显式区分 `permission denied` / `daemon unavailable` / non-TTY / narrow | `TuiAppStartupFailureTest` | `ctest --preset vscode-linux-ninja -R "TuiAppStartupFailure" --output-on-failure` |
| TUI-TODO-030 | `docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` 必须把 root/sudo-only、user-level daemon future-only 与 bare `dasall` Blocked 口径写入 gate evidence | design consistency | `rg -n "B.0|权限模型|projection|selector|packaging smoke|dasall-cli|/usr/bin/dasall|debian|scripts/packaging|inventory" docs/todos/tui/deliverables/TUI-TODO-030-command-release-gate-evidence.md` |
| TUI-TODO-031~034 | 命令释放、formal target、Debian/script 迁移与 command routing tests 必须建立在本文件冻结的权限边界之上 | `CliControlPlaneCommandNameTest`、`DasallCommandRoutingTest`、package smoke | `ctest --preset vscode-linux-ninja -R "DasallCommandRouting|CliControlPlane" --output-on-failure` |

## 8. D Gate 结果

1. 本地事实、SSOT 和已冻结 operator 模型已对齐到单一结论：当前真实 daemon/operator backend 是 `root/sudo-only`，而不是普通用户主路径。
2. user-level daemon/socket 已被明确降格为 future-only 设计流，不再作为本轮可隐式展开的未决项。
3. 后续 Build 三件套已锁定到 TUI-TODO-024、030、031~034，对应 code target、test target 和 acceptance command 已可追踪。

结论：TUI-TODO-001 D Gate = PASS。本任务为文档决策任务，无独立 B 阶段；完成条件是 deliverable、专项 TODO、总账与 worklog 口径同步回写。