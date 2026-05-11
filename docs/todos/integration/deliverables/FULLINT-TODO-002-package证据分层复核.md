# FULLINT-TODO-002 package 证据分层复核

日期：2026-05-11  
来源任务：FULLINT-TODO-002  
范围：installed-package local、build-tree release-preflight、qemu / autopkgtest、lintian、LLM origin、knowledge installed-package gap

## 1. 结论

本轮复核结论：当前机器上的 DASALL `0.1.0-1` installed-package local 控制面与 LLM 主功能可实际运行，但 release runner / qemu authoritative gate 仍未在本轮执行，Knowledge 仍没有安装态独立 retrieve / refresh / health 正向入口。

因此本任务只允许写出以下分层结论：

1. L3 build-tree `release-preflight`：当前仓库有 `dasall_gate_int_10` 与 `dasall_packaging_preflight_tests` 的正式入口和 label discoverability 设计，但本任务不把现有单测 / 集测当作全量集成结论。
2. L4 installed-package local：当前安装态 `dasall run` 真实返回 `llm.origin=deepseek-prod/deepseek-reasoner`、`task_completed=true`，且没有 `agent.dataset` fallback。
3. L5 release runner / qemu：当前本机具备 `autopkgtest`、`lintian`、`qemu-system-x86_64` 命令和 `.changes` artifact，但没有本轮 qemu image / virt-server 配置执行记录，所以不能宣称 qemu gate pass。
4. Knowledge installed-package：CLI help 未暴露 `knowledge` / `retrieve` / `refresh` 正向入口，保持 `FULLINT-BLK-002`。

## 2. 输入与参考

### 2.1 本地输入

1. `scripts/packaging/pkg_smoke_install.sh`
2. `scripts/packaging/README.md`
3. `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`
4. `debian/tests/control`
5. `debian/tests/pkg-smoke-local-control-plane`
6. `docs/todos/packaging/DASALL_Ubuntu_DPKG打包专项TODO.md`
7. `docs/ssot/BusinessChainIntegrationMatrix.md`
8. 本轮实际 installed-package 命令输出文件：`/tmp/fullint002-*.json`、`/tmp/fullint002-lintian.txt`

### 2.2 外部参考

1. Debian `autopkgtest(1)`：`autopkgtest` 在指定 testbed 中测试已安装二进制包，`.changes` 可携带源码测试和二进制包，qemu virt server 才能提供 machine-level isolation。
2. Anthropic, Building effective agents：生产 agent 系统需要 ground truth、可观测执行和受控复杂度；映射到 DASALL 时，LLM 主链必须用实际 provider/model origin 证明，而不是内部 dataset fallback。

## 3. PackageEvidenceLayer 表

