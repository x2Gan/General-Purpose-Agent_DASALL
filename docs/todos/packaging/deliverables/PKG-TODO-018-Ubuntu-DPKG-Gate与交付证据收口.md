# PKG-TODO-018 Ubuntu DPKG Gate 与交付证据收口

状态：Done
日期：2026-05-09
来源 TODO：docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md

## 1. 任务边界

1. 本任务只负责收口 v1 Ubuntu DPKG packaging gate、交付证据、残余风险与 worklog，不扩大到 APT 仓库发布、签名分发、gateway/simulator 包或非 Ubuntu/Debian 渠道。
2. 本任务允许为了让 Gate 07/08 真实通过而修正 installed-package 路径、package payload 与 smoke 断言，但不接管 runtime policy、recovery 或 orchestrator 所有权。
3. 本任务的完成条件是每条 packaging gate 都有正式命令、结果、可追溯证据与残余风险说明，且不得用 `null` virtualization 或 `--ignore-restrictions=isolation-machine` 代替 qemu authoritative `autopkgtest`。

## 2. Gate 结果总览

| Gate ID | 正式命令 | 结果 | 证据 |
|---|---|---|---|
| PKG-GATE-03 | `dpkg-buildpackage -us -uc -b` | PASS | 产出 `../dasall_0.1.0-1_all.deb`、`../dasall-cli_0.1.0-1_amd64.deb`、`../dasall-daemon_0.1.0-1_amd64.deb`、`../dasall-common_0.1.0-1_all.deb` 与 `../dasall_0.1.0-1_amd64.changes` |
| PKG-GATE-04/05/06 | `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh`；本轮补充 `bash scripts/packaging/pkg_smoke_install.sh` | PASS | fresh install、explicit start、upgrade/conffile、remove/purge 生命周期在 PKG-TODO-017 已闭合；本轮重新验证 installed-package `config validate` 与 local control-plane smoke |
| PKG-GATE-07 | `AUTOPKGTEST_QEMU_DISABLE_KVM=1 /usr/bin/autopkgtest /tmp/pkg018-adt-src ../dasall_0.1.0-1_amd64.changes -- /tmp/pkg018-adtshim/bin/autopkgtest-virt-qemu --timeout-reboot=180 /tmp/pkg018-autopkgtest/autopkgtest-noble-amd64.img` | PASS | `/tmp/pkg018-autopkgtest-minsrc-fixed.log`：`pkg-smoke-local-control-plane PASS`、`pkg-smoke-common-assets PASS`、`RC=0` |
| PKG-GATE-08 | `lintian ../dasall_0.1.0-1_amd64.changes` | PASS with warnings | `/tmp/pkg018-lintian.log`：`RC=0`；warnings 仅为 `initial-upload-closes-no-bugs` 与 `dasall-daemon: no-manual-page [usr/sbin/dasall-daemon]` |

## 3. 本轮修正

1. 修正 installed-package `dasall config validate --json` 的 daemon validate-only 默认路径：`apps/cli/src/config/ConfigPreflightChecker.h` 将默认 daemon binary 收敛到 `/usr/sbin/dasall-daemon`，并同步更新 `ConfigValidateWorkflowTest` 与 `ConfigOutputContractTest`。
2. 修正 `dasall-cli` package payload 来源：`debian/dasall-cli.install` 改为从 `debian/tmp/usr/bin/dasall` 安装，避免 `.deb` 继续携带旧 build tree binary。
3. 修正 `debian/tests/pkg-smoke-local-control-plane` 的 post-apply 断言边界：`config apply` summary 负责证明 secret ref 与 service action 投影；`config show` 当前不查询 live systemd/IPC，因此 post-apply `config show` 只验证 summary schema，服务状态继续由 `systemctl`、socket owner/mode、`dasall ping/readiness/version` 直接验证。
4. qemu 环境采用当前主机的临时 shim `/tmp/pkg018-adtshim/bin/autopkgtest-virt-qemu` 与 `AUTOPKGTEST_QEMU_DISABLE_KVM=1` 跳过 KVM 预检；这只影响本机 testbed 启动方式，不改变仓库内 `debian/tests` 的测试语义。

