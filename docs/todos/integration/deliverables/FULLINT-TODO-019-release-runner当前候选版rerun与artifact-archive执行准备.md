# FULLINT-TODO-019 release runner 当前候选版 rerun 与 artifact-archive 执行准备

日期：2026-05-17
来源任务：FULLINT-TODO-019
范围：current release candidate 的 Gate-INT-10 -> package build -> metadata -> qemu autopkgtest -> lintian -> installed LLM smoke；release evidence bundle 归档 contract；当前主机 preflight / blocker 回写

## 1. Phase -1 任务确认

本轮只推进 `FULLINT-TODO-019`。

可执行性判定：Blocked by runner-local assets；同轮已完成 archive contract 补位。

1. `FULLINT-BLK-001` 已解决，说明仓库内的 release-runner contract、workflow 输入面、DeepSeek key file 注入方式与 qemu harness 都已经固定，不再缺 repo-level blocker。
2. 本轮本机 preflight 已证实基础命令与 package 产物存在：`cmake`、`dpkg-buildpackage`、`autopkgtest`、`lintian`、`qemu-system-x86_64`、`autopkgtest-virt-qemu` 均可发现，且 `../dasall_0.1.0-1_amd64.changes` 已存在。
3. 当前环境仍缺两项 runner-local 资产：可直接传给 workflow/script 的 `qemu_image`，以及 host-side `DASALL_DEEPSEEK_API_KEY_FILE`。在缺少这两项时，不能诚实地声称 current release candidate 已完成 qemu rerun。
4. 本轮同轮最小闭环不是伪造 rerun，而是把 rerun / artifact archive contract 补完整：release workflow 现在会归档结构化 `autopkgtest` 输出目录、`.changes/.buildinfo/.deb/.ddeb` package artifacts、`lintian.log`、secret injection record 与命令元数据。
5. 本交付物只记录当前执行准备、preflight 结果、阻塞项与解阻条件，不把 `FULLINT-TODO-019` 写成通过。

## 2. 研究输入

| 输入 | 本轮采用方式 |
|---|---|
| [docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md) | 锁定 `FULLINT-TODO-019` 的 scope、artifact 目标、验收命令与完成判定。 |
| [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh) | 确认 qemu 串联入口的真实 owner；本轮为其补 `DASALL_AUTOPKGTEST_OUTPUT_DIR`，把结构化 autopkgtest 产物暴露给 archive bundle。 |
| [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | 确认 self-hosted runner 输入面；本轮补 package artifacts copy、secret injection record 与 `autopkgtest` 输出目录归档。 |
| [scripts/packaging/README.md](../../../../scripts/packaging/README.md) | 回写 release runner contract，不再把 archive 误写成“只有 gate/lintian 单日志”。 |
| 本机 preflight | 记录 commands / `.changes` 已就绪，以及当前主机缺 `qemu_image` 与 `DASALL_DEEPSEEK_API_KEY_FILE` 的事实。 |

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 为 qemu rerun 输出结构化 `autopkgtest` 证据目录 | `validate_gate_int_10_installed_package_qemu.sh`、019 artifact 需求 | 脚本支持 `DASALL_AUTOPKGTEST_OUTPUT_DIR`，workflow 可直接上传目录 | 若只保留 stdout gate log，则 autopkgtest case 级材料仍难以归档与追溯 |
| D2 | 把 package artifacts 与 secret injection 记录纳入 release bundle | workflow 现有 artifact_dir、019 目标字段 | artifact bundle 至少包含 `.changes/.buildinfo/.deb/.ddeb`、`lintian.log`、secret injection record、command log | 若 gate 失败后不复制 package artifacts，则失败轮次缺核心复盘材料 |
| D3 | 如实记录当前主机 preflight 与阻塞 | 本机命令可用性与环境变量检查 | deliverable / TODO 行明确写出 commands / `.changes` 已就绪，`qemu_image` 与 key file 缺失 | 若把 repo-level contract fixed 误写成 rerun 已完成，会抬高 release confidence |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| D1：结构化 autopkgtest 输出 | [scripts/packaging/validate_gate_int_10_installed_package_qemu.sh](../../../../scripts/packaging/validate_gate_int_10_installed_package_qemu.sh)、[.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | workflow 通过 `DASALL_AUTOPKGTEST_OUTPUT_DIR` 把 autopkgtest case 级输出保留到 artifact bundle |
| D2：package artifacts + secret injection record | [.github/workflows/release-package-gate.yml](../../../../.github/workflows/release-package-gate.yml) | `.changes/.buildinfo/.deb/.ddeb` 被复制到 artifact bundle，secret injection record 单独落盘 |
| D3：阻塞态回写 | [docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md](../DASALL_%E5%85%A8%E9%87%8F%E4%B8%9A%E5%8A%A1%E9%93%BE%E9%9B%86%E6%88%90%E9%AA%8C%E8%AF%81%E4%B8%93%E9%A1%B9TODO-2026-05-11.md)、本交付物、[docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) | `FULLINT-TODO-019` 当前环境写成 Blocked，解阻条件明确且不冒充通过 |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-019` 的 rerun / archive contract 与当前主机 blocker 回写，不扩散到新的产品行为修改。 |
| 前置依赖 | PASS | `FULLINT-TODO-013` 已完成，`FULLINT-BLK-001` 已解阻，仓库级 workflow / qemu contract 已存在。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令与解阻条件都在 019 行和本交付物内明确。 |
| 不伪造 rerun | PASS | 本轮只补 archive contract 与 preflight 证据；在缺 `qemu_image` / key file 时不宣称 qemu rerun 已通过。 |

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
| runner-local `qemu_image` | 当前 workspace / 常见本机目录未发现可直接使用候选 | Blocked |
| `DASALL_DEEPSEEK_API_KEY_FILE` | 当前未设置 | Blocked |

结论：当前主机只差 runner-local image 与 host-side key file；在这两项缺失前，不能执行诚实的 current release candidate qemu rerun。

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

1. 在 self-hosted release runner 提供可直接传给 workflow 的 `qemu_image`。
2. 在 runner host 提供 `deepseek_key_file`，并使 workflow 可将其映射为 `DASALL_DEEPSEEK_API_KEY_FILE`。
3. 保证 provider probe 与 guest 内实际外呼具备外网 reachability。
4. 满足以上三项后，执行 `DASALL-Release-Package-Gate` workflow 或等价命令 `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>`，再回写 artifact bundle 路径。

## 7. 当前结论

1. `FULLINT-TODO-019` 本轮已补齐仓库内的 rerun / artifact archive contract，但当前环境仍未完成真实 qemu rerun。
2. 当前主机阻塞点不是 repo 代码缺口，而是 runner-local `qemu_image` 与 `DASALL_DEEPSEEK_API_KEY_FILE` 缺失。
3. 只有在真实 runner 上拿到当轮 artifact bundle 后，当前版本才有资格升级为 current release candidate 的 production installed-package release-ready 证据。