| Evidence Layer | 本轮状态 | 当轮命令 / 证据 | Owner | 可采信结论 | 不可外推 |
|---|---|---|---|---|---|
| L3 build-tree release-preflight | Present / not authoritative for this task | `tests/CMakeLists.txt` 注册 `dasall_gate_int_10`、`dasall_packaging_preflight_tests`；`scripts/packaging/README.md` 拆分 app-binary smoke 与 package preflight | integration / packaging handoff | 入口和 owner 可追溯 | 不代表 installed-package、qemu、production-ready |
| L4 installed-package package set | Pass | `dpkg-query -W -f='${binary:Package} ${Version} ${Status}\n' 'dasall*'` 显示 `dasall`、`dasall-cli`、`dasall-common`、`dasall-daemon` 均为 `0.1.0-1 install ok installed` | packaging | 当前机器安装了 v0.1.0-1 四包 | 不代表 fresh install / qemu rerun |
| L4 daemon lifecycle | Pass with rootful access | `systemctl is-active dasall-daemon.service` -> `active`；`systemctl is-enabled dasall-daemon.service` -> `enabled` | packaging / daemon | 当前 installed daemon 在本机处于可服务状态 | 普通用户 socket 仍权限拒绝，不代表 non-root ready |
| L4 local control-plane rootful | Pass | `sudo -n dasall ping --json`、`sudo -n dasall readiness --json` 均 `disposition=completed`；readiness payload 包含 `state=READY`、`bridge_reachable=true` | access / daemon | rootful CLI 到 daemon 控制面可达 | 不证明 degraded/default-ready 对外语义已加严 |
| L4 local control-plane non-root | Fail / expected by current policy | 普通用户 `dasall ping/readiness --json` 返回 `Permission denied`、`exit_code=3` | packaging / operator policy | 当前 socket mode 仅支持 root/sudo 路径 | 不可宣称所有本地用户 ready |
| L4 installed LLM main function | Pass | `sudo -n dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000` 返回 `disposition=completed`、`task_completed=true`、`llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`，响应内容为 `2` | runtime / llm / packaging | installed `run` 真实经过 DeepSeek-compatible provider | 不代表外部 provider 长稳态；不代表 qemu testbed 有 secret/network |
| L4 dataset fallback guard | Pass | `/tmp/fullint002-run.json` 未命中 `agent.dataset`；runtime 代码中 `agent.dataset fallback is disabled` | runtime / tools | 本轮主功能没有用 builtin dataset 假绿 | 不代表 tools runtime caller 已全链 ready |
| L4 status / cancel missing receipt | Pass | `status` missing receipt exit 5 + `status_missing`；`cancel` missing receipt exit 5 + `cancel_missing` | access / daemon | 缺失 receipt fail-closed | 不代表真实 receipt lifecycle 已执行 |
| L4 diag default gate | Pass | `diag health` 与 `diag queue` 默认返回 exit 4 + `diag_disabled` | access / infra | diagnostics 默认门控有效 | 不代表 diag 正向 health 已在本轮启用验证 |
| L4 package assets | Present | `../dasall-cli_0.1.0-1_amd64.deb`、`../dasall-common_0.1.0-1_all.deb`、`../dasall-daemon_0.1.0-1_amd64.deb`、`../dasall_0.1.0-1_all.deb`、`../dasall_0.1.0-1_amd64.changes` 存在 | packaging | 当前本机有可扫描 package artifacts | 不代表 freshly rebuilt artifacts |
| L5 autopkgtest metadata | Pass | `python3 scripts/packaging/validate_autopkgtest_metadata.py` -> validated 2 tests: `pkg-smoke-local-control-plane`、`pkg-smoke-common-assets` | packaging | metadata 可解析 | 不代表 testbed run pass |
| L5 lintian | Pass with warnings | `lintian ../dasall_0.1.0-1_amd64.changes` -> RC 0；warnings: four `initial-upload-closes-no-bugs` and `dasall-daemon: no-manual-page [usr/sbin/dasall-daemon]` | packaging | static package scan 无 blocking error | warnings 需保留 release review 口径 |
| L5 qemu / autopkgtest run | Blocked | 本机存在 `/usr/bin/autopkgtest`、`/usr/bin/qemu-system-x86_64`，但本轮没有 qemu image / virt-server 配置 | release runner | 仓库脚本与本机工具存在 | 不能宣称 `autopkgtest` authoritative pass |
| Knowledge installed-package positive entry | Blocked | `sudo -n dasall knowledge --help` 只输出通用 CLI usage；help 中未包含 `knowledge` / `retrieve` / `refresh` | knowledge / access / packaging | 缺口真实存在 | 不能把 Gate-INT-04 或 runtime evidence projection 当作 package-ready |

## 4. 当轮命令证据摘要

### 4.1 installed-package local

```text
command -v dasall
dpkg-query -W -f='${binary:Package} ${Version} ${Status}\n' 'dasall*'
systemctl is-active dasall-daemon.service
systemctl is-enabled dasall-daemon.service
sudo -n dasall ping --json
sudo -n dasall readiness --json
sudo -n dasall run '{"prompt":"请用LLM回答：1+1等于几？只给出简短答案。"}' --json --timeout-ms 120000
sudo -n dasall status receipt:missing token local://uid/0 --json
sudo -n dasall cancel receipt:missing token local://uid/0 --json
sudo -n dasall diag health --json
```