## 4. 关键证据

1. 单元与契约回归：
   - `build/vscode-linux-ninja/tests/unit/apps/cli/dasall-config_validate_workflow_unit_test`：PASS。
   - `build/vscode-linux-ninja/tests/contract/access/dasall-config_output_contract_test`：PASS。
2. `.deb` payload 校验：
   - `../dasall-cli_0.1.0-1_amd64.deb` 内 `/usr/bin/dasall` 已包含 `/usr/sbin/dasall-daemon` 字符串，证明 CLI payload 来自当前修正后的 staging binary。
3. installed-package 本地验证：
   - `bash scripts/packaging/pkg_smoke_install.sh`：PASS。
   - `sudo dasall config validate --json`：`RC=0`，输出包含 `"ok":true`、`"validate_only_passed":true`、`"validate_only_command":["/usr/sbin/dasall-daemon",...]` 与空 `failure_reasons`。
   - `sudo sh -x debian/tests/pkg-smoke-local-control-plane`：`/tmp/pkg018-host-local-control-plane-trace-fixed3.log`，`RC=0`。
4. authoritative qemu `autopkgtest`：
   - 使用最小 source tree `/tmp/pkg018-adt-src`，包含 `debian/control`、`debian/changelog`、`debian/source/format`、`debian/tests/control` 与两条 `debian/tests/pkg-smoke-*` 脚本。
   - 最终日志 `/tmp/pkg018-autopkgtest-minsrc-fixed.log` 显示 `pkg-smoke-local-control-plane PASS`、`pkg-smoke-common-assets PASS`、`RC=0`。
5. `lintian`：
   - `/tmp/pkg018-lintian.log` 显示 `RC=0`。
   - `initial-upload-closes-no-bugs` 属首次本地打包上传语义 warning；`dasall-daemon: no-manual-page [usr/sbin/dasall-daemon]` 为 daemon binary manpage 缺口，当前 v1 已为 operator-facing CLI 提供 `dasall.1`，daemon manpage 可作为后续 polish，不阻塞 package-ready gate。

## 5. Gate 结论

1. PKG-GATE-07 已闭合：authoritative qemu testbed 中 `pkg-smoke-local-control-plane` 与 `pkg-smoke-common-assets` 均 PASS，最终 `RC=0`。
2. PKG-GATE-08 已闭合：`lintian` 返回 `RC=0`，warning 均已评审且不构成 blocker。
3. PKG-BLK-05 已解阻：installed-package QA 已具 local lifecycle 与 qemu authoritative 双证据。
4. PKG-TODO-018 可标记为 Done，v1 packaging 当前可宣称 package gate closed / package-ready evidence complete。

## 6. 残余风险

1. qemu 本地运行依赖临时 shim 跳过 KVM 预检；CI 或具备 `/dev/kvm` 权限的环境应直接使用标准 `autopkgtest-virt-qemu`，不把该 shim 固化进仓库。
2. `lintian` 仍提示 daemon binary 缺 manpage；当前不阻塞 v1 package-ready，但后续若要求 daemon-facing operator docs 完整，应新增 `dasall-daemon(8)` 或等价说明。
3. package gate 证据依赖 `.changes` 与当前源码同源；后续执行 PKG gate 前必须先重新 `dpkg-buildpackage -us -uc -b`，避免复用 stale `.deb`。

## 7. 后续范围冻结

1. v1 packaging 不纳入 APT 仓库发布、GPG 签名托管、gateway/simulator 包、streaming/MQTT/WebSocket 包装。
2. v1 operator path 继续保持 root/sudo-only，不回退到 `0660 dasall group`。
3. `config show` live systemd/IPC 状态探测若后续要增强，应进入 CLI config 专项或 install-state probe 增量任务，不在 packaging smoke 中伪造该语义。