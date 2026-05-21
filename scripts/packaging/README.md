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
| release runner workflow | 在 self-hosted runner 固定 local installed package evidence、Knowledge proof / soak artifact，再执行 qemu gate 与日志归档 | GitHub Actions `DASALL-Release-Package-Gate` workflow_dispatch | FULLINT-BLK-001 blocker-fix |

补充约束：`dasall_gate_int_10` 只承载 build-tree app-binary smoke（daemon/gateway 真实 binary + socket path discoverability），`dasall_packaging_preflight_tests` 继续承载 package 相关 contract / daemon preflight 切片；两者都通过 `release-preflight-gate` label 接入 discoverability verifier，但彼此不互相替代。

单目标结果词汇固定如下，避免 CI / release note 误写：

| 单独通过的目标 | 允许结论 | 禁止外推 |
|---|---|---|
| `dasall_gate_int_10` | app-binary smoke slice passed；daemon/gateway binary smoke 与 startup diagnostics 当前通过 | packaging preflight passed、installed-package ready、qemu passed |
| `dasall_packaging_preflight_tests` | package preflight slice passed；package metadata / contract / daemon preflight 切片当前通过 | gateway binary ready、daemon/CLI binary ready、Gate-INT-10 passed、build-tree release-preflight complete |
| 两者同轮均通过 | build-tree `release-preflight` complete | installed-package ready、qemu passed、production release-ready |

