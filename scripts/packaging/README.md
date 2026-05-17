# DASALL Packaging Scripts README

## 1. 目的

这个目录承载 Ubuntu DPKG v1 的 packaging 验证入口。`PKG-TODO-003` 先冻结运行策略与 gate 分层；`PKG-TODO-016` 已落 `debian/tests/*` installed-package smoke 脚本，`PKG-TODO-017` 已补齐本地 rootful lifecycle harness，并在本机通过 `dpkg-buildpackage -us -uc -b` + `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` 完成 fresh install / explicit start / upgrade / remove-purge 验收。

当前结论只有一条：不要把 build-tree preflight、local installed-package smoke、`autopkgtest` 混成同一种验证；当前剩余正式 gate 只包括 `lintian`、qemu authoritative `autopkgtest` 和证据收口。

## 2. Gate 入口

| Gate | 目标 | 代表命令 | 归属任务 |
|---|---|---|---|
| build-tree preflight | 在源码树内分别验证 app-binary gate 与 package 相关 contract / integration preflight 切片 | `cmake --build <build-dir> --target dasall_gate_int_10 && cmake --build <build-dir> --target dasall_packaging_preflight_tests` | INTFIX-TODO-010 / PKG-TODO-015 |
| package build | 生成四包 `.deb` / `.changes` 产物 | `dpkg-buildpackage -us -uc -b` | PKG-TODO-009 |
| static package scan | 对包产物跑 policy/static analysis | `lintian ../*.changes` | PKG-TODO-015 |
| local installed-package smoke | fresh install、explicit enable/start、upgrade、remove/purge | `bash scripts/packaging/validate_ubuntu_dpkg_v1.sh` | PKG-TODO-017 |
| autopkgtest metadata validate | 校验 `debian/tests/control` 语法与元数据 | `python3 scripts/packaging/validate_autopkgtest_metadata.py` | PKG-TODO-016 |
| autopkgtest installed-package run | 在 testbed 中验证安装后的包行为 | `autopkgtest ../dasall_*.changes -- qemu <image-or-config>` | PKG-TODO-016 |
| Gate-INT-10 -> qemu autopkgtest 串联 | 先验证 build-tree `release-preflight`，再重新构包并执行 qemu installed-package run | `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | INTFIX backlog / PKG-GATE-07 |
| release runner workflow | 在 self-hosted runner 固化 qemu image、DeepSeek key file、provider preflight 与日志归档 | GitHub Actions `DASALL-Release-Package-Gate` workflow_dispatch | FULLINT-BLK-001 blocker-fix |

补充约束：`dasall_gate_int_10` 只承载 build-tree app-binary smoke（daemon/gateway 真实 binary + socket path discoverability），`dasall_packaging_preflight_tests` 继续承载 package 相关 contract / daemon preflight 切片；两者都通过 `release-preflight-gate` label 接入 discoverability verifier，但彼此不互相替代。

单目标结果词汇固定如下，避免 CI / release note 误写：

| 单独通过的目标 | 允许结论 | 禁止外推 |
|---|---|---|
| `dasall_gate_int_10` | app-binary smoke slice passed；daemon/gateway binary smoke 与 startup diagnostics 当前通过 | packaging preflight passed、installed-package ready、qemu passed |
| `dasall_packaging_preflight_tests` | package preflight slice passed；package metadata / contract / daemon preflight 切片当前通过 | gateway binary ready、daemon/CLI binary ready、Gate-INT-10 passed、build-tree release-preflight complete |
| 两者同轮均通过 | build-tree `release-preflight` complete | installed-package ready、qemu passed、production release-ready |

`validate_gate_int_10_installed_package_qemu.sh` 是二者之间的串联入口，不改变 Gate owner：脚本在同一轮内依次执行 `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、`dpkg-buildpackage -us -uc -b`、`validate_autopkgtest_metadata.py` 与 qemu `autopkgtest`。它证明 installed-package gate 只在 build-tree `release-preflight` 通过后运行，但 qemu / `autopkgtest` 结果仍归 PKG-GATE-07，不回写为 `Gate-INT-10` 本身。

## 3. installed-package 功能矩阵

`pkg_smoke_install.sh --explicit-start-check` 与 `debian/tests/pkg-smoke-local-control-plane` 不再只验安装生命周期；二者必须把安装后的本地控制面和主功能语义一起纳入 gate。

