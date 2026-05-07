# PKG-TODO-002 冻结 package-mode profile 选择接缝与配置 ownership

状态：Done
日期：2026-05-07
来源 TODO：docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md

## 1. 任务边界

1. 本任务只冻结 Ubuntu DPKG package-mode 下 `profile_id` 选择入口、`/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json` 的 ownership，以及 `systemd` unit 到 daemon 入口参数的唯一接缝。
2. 本任务不在 002 阶段扩展新的 deployment config schema，不提前实现 `debian/` 文件，不触碰 socket 默认值、install layout owner、LLM 资产路径或 fresh-install UX 的完整落地，这些分别继续由 004、009、010、011、012、013、014 承接。
3. 本任务的输出必须满足 ADR-006/007/008 与 packaging 约束：打包层只承载部署/运维输入面，不接管 runtime profile 选择逻辑本身。

## 2. 当前代码事实

1. `apps/daemon/src/main.cpp` 当前只接受 `--profile-id`、`--config-file`、`--socket-path` 三类 package-mode 相关入口参数；daemon 启动参数解析中没有“从 deployment config 读取 profile_id”的旁路。
2. `apps/daemon/src/DaemonEntryConfigLoader.cpp` 在 `load()` 中把 `request.requested_profile_id` 直接传给 `DaemonProfileProjectionRequest` 与 `RuntimePolicyLoadRequest`，说明 profile 选择发生在 request/CLI 接缝，而不是 deployment config parser 内。
3. 同一文件中的 YAML/JSON deployment parser 只读取 `daemon` 对象并生成 `daemon.*` 标量键；overlay 冲突检测也只显式覆盖 `daemon.socket_path`，没有 `profile_id` 相关分支。
4. `tests/unit/apps/daemon/DaemonEntryConfigProjectionTest.cpp` 已实证三点：
   - 默认路径通过 `requested_profile_id=desktop_full` 投影 profile。
   - deployment YAML/JSON 只覆盖 `daemon.socket_path`、`receipt_ttl_sec`、`override_enabled`、`log_format`、`diag_enabled` 等 `daemon.*` 键。
   - 当前测试中不存在“从 deployment config 读 profile_id”的通过用例。
5. Debian `systemd.exec(5)` 明确 `EnvironmentFile=` 会在进程执行前读取环境变量文件；`systemd.service(5)` 明确 `ExecStart=`/`ExecStartPre=` 支持环境变量替换。因此，v1 采用 `EnvironmentFile -> ExecStart --profile-id` 是与现有 daemon 代码最贴合、且无需扩 schema 的最小接缝。

## 3. 冻结结论

### 3.1 v1 package-mode 输入模型

1. `/etc/default/dasall-daemon` 是 v1 唯一的 package-owned profile 选择入口，owner 为 `dasall-daemon` binary package 的可编辑系统配置文件。
2. 该文件在 v1 只承载一个稳定键：`DASALL_DAEMON_PROFILE_ID=<profile-id>`。
3. `debian/dasall-daemon.service` 负责把 `DASALL_DAEMON_PROFILE_ID` 映射到 daemon CLI：
   - `ExecStartPre=/usr/sbin/dasall-daemon --validate-only --profile-id=${DASALL_DAEMON_PROFILE_ID} --config-file /etc/dasall/daemon.json`
   - `ExecStart=/usr/sbin/dasall-daemon --profile-id=${DASALL_DAEMON_PROFILE_ID} --config-file /etc/dasall/daemon.json`
4. `Environment=DASALL_DAEMON_PROFILE_ID=desktop_full` 作为保底默认值可以保留在 unit 中；`EnvironmentFile=-/etc/default/dasall-daemon` 作为运维可编辑入口必须保留。

### 3.2 `/etc/dasall/daemon.json` ownership

1. `/etc/dasall/daemon.json` 是 v1 唯一的 daemon deployment override conffile。
2. 该文件只承载 `daemon.*` 覆盖项，不承载 `profile_id`。
3. v1 允许的键集合以当前 parser/overlay 能力为边界，例如 `daemon.socket_path`、`daemon.startup_mode`、`daemon.diag_enabled`、`daemon.override_enabled`、`daemon.watchdog_enabled`、`daemon.log_format`、`daemon.receipt_ttl_sec`、`daemon.dispatch_workers` 等。
4. 任何“把 profile_id 同时写入 `/etc/default/dasall-daemon` 与 `/etc/dasall/daemon.json`”的双入口方案都不采纳，因为它会把 package-mode 输入面重新变成双写口径。