`validate_gate_int_10_installed_package_qemu.sh` 是二者之间的串联入口，不改变 Gate owner：脚本在同一轮内依次执行 `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、`dpkg-buildpackage -us -uc -b`、`validate_autopkgtest_metadata.py` 与 qemu `autopkgtest`。它证明 installed-package gate 只在 build-tree `release-preflight` 通过后运行，但 qemu / `autopkgtest` 结果仍归 PKG-GATE-07，不回写为 `Gate-INT-10` 本身。

## 3. installed-package 功能矩阵

`pkg_smoke_install.sh --explicit-start-check` 与 `debian/tests/pkg-smoke-local-control-plane` 不再只验安装生命周期；二者必须把安装后的本地控制面和主功能语义一起纳入 gate。子系统能力收敛可使用本机实际 installed-package smoke 作为 authoritative local evidence；qemu / machine isolation 继续用于 release-runner 环境和隔离可重复性，不作为所有子系统能力闭合的强制前置。

| 功能面 | local lifecycle smoke | `pkg-smoke-local-control-plane` | 验收语义 |
|---|---|---|---|
| `run` / LLM | `dasall run ... --json` 必须同时包含 `"disposition":"completed"`、`"task_completed":true`、`llm.origin=deepseek-prod/`，且不得包含 `agent.dataset` | 同左 | 证明 installed daemon 的主功能链路通过 production `ILLMManager` 调用 DeepSeek；仅有 completed transport 或 builtin dataset 投影不算通过。 |
| tools | `/usr/lib/dasall/dasall-tools-installed-proof --json` 必须返回 `"ok": true`、`"route_kind": "builtin"`、`"terminal_route_kind": "builtin"`、`"agent_dataset_visible": true`、`"agent_terminal_visible": true`、`"terminal_confirmation_denied": true`、`"terminal_invocation_succeeded": true`，且 payload / terminal_payload 分别保留 `capability_id=agent.dataset` / `projection=default` 与 `operation=agent.terminal` | `pkg_smoke_install.sh --explicit-start-check` 必须落盘 `tools-installed-proof.json`，release-runner package-smoke artifact 目录保留同名证据 | 证明 installed package 当前可见 tool surface 已与 runtime production builtin catalog 对齐，且 `agent.dataset` query 与 `agent.terminal` high-risk action 都能经由 governed `IToolManager -> builtin -> services` 正向链路产出 payload、observation digest 与 confirmation gate 证据；同时避免把 builtin dataset 投影误当 LLM 主链路。 |
| services | `services_local_installed_proof.sh` 必须消费 `tools-installed-proof.json` 或等价 source proof，并落盘 `services-installed-proof.json`，确认 `tool_to_services_adapter_backend_path_present=true`、payload 保留 `capability_id=agent.dataset` / `projection=default`、terminal payload 保留 `operation=agent.terminal`，且 bridge / observability evidence marker 仍存在 | release-runner local step 继续从 package-smoke artifact 目录归档 `services-installed-proof.json`，并执行 `services_subscription_adapter_soak.sh` 生成 `services-soak-summary.json` | 证明 installed local evidence 已把 `IToolManager -> builtin -> services -> adapter/backend` 正向链路单独收口为 services owner artifact；subscription overflow / remote timeout 的重复 direct-binary soak 继续作为 release-runner local hardening，不依赖 qemu / kvm。 |
| `status` | `status receipt:missing token local://uid/0 --json` 必须 exit 5 且 `status_missing` | 同左 | 缺失 receipt fail-closed，不误报成功。 |
| `cancel` | `cancel receipt:missing token local://uid/0 --json` 必须 exit 5 且 `cancel_missing` | 同左 | 缺失 receipt fail-closed，不误报成功。 |
| `diag` 默认门控 | 默认安装后 `diag health --json` 必须 exit 4 且 `diag_disabled` | 不适用，autopkgtest 会通过 config apply 启用 diag | 证明 diagnostics 不会绕过 daemon config gate。 |
| `diag` 正向 | 不启用 diag，不跑正向 | config apply 写入 `diag_enabled: true` 后，`diag health --json` 必须 completed 且返回 diagnostics summary | 证明 CLI alias `health`、daemon allowlist、infra diagnostics service 和 UDS JSON 投影都可用。 |
| LLM assets/config | 校验 planner/responder prompt、provider catalog、DeepSeek provider manifest 已安装；重装 smoke 会保留既有 DeepSeek secret 并恢复 `dasall` 组只读权限 | 使用既有 secret，或通过 `DASALL_DEEPSEEK_API_KEY_FILE` 导入；验证 secret 落盘且不明文保存 | 证明 installed package 具备真实 LLM 外呼所需资产、secret 权限和 runtime composition。 |
| knowledge | `dasall knowledge refresh/retrieve/health --json` 必须 completed；`knowledge_local_installed_proof.sh` 需生成 `knowledge-proof.json` 与 `installed-normative-assets.json`；`knowledge_failure_injection_installed_proof.sh` 需生成 `knowledge-failure-injection-proof.json` | release-runner local step 继续执行 `knowledge_refresh_retrieve_soak.sh` 并生成 `knowledge-soak-summary.json` | 证明 installed package 已具独立 Knowledge 正向入口与 active snapshot 损坏后的本机恢复能力；local authoritative evidence 由 refresh/provider-retrieve/health proof、installed normative asset files、failure-injection recovery proof 与 soak artifact 组成，qemu / machine isolation 继续留给 release hardening，不再作为 Knowledge 功能闭环的强制前置。 |

