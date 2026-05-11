# FULLINT-TODO-013 installed-package 控制面 + 主功能矩阵

日期：2026-05-11
来源任务：FULLINT-TODO-013
范围：fresh package build、fresh reinstall、explicit service start、CLI/daemon local control plane、installed LLM 主功能、status/cancel/diag fail-closed、Knowledge/tools installed-package 缺口

## 1. D 阶段结论

本轮只推进 `FULLINT-TODO-013`，不合并 `FULLINT-TODO-014` 的 Knowledge installed-package 正向入口实现，也不提前执行 `FULLINT-TODO-019` 的 release runner / qemu authoritative gate。

任务可执行性判定：PASS。

1. 前置 `FULLINT-TODO-002` 已冻结 package 证据分层，并明确 fresh reinstall package smoke 留给本任务。
2. 前置 `FULLINT-TODO-007` 已解 `FULLINT-BLK-003`，readiness no-stub 与 secret permission fail-closed 有 build-tree 证据。
3. `FULLINT-BLK-001` 仍是 release runner / qemu / secret 注入的 L5 阻塞，不阻止本轮 L4 local installed-package smoke，但禁止外推 production release-ready。
4. `FULLINT-BLK-002` 仍是 Knowledge retrieve / refresh / health 独立正向入口缺口；本轮只做缺口断言，不实现入口。
5. 本机具备本轮 L4 前提：`dpkg-buildpackage`、`dpkg-query`、`systemctl`、`lintian`、`autopkgtest`、`qemu-system-x86_64`、`dasall` 均存在；`sudo -n true` 通过；`secret://llm/providers/deepseek-prod` 对应 secret 文件存在。

## 2. 研究输入

### 2.1 本地证据

| 输入 | 本轮采用方式 |
|---|---|
| `scripts/packaging/pkg_smoke_install.sh` | 作为 local fresh reinstall smoke 主入口；必须实际执行 `--explicit-start-check`。 |
| `debian/tests/pkg-smoke-local-control-plane` | 作为 qemu/autopkgtest 同构 installed-package 断言参考；本轮不执行 qemu。 |
| `scripts/packaging/README.md` | 只采用其中 package smoke 断言口径，不采用历史完成状态。 |
| `runtime/src/AgentOrchestrator.cpp` | 用源码确认 installed `run` 的 `llm.origin=` 与 `agent.dataset fallback is disabled` 行为来自 runtime production LLM path。 |
| `access/src/AccessGatewayFactory.cpp`、`access/src/daemon/DaemonDiagnosticsHandler.cpp` | 用源码确认 `status_missing`、`cancel_missing`、`diag_disabled` 的 error_ref 来源。 |
| installed CLI probes | 用实际 `dasall --help`、`dasall knowledge --help`、`dasall diag health --json` 断言 installed Knowledge/diag 边界。 |

### 2.2 外部参考

