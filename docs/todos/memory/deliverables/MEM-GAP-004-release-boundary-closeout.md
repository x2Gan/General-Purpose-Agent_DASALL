# MEM-GAP-004 release boundary closeout

来源任务：MEM-GAP-004
完成日期：2026-05-20
关联修复：MEM-FIX-006

## 1. 任务边界

1. 本轮只处理 `MEM-GAP-004` 的口径纠偏：把 qemu / soak 从 Memory owner gap 中移出，明确归入 packaging / release 环境复核。
2. authoritative 问题定义固定为：Memory 是否已经具备本机 installed-package / release-runner local installed authoritative evidence；若已具备，则不能继续把 qemu guest-side rerun 或 soak 当作 Memory 功能性 blocker。
3. 本轮明确不执行 qemu / kvm；若需更高层隔离验证，后续只能由 packaging / release gate owner 在对应环境完成。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| release-runner contract | `.github/workflows/release-package-gate.yml` 已在 qemu gate 前固定 local installed memory evidence 步骤 | release runner 对 Memory 的 authoritative owner 已经是 local installed，而不是 guest-side rerun |
| installed smoke artifact | `MEM-FIX-006` 已固定 `memory-proof.json`，覆盖 same-session marker recall、WAL、turn / summary rows 与 latest summary prefix | Memory 功能性正向链路已在真实安装态可重复验证 |
| owner boundary | `MEM-FIX-006` 交付件与 worklog 已明确：qemu / autopkgtest / soak 继续留给 packaging / release 环境复核，不再回流成 Memory blocker | `MEM-GAP-004` 的剩余项是环境级 follow-up，不应继续占据 Memory gap 清单 |

## 3. 外部参考

1. Debian `autopkgtest(1)` 说明 autopkgtest 是“在 testbed 上测试已安装二进制包”的工具，而 qemu 只是可选的 virtualization server / testbed backend 之一。由此可见 qemu / autopkgtest 的职责属于 release / packaging 环境隔离验证，而不是 Memory 功能 owner 本身。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| Memory owner 的 authoritative evidence 应固定在 local installed / release-runner local step，而不是 qemu guest-side rerun | 验收命令：`pkg_smoke_install.sh --explicit-start-check` |
| same-session summary recall 与 installed SQLite proof 必须能在真实安装态落盘 | artifact：`memory-proof.json` |
| qemu / soak 只能作为 packaging / release 环境复核，不再记为 Memory gap blocker | 文档目标：修正总账 `MEM-GAP-004` 与章节结论 |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-004` 的边界口径。
2. 本轮不新增产品代码；若 installed smoke 回退，才回到 `MEM-FIX-006` 的实现面补修。
3. 本轮不把 qemu / soak 重新引回 Memory acceptance。

## 6. 验证结果

1. `DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-smoke-test bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
	- 结果：通过；脚本输出 `[pkg-smoke-install] install smoke passed`。
2. `/tmp/dasall-smoke-test/memory-proof.json`
	- 结果：`expected_marker=mem-fix-006-local-proof`、`journal_mode=wal`、`session_turn_count_after_second=2`、`session_summary_count_after_second=2`，且 `latest_summary_text_prefix` 命中 marker。

## 7. 完成判定

1. `MEM-GAP-004` 已关闭。
2. Memory owner 的 authoritative evidence 已稳定落在 local installed / release-runner local step；qemu / autopkgtest / soak 仅保留为 packaging / release 环境复核。
3. 本结论不宣称当轮 L5 qemu rerun 或 L6 production-ready，只是停止把这些环境 gate 误记为 Memory 功能缺口。