| 功能面 | local lifecycle smoke | `pkg-smoke-local-control-plane` | 验收语义 |
|---|---|---|---|
| `run` / LLM | `dasall run ... --json` 必须同时包含 `"disposition":"completed"`、`"task_completed":true`、`llm.origin=deepseek-prod/`，且不得包含 `agent.dataset` | 同左 | 证明 installed daemon 的主功能链路通过 production `ILLMManager` 调用 DeepSeek；仅有 completed transport 或 builtin dataset 投影不算通过。 |
| tools | `agent.dataset` 保留为 builtin 工具资产/管理面能力，不再作为 installed `run` 的主功能通过条件 | 同左 | 工具链路后续应以独立 tools/diag/registry 正向入口补验，避免把工具投影误当 LLM 主链路。 |
| `status` | `status receipt:missing token local://uid/0 --json` 必须 exit 5 且 `status_missing` | 同左 | 缺失 receipt fail-closed，不误报成功。 |
| `cancel` | `cancel receipt:missing token local://uid/0 --json` 必须 exit 5 且 `cancel_missing` | 同左 | 缺失 receipt fail-closed，不误报成功。 |
| `diag` 默认门控 | 默认安装后 `diag health --json` 必须 exit 4 且 `diag_disabled` | 不适用，autopkgtest 会通过 config apply 启用 diag | 证明 diagnostics 不会绕过 daemon config gate。 |
| `diag` 正向 | 不启用 diag，不跑正向 | config apply 写入 `diag_enabled: true` 后，`diag health --json` 必须 completed 且返回 diagnostics summary | 证明 CLI alias `health`、daemon allowlist、infra diagnostics service 和 UDS JSON 投影都可用。 |
| LLM assets/config | 校验 planner/responder prompt、provider catalog、DeepSeek provider manifest 已安装；重装 smoke 会保留既有 DeepSeek secret 并恢复 `dasall` 组只读权限 | 使用既有 secret，或通过 `DASALL_DEEPSEEK_API_KEY_FILE` 导入；验证 secret 落盘且不明文保存 | 证明 installed package 具备真实 LLM 外呼所需资产、secret 权限和 runtime composition。 |
| knowledge | 记录为 installed-package 未暴露独立 CLI 正向入口 | 同左 | v0.1.0 daemon live composition 仍以 memory/cognition/response/tools 为必需端口，knowledge 是 optional port；不得把缺少独立 knowledge 正向路径伪装成 package-ready 证据。 |

矩阵中的 `knowledge` 行是显式缺口，不是通过项。后续若要宣称 “knowledge installed-package ready”，必须新增安装后可执行的 retrieve/refresh/health 正向命令或把 runtime live composition 接入真实 `IKnowledgeService`，再补上对应 smoke 断言。

## 4. testbed 策略

### 4.1 authoritative testbed

1. `pkg-smoke-local-control-plane` 的正式 gate 固定使用 qemu 或其他 machine-level isolation testbed。
2. 原因是该用例依赖 `systemd`、服务启停、socket 文件权限和 CLI 对安装后 daemon 的访问。
3. 不把 `null` virtualization 或 `--ignore-restrictions=isolation-machine` 视为正式验收路径。

### 4.2 local quick loop

1. 在没有完整 testbed 的场景下，开发者仍可先跑：

   `python3 scripts/packaging/validate_autopkgtest_metadata.py`

2. 后续若 `pkg-smoke-common-assets` 被拆成纯资产检查，可允许它在 container/unshare 类 testbed 上单独快速回归。
3. 这种 quick loop 不能替代 `pkg-smoke-local-control-plane` 的 machine-isolation gate。
4. 部分 Ubuntu 24.04 `autopkgtest` 版本在 `--validate` 仍会触发 testbed setup；本仓库已提供仅包装 `parse_debian_source()` 的本地兼容 shim `scripts/packaging/validate_autopkgtest_metadata.py` 作为 metadata quick loop，但不能把它当成 installed-package run。

### 4.3 CI 最小要求

1. CI 至少串行执行 build-tree preflight、package build、`lintian`。
2. 若 CI 输出 package-ready 结论，则必须额外执行 qemu testbed 上的 `autopkgtest` installed-package run。
3. local rootful lifecycle smoke 与 qemu `autopkgtest` 共同组成 installed-package gate；缺一不可。
4. 若 CI 需要证明 `Gate-INT-10` 与 installed-package qemu gate 的顺序关系，应使用 `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`，并显式传入 qemu image 或 virt-server 配置。

### 4.4 release runner contract

1. `.github/workflows/release-package-gate.yml` 是当前仓库内固定的 release runner 入口：只接受 self-hosted runner 提供的 `qemu_image` 与 `deepseek_key_file`，不在仓库脚本里下载 image 或写死 secret。
2. workflow 会生成 testbed preflight 脚本，并通过 `DASALL_AUTOPKGTEST_SETUP_COMMANDS` 传给 `validate_gate_int_10_installed_package_qemu.sh`；默认 preflight 只做 provider reachability 探测，不在日志中记录 secret 值。
3. `validate_gate_int_10_installed_package_qemu.sh` 现已正式支持以下 release-runner 环境变量：`DASALL_DEEPSEEK_API_KEY_FILE`、`DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH`、`DASALL_AUTOPKGTEST_SETUP_COMMANDS`、`DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT`、`DASALL_AUTOPKGTEST_OUTPUT_DIR`。
4. workflow 当前会归档 `gate-int-10-qemu.log`、结构化 `autopkgtest` 输出目录、`.changes/.buildinfo/.deb/.ddeb` package artifacts、`lintian.log`、secret injection record 与命令元数据；`FULLINT-TODO-019` 后续只需要在真实 runner 上复跑并把归档路径回写 worklog / deliverable，不需要再发明第二套 release harness。

## 5. 已落盘文件

当前目录已经落盘以下 packaging validator / smoke harness：

1. `scripts/packaging/validate_ubuntu_dpkg_v1.sh`
2. `scripts/packaging/pkg_smoke_install.sh`
3. `scripts/packaging/pkg_smoke_upgrade.sh`
4. `scripts/packaging/pkg_smoke_remove_purge.sh`
5. `scripts/packaging/validate_autopkgtest_metadata.py`
6. `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`

仍按需待定的只有：

7. `scripts/packaging/autopkgtest-*.cfg`（仅当 qemu / CI 配置需要固化时新增）

## 6. 不做什么

1. 不在这个目录里复写仓库通用 CTest 入口。
2. 不把 maintainer scripts、`debian/tests/*` 和 rootful lifecycle smoke 合并成一个黑盒脚本。
3. 不在 003 阶段提供伪可执行脚本占位；当前 README 只负责固定职责边界与命令口径。