# FULLINT-TODO-019 release runner 当前候选版 rerun 与 artifact-archive 执行准备

日期：2026-05-17
来源任务：FULLINT-TODO-019
范围：current release candidate 的 Gate-INT-10 -> package build -> metadata -> qemu autopkgtest -> lintian -> installed LLM smoke；release evidence bundle 归档 contract；当前主机 preflight / blocker 回写

## 1. Phase -1 任务确认

本轮只推进 `FULLINT-TODO-019`。

可执行性判定：Partially unblocked at local-host bootstrap；current release candidate 仍未完成 qemu rerun，但 host-side `autopkgtest-virt-qemu` 启动链已单独定位并给出可复用 workaround。

1. `FULLINT-BLK-001` 已解决，说明仓库内的 release-runner contract、workflow 输入面、DeepSeek key file 注入方式与 qemu harness 都已经固定，不再缺 repo-level blocker。
2. 本轮本机 preflight 已证实基础命令与 package 产物存在：`cmake`、`dpkg-buildpackage`、`autopkgtest`、`lintian`、`qemu-system-x86_64`、`autopkgtest-virt-qemu` 均可发现，且 `../dasall_0.1.0-1_amd64.changes` 已存在。
3. 当前仓库已新增 `scripts/packaging/setup_local_qemu_gate_env.sh` 与 `scripts/packaging/run_local_qemu_gate.sh`，可把 runner-local `qemu_image` 与 host-side `DASALL_DEEPSEEK_API_KEY_FILE` 固化到稳定本地路径，并自动处理 KVM / `--disable-kvm` 回退。当前主机已不再缺这两项资产；早前串联实跑曾停在 build-tree `dasall_gate_int_10` 的 `DaemonBinaryUnarySmokeTest`，后续单独调 host qemu 启动链又进一步确认：即使绕过仓库 preflight，当前 WSL2 host 仍有 loopback 端口探测与 KVM 假阳性两类 host-side trap。
4. 只停在 `autopkgtest` 启动头的原因已明确：Ubuntu 24.04 `autopkgtest-virt-qemu` 5.47 会先用无 timeout 的 `socket.create_connection(("127.0.0.1", 10022))` 探测 SSH 转发端口；当前 WSL2 route table 让未监听的 `127.0.0.1:10022` 进入 `SYN-SENT` 而非快速 `ECONNREFUSED`，因此 qemu 进程尚未创建，也不会有稳定 qemu/testbed 日志。
5. 通过临时 `ip rule add pref 0 to 127.0.0.0/8 lookup local` 使 loopback 目标优先走 local table 后，端口探测会快速拒绝；通过临时 qemu wrapper 过滤 `-enable-kvm` 后，最小 autopkgtest probe 已进入 Ubuntu 24.04 guest、拿到 testbed capabilities，并执行 `smoke PASS`。该结果证明 host 启动链可用，但不等于 DASALL current release candidate gate 已通过。
6. 本轮同轮最小闭环不是伪造 rerun，而是把 rerun / artifact archive contract 补完整：release workflow 现在会归档结构化 `autopkgtest` 输出目录、`.changes/.buildinfo/.deb/.ddeb` package artifacts、`lintian.log`、secret injection record 与命令元数据。
7. 本交付物只记录当前执行准备、preflight 结果、阻塞项与解阻条件，不把 `FULLINT-TODO-019` 写成通过。

## 2. 研究输入

