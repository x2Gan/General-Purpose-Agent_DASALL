# CLCFG-TODO-002 冻结 P0 安装态 socket canonical path、root/sudo-only operator access model 与命令安装名

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/cli/DASALL_cli_config交互式部署配置专项TODO.md

## 1. 任务边界

1. 本任务只冻结 P0 安装态 daemon socket canonical path、operator access model 与安装态公开命令名，不提前实现 `DaemonEndpointDefaults`、`DaemonSocketPolicy`、postinst 或 manpage 代码。
2. 本任务直接消费 `DMD-TODO-037` 已完成的真实 socket mode=`0600` 事实，并把它收敛为 CLI config / packaging / operator docs 的统一设计输入。
3. 本任务不把 `0660 dasall group` 方案做成当前承诺；该方案只作为 future evolution note 保留，若未来重开，必须新建冻结任务并同步 systemd、daemon、packaging、tests 与 docs。
4. 本任务不展开 `InstallState`、`ConfigActionPlan` 或文件事务字段设计；这些由 CLCFG-TODO-003 单独收口。

## 2. 当前事实与冲突来源

1. `docs/architecture/DASALL_cli_config交互式部署配置设计.md` 在本任务开始前仍把安装态 socket path、socket 权限模型与安装态命令名写成“待冻结项”，导致 wizard、OperatorAccessPage、README.Debian 与 postinst 都可能各说各话。
2. `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md` 中 `DMD-TODO-037` 已明确：真实 daemon bind 后的 socket mode 已收敛到 `0600`，且相关 focused gate 已通过。这意味着 package/config 层若继续承诺 `0660 dasall group`，就会与已验证实现面冲突。
3. `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md` 在本任务开始前仍保留 `PKG-TC013=socket 默认 0660` 的表述，且安装后 operator 文案还没有收敛到唯一的 `dasall config` 入口。
4. 外部参考也支持把 group access 视为安装后的显式运维动作，而不是安装默认语义：
   - Debian maintainer scripts 要求 maintainer scripts 保持非交互、幂等，不应在安装阶段偷偷改写操作者本地会话模型。
   - Docker Linux post-install 文档明确把“加入 `docker` 组以便非 root 使用”放在安装后的显式步骤，并提示其具有 root 级权限和重新登录要求。这进一步说明：若 DASALL 将来开放 group access，也必须作为显式、可审计、带安全警示的后续动作，而不是 P0 默认主路径。

## 3. 冻结结论

### 3.1 安装态 canonical socket path

1. P0 package-mode daemon 的唯一 public default socket path 冻结为 `/run/dasall/daemon.sock`。
2. `/run/dasall/control.sock` 不作为 v1 安装态对外默认 endpoint；若未来需要第二控制面 socket，必须另起设计冻结并同步 CLI/daemon/profile/package 文档。
3. README.Debian、postinst next steps、`OperatorAccessPage`、CLI 安装态示例、systemd/unit 相关文档都必须使用 `/run/dasall/daemon.sock` 这一唯一值。

### 3.2 P0 operator access model

1. `DMD-TODO-037` 的真实 socket mode=`0600` 是当前唯一可信实现事实，因此 P0 operator access model 冻结为 `root/sudo-only`。
2. P0 `config` wizard、`OperatorAccessPage`、README.Debian、postinst 与 operator docs 只能提示 sudo/root 运维路径，不得把“加入 `dasall` 组即可访问 daemon socket”写成当前版本主路径。
3. P0 不执行 `groupadd`、`usermod`、`gpasswd` 等用户组变更；postinst 也不得默认操作本地用户组。
4. `0660 dasall group` 只保留为 future evolution note。若未来重开，必须同步更新 `UnixIpcProvider`、`DaemonSocketPolicy`、systemd `Group=` / `UMask`、autopkgtest、README.Debian、manpage 与 CLI operator flow，并新建冻结任务。

### 3.3 命令安装名映射

1. 开发态命令与源码 target 继续使用 `dasall-cli config`。
2. 安装态正式包的公开命令名固定为 `/usr/bin/dasall`，对外 operator workflow 一律写作 `dasall config`。
3. README.Debian、postinst next steps、manpage、首次部署文档与 package smoke 提示不得再使用安装态 `dasall-cli` 文案。

## 4. 对后续 Build 的直接约束

1. PKG-TODO-013 的 postinst hint 必须建议 `sudo dasall config`，同时不得再建议 `usermod -aG dasall ...` 作为 P0 主路径。
2. PKG-TODO-014 的 README.Debian / manpage / operator onboarding 必须统一到 `dasall config` + `0600 root/sudo-only` 语义。
3. CLCFG-TODO-010/012/013 的 preflight、show/plan/validate/apply 和 `OperatorAccessPage` 必须把“当前用户对 `/run/dasall/daemon.sock` 是否有权限”视为只读探测结果，而不是让 wizard 即时修改 group membership。
4. 若未来要支持 non-root group access，必须先通过独立冻结把 docs、daemon、systemd、tests 与 packaging 一次性收口，再进入实现阶段。

## 5. 验证口径

1. 设计验收使用以下命令：

   `rg -n "0600 root/sudo-only|0660 dasall group|后续演进|/run/dasall|/usr/bin/dasall|dasall-cli" docs/architecture/DASALL_cli_config交互式部署配置设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md docs/todos/cli/deliverables/CLCFG-TODO-002-socket与operator-model冻结.md`

2. 通过标准：
   - `/run/dasall/daemon.sock` 成为唯一安装态 canonical path。
   - `0600 root/sudo-only` 成为当前唯一 P0 operator model。
   - `0660 dasall group` 只出现在“后续演进项”语境，不再被写成当前 Must/默认值。
   - `dasall-cli` 仅保留开发态含义，安装态公开命令统一映射为 `dasall config`。