| 参考 | 对本任务的约束 |
|---|---|
| Debian `autopkgtest(1)` man page | `autopkgtest` 是在 testbed 中测试已安装二进制包；`.changes` 可携带本地 `.deb`；qemu 由 `-- virt-server` 提供。因此本轮 local smoke 不能冒充 qemu authoritative gate。 |
| Debian package tests metadata model | `Restrictions: needs-root, isolation-machine` 与本仓库 `debian/tests/control` 匹配，说明 package control-plane smoke 本质上需要 rootful / machine isolation 环境。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 冻结 L4 local 与 L5 qemu 的边界 | `FULLINT-BLK-001`、`validate_gate_int_10_installed_package_qemu.sh`、Debian `autopkgtest(1)` | 文档明确 local smoke 不外推 qemu / production ready | 若 qemu image 不存在，保持 Blocked 给 019 |
| D2 | 锁定 fresh reinstall smoke 矩阵 | `pkg_smoke_install.sh`、`debian/tests/pkg-smoke-local-control-plane` | 矩阵包含 package set、validate-only、explicit start、ping/readiness、run、status/cancel、diag、assets | 若 smoke 失败，按真实失败转 Validation Blocker |
| D3 | 锁定人工 LLM 主功能探针 | TODO 验收命令、runtime `llm.origin=` 输出 | `sudo -n dasall run ...` 返回 completed、`task_completed=true`、含 `llm.origin=deepseek-prod/` 且无 `agent.dataset` | 外部 provider/network 抖动只记录为环境/验证阻塞，不伪造通过 |
| D4 | 锁定 Knowledge/tools 缺口断言 | `FULLINT-BLK-002`、CLI help、diag 默认门控 | installed package 不暴露 retrieve/refresh/health 正向入口时记录 Partial/Blocked，不宣称 ready | 实现入口转交 014/016，不在本轮扩张 |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| 本轮必须 freshly rebuild Debian artifacts | `dpkg-buildpackage -us -uc -b` | 命令 exit 0，生成当前版本 `.deb` / `.changes` artifacts |
| 本轮必须 freshly reinstall package set | `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | 命令 exit 0，脚本内完成 install、validate-only、explicit start、ping/readiness、run、status/cancel、diag、assets 断言 |
| 本轮必须人工复核生产 LLM prompt | `sudo -n dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000` | `disposition=completed`、`task_completed=true`、`llm.origin=deepseek-prod/`、无 `agent.dataset` |
| 本轮必须记录 Knowledge/tools installed gap | `sudo -n dasall knowledge --help`、`sudo -n dasall diag health --json`、installed command surface | 没有 retrieve/refresh/health 正向入口时保持 `FULLINT-BLK-002`，不宣称 installed knowledge/tools ready |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 仅推进 `FULLINT-TODO-013`，不实现 014/019。 |
| 前置依赖 | PASS | `FULLINT-TODO-002`、`FULLINT-TODO-007` 已 Done；`FULLINT-BLK-003` 已解。 |
| 本地环境前提 | PASS | 工具、sudo、secret 当前可用。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令已在 Design -> Build 映射中锁定。 |

## 6. B 阶段执行结果

### 6.1 Build 原子清单

| 原子项 | 代码目标 | 测试目标 | 验收命令 | 结果 |
|---|---|---|---|---|
| B1 | `scripts/packaging/pkg_smoke_install.sh` 既有 installed-package smoke | fresh package build artifact 可安装 | `dpkg-buildpackage -us -uc -b` | PASS，`/tmp/dasall-fullint013/dpkg-buildpackage-final.exit` 记录 `dpkg_build_exit=0` |
| B2 | `scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | fresh reinstall、validate-only、explicit service start、control-plane、LLM run、status/cancel/diag fail-closed | `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PASS，`/tmp/dasall-fullint013/pkg_smoke_install.exit` 记录 `pkg_smoke_exit=0` |
| B3 | installed CLI / daemon 真实控制面 | 人工复核 ping/readiness/run/status/cancel/diag | `sudo -n systemctl enable --now dasall-daemon.service` 后逐项执行 `dasall` 命令 | PASS，人工矩阵全部有 JSON / exit-code 证据 |
| B4 | installed command surface | Knowledge/tools installed-package 缺口断言 | `dasall --help`、`sudo -n dasall knowledge --help`、`sudo -n dasall tools --help` | PASS as gap，help surface 未暴露 retrieve/refresh/health 或 tools 正向入口 |

本轮没有修改 production 代码或 shell 脚本。原因是现有 smoke harness 已包含本任务要求的 package assertions；本轮交付重点是按真实 fresh build / fresh install / installed daemon 运行结果完成矩阵和证据回写。

### 6.2 package build 与 artifact

| 项 | 结果 |
|---|---|
| package build | `dpkg-buildpackage -us -uc -b`：PASS，退出码 `0` |
| artifacts | `../dasall-cli_0.1.0-1_amd64.deb`、`../dasall-common_0.1.0-1_all.deb`、`../dasall-daemon_0.1.0-1_amd64.deb`、`../dasall_0.1.0-1_all.deb`、`../dasall_0.1.0-1_amd64.changes`，时间戳 `2026-05-11 17:20` |
| metadata | `dasall-cli` / `dasall-daemon` version `0.1.0-1` arch `amd64`；`dasall-common` version `0.1.0-1` arch `all` |

说明：本轮曾启动过一次重复的 async package build，并在前一轮静默 build 已有 `dpkg_build_exit=0` 后主动终止，避免它与 installed smoke 争用 build directory。采信的 package build 证据是 `/tmp/dasall-fullint013/dpkg-buildpackage-final.log` 与 `/tmp/dasall-fullint013/dpkg-buildpackage-final.exit`。

### 6.3 fresh reinstall smoke

`bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 结果：PASS。