| 输入 | 本轮采用方式 |
|---|---|
| [docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md) | 锁定 `FULLINT-TODO-019` 的 scope、artifact 目标、验收命令与完成判定。 |
| [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh) | 确认 qemu 串联入口的真实 owner；本轮为其补 `DASALL_AUTOPKGTEST_OUTPUT_DIR`，把结构化 autopkgtest 产物暴露给 archive bundle。 |
| [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | 确认 self-hosted runner 输入面；本轮补 package artifacts copy、secret injection record 与 `autopkgtest` 输出目录归档。 |
| [scripts/packaging/README.md](../../../../scripts/packaging/README.md) | 回写 release runner contract，不再把 archive 误写成“只有 gate/lintian 单日志”。 |
| [scripts/packaging/README.md §4.6](../../../../scripts/packaging/README.md) | 固化 WSL2 loopback `find_free_port` trap、KVM 假阳性、local route rule 与 no-KVM qemu wrapper。 |
| 本机 preflight | 记录 commands / `.changes` 已就绪，并通过 local bootstrap helper 固化 `qemu_image`、`DASALL_DEEPSEEK_API_KEY_FILE` 与 KVM 回退策略；本轮补充最小 qemu probe 证明 host 启动链可在 no-KVM wrapper 下进入 guest。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 为 qemu rerun 输出结构化 `autopkgtest` 证据目录 | `validate_gate_int_10_installed_package_qemu.sh`、019 artifact 需求 | 脚本支持 `DASALL_AUTOPKGTEST_OUTPUT_DIR`，workflow 可直接上传目录 | 若只保留 stdout gate log，则 autopkgtest case 级材料仍难以归档与追溯 |
| D2 | 把 package artifacts 与 secret injection 记录纳入 release bundle | workflow 现有 artifact_dir、019 目标字段 | artifact bundle 至少包含 `.changes/.buildinfo/.deb/.ddeb`、`lintian.log`、secret injection record、command log | 若 gate 失败后不复制 package artifacts，则失败轮次缺核心复盘材料 |
| D3 | 如实记录当前主机 preflight 与阻塞 | 本机命令可用性、local bootstrap helper 与 qemu gate 实跑检查 | deliverable / TODO 行明确写出 commands / `.changes` 已就绪，本机资产可固化，但 `Gate-INT-10` 仍失败 | 若把 local bootstrap helper 或 qemu image accepted 误写成 rerun 已完成，会抬高 release confidence |
| D4 | 固化 host-side qemu 启动链 trap 与 workaround | 本轮最小 autopkgtest probe、WSL2 route / KVM 观测、packaging README | 文档明确 `autopkgtest` 启动头、`find_free_port` 卡点、route rule、no-KVM wrapper 与最小 probe 命令 | 若不固化，会反复误判为 qemu/testbed 慢或日志缺失，并浪费时间重查同一 host 问题 |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| D1：结构化 autopkgtest 输出 | [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh)、[.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | workflow 通过 `DASALL_AUTOPKGTEST_OUTPUT_DIR` 把 autopkgtest case 级输出保留到 artifact bundle |
| D2：package artifacts + secret injection record | [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | `.changes/.buildinfo/.deb/.ddeb` 被复制到 artifact bundle，secret injection record 单独落盘 |
| D3：阻塞态回写 | [docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md)、本交付物、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) | `FULLINT-TODO-019` 当前环境写成 Blocked，解阻条件明确且不冒充通过 |
| D4：host qemu 启动链经验固化 | [scripts/packaging/README.md](../../../../scripts/packaging/README.md)、本交付物、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) | 后续看到只停在 `autopkgtest` 启动头时，能按 route / KVM / wrapper 顺序复验，而不是重新猜测 testbed |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-019` 的 rerun / archive contract 与当前主机 blocker 回写，不扩散到新的产品行为修改。 |
| 前置依赖 | PASS | `FULLINT-TODO-013` 已完成，`FULLINT-BLK-001` 已解阻，仓库级 workflow / qemu contract 已存在。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令与解阻条件都在 019 行和本交付物内明确。 |
| 不伪造 rerun | PASS | 本轮补 archive contract 与 local bootstrap helper；即使本机已具备稳定 image/key 与 KVM 回退入口，只要 `Gate-INT-10` 未过，仍不宣称 qemu rerun 已通过。 |

## 6. B 阶段执行结果

### 6.1 代码落点

| 文件 | 变更 | 结果 |
|---|---|---|
| [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh) | 新增 `DASALL_AUTOPKGTEST_OUTPUT_DIR` 支持，并在传给 `autopkgtest` 前创建输出目录 | qemu rerun 现在可产出结构化 autopkgtest artifact |
| [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | 归档 `autopkgtest` 输出目录、`.changes/.buildinfo/.deb/.ddeb` package artifacts、secret injection record、命令元数据 | release bundle 不再只剩单条 gate/lintian log |
| [scripts/packaging/README.md](../../../../scripts/packaging/README.md) | 更新 release runner contract 文案 | packaging README 与当前 archive contract 一致 |
| [docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md) | 把 `FULLINT-TODO-019` 标记为 `Blocked` 并写明环境阻塞与解阻条件 | 当前环境执行态有明确 owner 与不外推边界 |

### 6.2 当前主机 preflight

| 检查项 | 结果 | 判定 |
|---|---|---|
| `cmake` / `dpkg-buildpackage` / `autopkgtest` / `lintian` / `qemu-system-x86_64` / `autopkgtest-virt-qemu` | 均可发现 | 命令层已就绪 |
| `../dasall_0.1.0-1_amd64.changes` | 已存在 | package build 产物层已就绪 |
| runner-local `qemu_image` | 已通过 `setup_local_qemu_gate_env.sh` 固化到 `$HOME/.cache/dasall/qemu/autopkgtest-noble-amd64.img` | Ready |
| `DASALL_DEEPSEEK_API_KEY_FILE` | 已通过 `setup_local_qemu_gate_env.sh` 固化到 `$HOME/.local/share/dasall/secrets/deepseek-prod.secret` | Ready |
| `sh scripts/packaging/run_local_qemu_gate.sh --print-config` | 通过；能解析稳定 image/key 路径，且在当前会话自动经 `sg kvm` 进入 `kvm=enabled` | Ready |
| `sh scripts/packaging/run_local_qemu_gate.sh` | qemu image / key / KVM 均 accepted，但 `dasall_gate_int_10` 中 `DaemonBinaryUnarySmokeTest` 失败，未进入 `dpkg-buildpackage` / `autopkgtest` / `lintian` | Blocked by repo preflight |
| WSL2 loopback route probe | `ip route get 127.0.0.1` 命中 `via ... dev loopback0 table 127`；空端口连接停在 `SYN-SENT`；临时 `sudo ip rule add pref 0 to 127.0.0.0/8 lookup local` 后可快速 `ECONNREFUSED` | Host trap identified |
| KVM real probe | `/dev/kvm` 可写，但 QEMU `-enable-kvm` 报 `Could not access KVM kernel module: No such device`；`AUTOPKGTEST_QEMU_DISABLE_KVM=1` 不能阻止当前 `autopkgtest-virt-qemu` 自动追加 `-enable-kvm` | Host trap identified |
| 最小 no-KVM qemu probe | 端口规则 / 端口占位绕过 `find_free_port` 后，通过 qemu-command wrapper 过滤 `-enable-kvm`，最小 autopkgtest 在 guest 中输出 `DASALL_AUTOPKGTEST_QEMU_PROBE_OK` 且 `smoke PASS` | Host startup chain proven |

结论：当前主机已不再缺 runner-local image 与 host-side key file；只停在 `autopkgtest` 启动头的根因已定位为 WSL2 loopback 端口探测，KVM 假阳性也已定位。通过 route rule + no-KVM qemu wrapper，host 启动链可进入 guest 并执行最小 smoke；但 current release candidate 仍需在修复 build-tree preflight 并应用上述 host workaround 后重新跑正式 package qemu gate。

### 6.3 当前 artifact bundle contract

| Artifact | 当前来源 | 备注 |
|---|---|---|
| `gate-int-10-qemu.log` | workflow 中 qemu gate stdout/stderr | 保留整体串联日志 |
| `autopkgtest/` | `DASALL_AUTOPKGTEST_OUTPUT_DIR` | 保留结构化 autopkgtest case 级产物 |
| `packages/*.changes`、`*.buildinfo`、`*.deb`、`*.ddeb` | workflow 在 package build 后复制 | 失败轮次也能回看 package build 产物 |
| `lintian.log` | workflow lintian step | 独立于 gate log |
| `secret-injection-record.txt` | workflow 明示 `--copy + --env` contract | 只记注入方式与 testbed 路径，不记 secret 值 |
| `release-gate-command.txt` | workflow 命令元数据 | 记录 qemu image、timeout、disable_kvm、artifact dir 等执行参数 |

### 6.4 解阻条件

1. 先修复当前仓库态 `dasall_gate_int_10` / `DaemonBinaryUnarySmokeTest` 回退，使 qemu 串联不再停在 build-tree preflight。
2. 继续保留 `setup_local_qemu_gate_env.sh` 作为本机 / self-hosted runner 的 once bootstrap 入口，使 `qemu_image` 与 `DASALL_DEEPSEEK_API_KEY_FILE` 能固定到稳定路径。
3. 在 WSL2 host 上固化 `sudo ip rule add pref 0 to 127.0.0.0/8 lookup local` 或等价 systemd oneshot，确保 `autopkgtest-virt-qemu` 的 `find_free_port()` 不再卡住。
4. 对 KVM 不可用但 `/dev/kvm` 假阳性的 host，固化 qemu-command wrapper 过滤 `-enable-kvm`，或在未来脚本中增加真实 KVM probe 后自动选择 no-KVM wrapper。
5. 保证 provider probe 与 guest 内实际外呼具备外网 reachability。
6. 在 `Gate-INT-10` 恢复后，执行 `sh scripts/packaging/run_local_qemu_gate.sh` 或等价 workflow / `validate_gate_int_10_installed_package_qemu.sh`，再回写 artifact bundle 路径。

## 7. 当前结论

1. `FULLINT-TODO-019` 本轮已补齐仓库内的 rerun / artifact archive contract，但当前环境仍未完成真实 qemu rerun。
2. 当前主机阻塞点已不再是 runner-local `qemu_image` 与 `DASALL_DEEPSEEK_API_KEY_FILE` 缺失；host-side 启动链还需要 loopback route rule 与 no-KVM wrapper，仓库态则仍需修复 `dasall_gate_int_10` / `DaemonBinaryUnarySmokeTest` 回退。
3. 最小 no-KVM qemu probe 已证明 `autopkgtest -> autopkgtest-virt-qemu -> qemu -> testbed` 可达并能执行 guest smoke，但它只证明 host 启动链，不证明 DASALL package gate。
4. 只有在真实 runner 上拿到当轮 artifact bundle 后，当前版本才有资格升级为 current release candidate 的 production installed-package release-ready 证据。