### 3.3 daemon 入口 ABI 冻结

1. `--profile-id` 与 `--config-file` 在 v1 继续作为 package-mode 的 canonical daemon entry ABI。
2. systemd unit 是这组 ABI 的编排层，而不是 schema owner；schema owner 仍然分别是：
   - profile 选择：`/etc/default/dasall-daemon`
   - daemon 覆盖：`/etc/dasall/daemon.json`
3. 只有当后续专门增量设计明确扩展 deployment config schema，并补齐对应 parser、tests 与 upgrade/conffile 迁移策略时，`profile_id` 才允许迁入 config file。该工作不属于 v1 packaging 002。

## 4. 明确不采纳的方案

1. 不采纳“在 `/etc/dasall/daemon.json` 新增 `profile_id` 字段后让 `DaemonEntryConfigLoader` 同时支持双入口”的折中方案，因为当前没有对应 parser/test/upgrade 迁移闭环。
2. 不采纳“依赖 `WorkingDirectory=/usr/share/dasall` 或其它 cwd 技巧隐式决定 profile/asset 行为”的伪收敛方案；profile 选择必须显式经过 `--profile-id`。
3. 不采纳“在 `postinst` 或 `debconf` 中现场询问 profile”方案；首次部署仍保持非交互、显式启用。

## 5. 对后续任务的直接约束

1. PKG-TODO-009 创建 `debian/dasall-daemon/etc/default/dasall-daemon` 时，只允许落 `DASALL_DAEMON_PROFILE_ID=desktop_full` 这一 profile 入口，不得把 `socket_path`、`config_file` 或其它 daemon 覆盖键塞进 `/etc/default`。
2. PKG-TODO-009 / 013 创建 `debian/dasall-daemon/etc/dasall/daemon.json` 时，必须保持文件内容仅含 `daemon.*` 键，且文件扩展名为 `.json` 时内容必须是 JSON。
3. PKG-TODO-013 的 postinst / README.Debian / service 模板必须共享同一验证路径：先读取 `/etc/default/dasall-daemon`，再把 `DASALL_DAEMON_PROFILE_ID` 透传给 `--profile-id` 与 `/etc/dasall/daemon.json` 组合校验。
4. 若未来确有 schema 扩展需求，必须新开增量任务，同时更新 `DaemonEntryConfigLoader`、projection tests、conffile 迁移与 README.Debian，而不是直接改写 002 已冻结的 v1 接缝。

## 6. 推荐的最小运维命令口径

1. profile 选择：编辑 `/etc/default/dasall-daemon` 中的 `DASALL_DAEMON_PROFILE_ID`。
2. daemon 覆盖：编辑 `/etc/dasall/daemon.json` 中的 `daemon.*` 键。
3. 校验命令：

   `. /etc/default/dasall-daemon && /usr/sbin/dasall-daemon --validate-only --profile-id="${DASALL_DAEMON_PROFILE_ID}" --config-file /etc/dasall/daemon.json`

4. 显式启动：

   `systemctl enable --now dasall-daemon.service`

## 7. 验证口径

1. 设计验收使用以下命令：

   `rg -n "DASALL_DAEMON_PROFILE_ID|/etc/default/dasall-daemon|/etc/dasall/daemon.json|EnvironmentFile|profile_id|daemon\.\*" docs/architecture/DASALL_Ubuntu平台DPKG打包方案设计.md docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md docs/todos/packaging/deliverables/PKG-TODO-002-package-mode-profile接缝与配置ownership冻结.md`

2. 通过标准：
   - `profile_id` 入口统一收敛为 `/etc/default/dasall-daemon` + `EnvironmentFile` + `--profile-id`。
   - `/etc/dasall/daemon.json` 被明确约束为 `daemon.*` conffile。
   - 设计文档、专项 TODO 与 002 deliverable 三处不再出现互相冲突的 ownership 口径。