矩阵中的 `knowledge` 行不再是“无独立正向入口”的显式缺口。当前 authoritative local owner 固定为 `knowledge_local_installed_proof.sh` 落盘的 `knowledge-proof.json` / `installed-normative-assets.json`、`knowledge_failure_injection_installed_proof.sh` 落盘的 `knowledge-failure-injection-proof.json`，以及 `knowledge_refresh_retrieve_soak.sh` 落盘的 `knowledge-soak-summary.json`；`pkg_smoke_install.sh` 仍可保留 package-smoke 侧原始 Knowledge JSON，但不再作为 Knowledge proof 的唯一 owner。Capability Services 的 local owner 现固定为 package-smoke artifact 目录中的 `services-installed-proof.json` 与 release-runner local soak 目录中的 `services-soak-summary.json`；更高层 qemu / machine isolation 继续属于 packaging / release hardening，不把本机 installed 证据外推为 package-ready。

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
2. workflow 在 qemu gate 前先固定五类 local installed artifact：`pkg_smoke_install.sh --explicit-start-check` 的 package-smoke 目录（现至少包含 `run-first.json`、`run-second.json`、`memory-proof.json`、`memory-maintenance-proof.json`、`tools-installed-proof.json`、`services-installed-proof.json`）、`services_subscription_adapter_soak.sh` 的 soak 目录（含 `services-soak-summary.json`）、`knowledge_local_installed_proof.sh` 的 proof 目录（含 `knowledge-proof.json` / `installed-normative-assets.json`）、`knowledge_failure_injection_installed_proof.sh` 的 failure 目录（含 `knowledge-failure-injection-proof.json`）与 `knowledge_refresh_retrieve_soak.sh` 的 soak 目录（含 `knowledge-soak-summary.json`）；五者只证明 local authoritative evidence，不直接外推为 qemu PASS。
3. `validate_gate_int_10_installed_package_qemu.sh` 现已正式支持以下 release-runner 环境变量：`DASALL_DEEPSEEK_API_KEY_FILE`、`DASALL_AUTOPKGTEST_TESTBED_SECRET_PATH`、`DASALL_AUTOPKGTEST_SETUP_COMMANDS`、`DASALL_AUTOPKGTEST_SETUP_COMMANDS_BOOT`、`DASALL_AUTOPKGTEST_OUTPUT_DIR`。
4. workflow 会生成 testbed preflight 脚本，并通过 `DASALL_AUTOPKGTEST_SETUP_COMMANDS` 传给 `validate_gate_int_10_installed_package_qemu.sh`；默认 preflight 只做 provider reachability 探测，不在日志中记录 secret 值。
5. workflow 当前会归档 `package-smoke-local.log`、`services-soak.log`、`knowledge-proof.log`、`knowledge-failure-injection.log`、`knowledge-soak.log`、`package-smoke/services-installed-proof.json`、`services-soak/services-soak-summary.json`、`knowledge-proof/knowledge-proof.json`、`knowledge-proof/installed-normative-assets.json`、`knowledge-failure/knowledge-failure-injection-proof.json`、`knowledge-soak/knowledge-soak-summary.json`、`gate-int-10-qemu.log`、结构化 `autopkgtest` 输出目录、`.changes/.buildinfo/.deb/.ddeb` package artifacts、`lintian.log`、secret injection record 与命令元数据；`FULLINT-TODO-019` 后续只需要在真实 runner 上复跑并把归档路径回写 worklog / deliverable，不需要再发明第二套 release harness。

### 4.5 local host bootstrap

1. 本仓库现提供一套面向开发机的一次性 qemu/KVM bootstrap 入口：`scripts/packaging/setup_local_qemu_gate_env.sh` 负责把已有 qemu image 与 host-side DeepSeek key/secret 固定到稳定本地路径，并生成 qemu gate 所需的 env/setup 脚本；`scripts/packaging/run_local_qemu_gate.sh` 负责消费这些稳定路径并执行正式串联 gate。
2. 建议先安装本机依赖：`sudo apt-get install -y debhelper cmake ninja-build g++ pkgconf dpkg-dev lintian autopkgtest qemu-system-x86 python3 curl fakemachine vmdb2 zerofree`。
3. 若当前用户尚未具备 `/dev/kvm` 写权限，可执行 `sudo usermod -aG kvm "$USER"` 后重新登录；`run_local_qemu_gate.sh` 也会在用户已加入 `kvm` 组、但当前会话尚未刷新时自动尝试 `sg kvm`，否则回退到 `--disable-kvm` 慢速路径。
4. 一次性 setup 示例：`sh scripts/packaging/setup_local_qemu_gate_env.sh --image /path/to/autopkgtest.img`。若不显式传 `--deepseek-key-file`，脚本默认复用 `/var/lib/dasall/secrets/llm/providers/deepseek-prod.secret`；若稳定目标文件已存在，则会直接复用，不会重复覆盖。
5. 生成的 guest setup 脚本会在 `autopkgtest` 安装依赖前修复 resolver，默认使用 QEMU slirp DNS `10.0.2.3` 并带 `1.1.1.1` 兜底；脚本会把 `/etc/resolv.conf` 固化为 regular file，并同步写入 `/run/systemd/resolve/resolv.conf` 与 `/run/systemd/resolve/stub-resolv.conf`，随后对 `archive.ubuntu.com` 做解析探针。如需覆盖 resolver 或探针 host，可在执行 once setup 时设置 `DASALL_LOCAL_QEMU_DNS_SERVERS="10.0.2.3 1.1.1.1"` / `DASALL_LOCAL_QEMU_DNS_PROBE_HOST=archive.ubuntu.com`。
6. setup 完成后的快速检查：`sh scripts/packaging/run_local_qemu_gate.sh --print-config`。正式执行命令：`sh scripts/packaging/run_local_qemu_gate.sh`。
7. 本地 bootstrap 只负责把已有 image 和 secret 固化为稳定 host-side 输入，不会在仓库脚本里下载 qemu image，也不会把 secret 值写进仓库文件或日志。