关键日志摘要：

1. preserved existing DeepSeek secret for reinstall smoke；未记录 secret 值。
2. previous DASALL install state reset，四包重新 `dpkg -i`。
3. `dasall-daemon --validate-only` 使用 installed defaults 通过。
4. daemon 未自动启动；只有 explicit `systemctl enable --now dasall-daemon.service` 后进入服务状态。
5. 脚本内部完成 `ping`、`readiness`、`run`、missing receipt `status/cancel`、default `diag`、LLM assets 断言。
6. `pkg_smoke_exit=0`。

smoke 退出后脚本按设计清理服务状态；当轮快照显示四包仍为 `install ok installed`，daemon `inactive` / `disabled`。后续人工矩阵重新显式启动 daemon。

### 6.4 人工 installed-package 控制面矩阵

| 探针 | 命令 | 实际结果 | 判定 |
|---|---|---|---|
| service start | `sudo -n systemctl enable --now dasall-daemon.service` | daemon `active` / `enabled` | PASS |
| socket policy | `sudo -n stat -c '%U:%G %a %n' /run/dasall/daemon.sock` | `dasall:dasall 600 /run/dasall/daemon.sock` | PASS |
| package set | `dpkg-query -W -f='${binary:Package} ${Version} ${Status}\n' 'dasall*'` | `dasall`、`dasall-cli`、`dasall-common`、`dasall-daemon` 均为 `0.1.0-1 install ok installed` | PASS |
| ping | `sudo -n dasall ping --json` | `disposition=completed`、`task_completed=true`；payload readiness `DEGRADED` | PASS |
| readiness | `sudo -n dasall readiness --json` | `disposition=completed`、`state=DEGRADED`、`bridge_reachable=true`、`runtime_readiness=degraded-ready` | PASS，不能外推 default-ready |
| run / LLM | `sudo -n dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000` | `disposition=completed`、`task_completed=true`、`llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`，响应内容 `2` | PASS |
| dataset fallback guard | `grep -F agent.dataset /tmp/dasall-fullint013/run-manual-llm.json` | 未命中，`agent_dataset_absent` | PASS |
| status missing receipt | `sudo -n dasall status receipt:missing token local://uid/0 --json` | exit `5`，`error_ref=status_missing`、domain `receipt` | PASS |
| cancel missing receipt | `sudo -n dasall cancel receipt:missing token local://uid/0 --json` | exit `5`，`error_ref=cancel_missing`、domain `receipt` | PASS |
| diag default gate | `sudo -n dasall diag health --json` | exit `4`，`error_ref=diag_disabled`、domain `authorization` | PASS |
| installed profile assets | `rg -n "multi_agent:" /usr/share/dasall/profiles/.../runtime_policy.yaml` | `desktop_full` 与 `cloud_full` 均为 `multi_agent: false` | PASS，FULLINT-TODO-004 安装态资产差异已由 fresh reinstall 复核收口 |

### 6.5 Knowledge / tools installed-package 缺口