结果摘要：

1. package set：四包均为 `0.1.0-1 install ok installed`。
2. daemon lifecycle：`active` / `enabled`。
3. `ping` / `readiness`：rootful completed / READY；普通用户 socket 权限拒绝。
4. `run`：completed，`task_completed=true`，`llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`，回答为 `2`。
5. `status` / `cancel` missing receipt：均 exit 5，分别返回 `status_missing` / `cancel_missing`。
6. `diag health` / `diag queue`：默认均 exit 4，返回 `diag_disabled`。

### 4.2 release / qemu / lintian

```text
command -v autopkgtest
command -v lintian
command -v qemu-system-x86_64
python3 scripts/packaging/validate_autopkgtest_metadata.py
sh -n scripts/packaging/validate_gate_int_10_installed_package_qemu.sh
sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh --help
lintian ../dasall_0.1.0-1_amd64.changes
```

结果摘要：

1. `autopkgtest`、`lintian`、`qemu-system-x86_64` 均存在。
2. `.changes` 与四包 `.deb` artifact 存在。
3. metadata validate 通过，发现 `pkg-smoke-local-control-plane` 与 `pkg-smoke-common-assets` 两条 tests。
4. 串联脚本语法与 help 通过。
5. `lintian` RC=0，仍保留已知 warning：`initial-upload-closes-no-bugs` 与 `dasall-daemon: no-manual-page [usr/sbin/dasall-daemon]`。
6. qemu authoritative run 未执行；缺少本轮明确的 image / virt-server 参数和 release-runner secret/network 记录。

## 5. Knowledge installed-package gap

本轮直接检查安装态 CLI surface：

1. `dasall --help` / `sudo -n dasall knowledge --help` 只列出 `config`、`ping`、`readiness`、`run`、`status`、`cancel`、`diag` 等命令。
2. help 输出未包含 `knowledge`、`retrieve`、`refresh`。
3. `diag` 默认门控仍返回 `diag_disabled`，不能借 diag 默认面证明 knowledge health 正向入口。

结论：`FULLINT-BLK-002` 继续成立。后续必须通过 `FULLINT-TODO-014` 新增安装态可执行 retrieve / refresh / health 正向入口，或明确 daemon live composition 接入真实 `IKnowledgeService` 后再补 package smoke。

## 6. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 后继任务 |
|---|---|---|
| build-tree release-preflight 只证明源码树 app-binary / preflight，不吞并 installed-package | `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、`validate_gate_int_10_installed_package_qemu.sh` | FULLINT-TODO-010、020 |
| local installed-package 主功能必须断言 provider/model origin | `sudo -n dasall run ... --json --timeout-ms 120000` | FULLINT-TODO-013 |
| qemu authoritative gate 必须在 release runner 上给出 image / virt-server / secret / network 记录 | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | FULLINT-TODO-019 |
| Knowledge installed-package gap 不允许被 runtime evidence projection 替代 | CLI / daemon knowledge positive entry 或 package smoke hook | FULLINT-TODO-014 |

## 7. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | 证据层、owner、可采信结论与不可外推范围均已冻结 |
| B Gate | PASS | 当轮 installed `run`、ping/readiness、status/cancel、diag、metadata、lintian 已执行并记录；qemu / knowledge 缺口被显式标为 Blocked |

## 8. 残余风险

1. LLM 主功能依赖 DeepSeek provider、网络与本机 secret；本文件只记录 secret URI 与运行状态，不记录 secret 值。
2. 本轮没有 fresh reinstall package smoke；`pkg_smoke_install.sh --explicit-start-check` 会重装并清理服务，留给 FULLINT-TODO-013 执行。
3. 本轮没有 qemu authoritative `autopkgtest`，不能将当前版本升级为 production installed-package release-ready。