### 4.6 host-side qemu startup traps

1. 如果日志只停在如下 `autopkgtest` 启动头，不代表 qemu/testbed 已经启动：

   ```text
   autopkgtest [..]: version ...
   autopkgtest [..]: host ... command line ...
   ```

   真正进入 `autopkgtest-virt-qemu` 后，至少应继续看到 `find_free_port`、`qemu-img info`、`full qemu command-line`、guest boot 或 testbed capability 日志。没有这些行时，优先排查 host-side virt server 启动链。

2. WSL2 / mirrored-network 场景下，`127.0.0.1` 的空端口可能不会立即返回 `ECONNREFUSED`，而是停在 `SYN-SENT`。Ubuntu 24.04 `autopkgtest-virt-qemu` 5.47 的 `find_free_port()` 对 `127.0.0.1:10022` 探测没有显式 timeout，会因此卡在 `find_free_port: trying 10022`，qemu 进程尚未创建。快速诊断：

   ```bash
   ip route get 127.0.0.1
   timeout 5s python3 - <<'PY'
   import socket, time
   start = time.time()
   try:
       socket.create_connection(("127.0.0.1", 10022))
       print("connected", time.time() - start)
   except Exception as exc:
       print(type(exc).__name__, getattr(exc, "errno", None), exc, time.time() - start)
   PY
   ```

   若 `ip route get 127.0.0.1` 显示 `via ... dev loopback0 table 127` 且 Python 命令超时，先固化本机规则，使 loopback 目标优先走 local table：

   ```bash
   sudo ip rule add pref 0 to 127.0.0.0/8 lookup local
   ```

   若需要持久化，可用 systemd oneshot 管理该规则：

   ```ini
   [Unit]
   Description=DASALL autopkgtest loopback routing rule
   After=network-online.target

   [Service]
   Type=oneshot
   RemainAfterExit=yes
   ExecStart=/bin/sh -c '/usr/sbin/ip rule show | /usr/bin/grep -q "to 127.0.0.0/8 lookup local" || /usr/sbin/ip rule add pref 0 to 127.0.0.0/8 lookup local'
   ExecStop=/bin/sh -c '/usr/sbin/ip rule del pref 0 to 127.0.0.0/8 lookup local || true'

   [Install]
   WantedBy=multi-user.target
   ```

3. `/dev/kvm` 可写不等于 QEMU KVM 可用。当前 WSL2 host 上曾出现 `/dev/kvm` `rw-ok`、但 `qemu-system-x86_64 -enable-kvm` 报 `Could not access KVM kernel module: No such device` 的假阳性。不要只凭 `run_local_qemu_gate.sh --print-config` 的 `kvm=enabled` 判断可用；应额外用最小 qemu 或最小 autopkgtest probe 验证真实 `-enable-kvm`。