| 探针 | 结果 | 判定 |
|---|---|---|
| `dasall --help` | 只列出 `help`、`version`、`config`、`ping`、`readiness`、`run`、`status`、`cancel`、`diag` | No positive Knowledge/tools surface |
| `sudo -n dasall knowledge --help` | exit `0`，但输出仍是通用 CLI usage，未出现 `knowledge`、`retrieve`、`refresh` | `FULLINT-BLK-002` 继续成立 |
| `sudo -n dasall tools --help` | exit `0`，但输出仍是通用 CLI usage，未出现 tools 正向入口 | tools installed ready 不能由 package run 外推 |
| `sudo -n dasall diag health --json` | default config 下 exit `4` / `diag_disabled` | 不能借默认 diag 证明 Knowledge health 正向入口 |

结论：本轮证明 installed-package 控制面与 LLM 主功能 L4 local smoke 通过；Knowledge retrieve / refresh / health 和 tools runtime production caller 仍分别归 `FULLINT-TODO-014`、`FULLINT-TODO-016`，不得外推为 installed-package ready。

### 6.6 验收命令与证据路径

| 命令 | 结果 | 证据路径 |
|---|---|---|
| `dpkg-buildpackage -us -uc -b` | PASS | `/tmp/dasall-fullint013/dpkg-buildpackage-final.log`、`/tmp/dasall-fullint013/dpkg-buildpackage-final.exit` |
| `bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check` | PASS | `/tmp/dasall-fullint013/pkg_smoke_install.log`、`/tmp/dasall-fullint013/pkg_smoke_install.exit` |
| `sudo -n dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000` | PASS | `/tmp/dasall-fullint013/run-manual-llm.json`、`/tmp/dasall-fullint013/run-manual-llm.exit` |
| `sudo -n dasall status receipt:missing token local://uid/0 --json` | PASS / expected reject | `/tmp/dasall-fullint013/status-missing.json`、`/tmp/dasall-fullint013/status-missing.exit` |
| `sudo -n dasall cancel receipt:missing token local://uid/0 --json` | PASS / expected reject | `/tmp/dasall-fullint013/cancel-missing.json`、`/tmp/dasall-fullint013/cancel-missing.exit` |
| `sudo -n dasall diag health --json` | PASS / expected reject | `/tmp/dasall-fullint013/diag-health-default.json`、`/tmp/dasall-fullint013/diag-health-default.exit` |
| `dasall --help` / `dasall knowledge --help` / `dasall tools --help` | PASS as gap evidence | `/tmp/dasall-fullint013/cli-help.txt`、`knowledge-help.txt`、`tools-help.txt`、`help-surface-grep.txt` |

## 7. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码注释 | 本轮未改 production 代码；无需新增注释。 |
| 正负例覆盖 | 正例：fresh install + ping/readiness + LLM `run`；负例：missing receipt `status/cancel`、default `diag_disabled`、Knowledge/tools no-positive-surface gap。 |
| 测试发现性 / 门禁入口 | 本轮不新增 CMake / CTest 注册；采用 package script 与 installed CLI 真实运行，不用既有单测/集测替代。 |
| TODO / 交付物回写 | 本文件记录 D/B gate、命令、结果和残余阻塞；来源 TODO 与 worklog 同步回写。 |
| 无关改动隔离 | 打包产生的 `obj-x86_64-linux-gnu/`、`debian/.debhelper/` 为副产物，不纳入提交。 |

## 8. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | 见 §5。 |
| B Gate | PASS | package build、fresh reinstall smoke、manual installed control-plane / LLM matrix 均通过。 |
| L4 installed-package local 主功能门 | PASS | `run` completed + `task_completed=true` + `llm.origin=deepseek-prod/deepseek-reasoner` + no `agent.dataset`。 |
| L5 qemu / release-ready | BLOCKED / out of scope | `FULLINT-BLK-001` 仍需 release runner / qemu image / secret / network，由 `FULLINT-TODO-019` 执行。 |
| Knowledge installed-package 正向入口 | BLOCKED / follow-up | `FULLINT-BLK-002` 仍需 `FULLINT-TODO-014`。 |