4. Ubuntu 24.04 `autopkgtest-virt-qemu` 5.47 会在发现 `/dev/kvm` 时自动追加 `-enable-kvm`；本仓库脚本里的 `--disable-kvm` / `AUTOPKGTEST_QEMU_DISABLE_KVM=1` 不足以保证该版本去掉 `-enable-kvm`。在 host 不支持 KVM 时，可临时或本地固化一个 qemu wrapper，并通过 `--qemu-command` 传给 virt server：

   ```bash
   sudo install -d /usr/local/lib/dasall
   sudo tee /usr/local/lib/dasall/qemu-system-x86_64-no-kvm >/dev/null <<'EOF'
   #!/usr/bin/env bash
   set -euo pipefail
   filtered=()
   for arg in "$@"; do
     [[ "$arg" == "-enable-kvm" ]] && continue
     filtered+=("$arg")
   done
   exec /usr/bin/qemu-system-x86_64 "${filtered[@]}"
   EOF
   sudo chmod +x /usr/local/lib/dasall/qemu-system-x86_64-no-kvm
   ```

   最小 host 启动链 probe 示例：

   ```bash
   autopkgtest -d -B --output-dir /tmp/dasall-adt-qemu-probe/out /tmp/dasall-adt-qemu-probe/src \
     -- /usr/bin/autopkgtest-virt-qemu --debug --show-boot --timeout-reboot=300 \
     --qemu-command=/usr/local/lib/dasall/qemu-system-x86_64-no-kvm \
     "$HOME/.cache/dasall/qemu/autopkgtest-noble-amd64.img"
   ```

   该 probe 只证明 host `autopkgtest -> autopkgtest-virt-qemu -> qemu -> testbed` 启动链可用；不能替代 `validate_gate_int_10_installed_package_qemu.sh` 的 installed package release gate。

5. 若 qemu 已进入 guest、但 `autopkgtest` 在 preparing testbed 阶段出现 `Temporary failure resolving 'archive.ubuntu.com'` / `apt failed to download packages`，先区分 host DNS 与 guest resolver。host 侧可用但 guest 侧失败时，重新执行 `setup_local_qemu_gate_env.sh` 生成带 resolver 修复的 setup 脚本；正式 gate 应继续通过 `run_local_qemu_gate.sh` 或 `validate_gate_int_10_installed_package_qemu.sh` 产生新的 `autopkgtest` artifact，而不要把 package build 日志当作 installed-package PASS。

## 5. 已落盘文件

当前目录已经落盘以下 packaging validator / smoke harness：

1. `scripts/packaging/validate_ubuntu_dpkg_v1.sh`
2. `scripts/packaging/pkg_smoke_install.sh`
3. `scripts/packaging/pkg_smoke_upgrade.sh`
4. `scripts/packaging/pkg_smoke_remove_purge.sh`
5. `scripts/packaging/validate_autopkgtest_metadata.py`
6. `scripts/packaging/validate_gate_int_10_installed_package_qemu.sh`
7. `scripts/packaging/setup_local_qemu_gate_env.sh`
8. `scripts/packaging/run_local_qemu_gate.sh`
9. `scripts/packaging/knowledge_local_installed_proof.sh`
10. `scripts/packaging/knowledge_refresh_retrieve_soak.sh`
11. `scripts/packaging/services_local_installed_proof.sh`
12. `scripts/packaging/services_subscription_adapter_soak.sh`

仍按需待定的只有：

13. `scripts/packaging/autopkgtest-*.cfg`（仅当 qemu / CI 配置需要固化时新增）

## 6. 不做什么

1. 不在这个目录里复写仓库通用 CTest 入口。
2. 不把 maintainer scripts、`debian/tests/*` 和 rootful lifecycle smoke 合并成一个黑盒脚本。
3. 不在 003 阶段提供伪可执行脚本占位；当前 README 只负责固定职责边界与命令口径。
