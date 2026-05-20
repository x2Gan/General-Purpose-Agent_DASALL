# MEM-GAP-007 installed maintenance closeout

来源任务：MEM-GAP-007
完成日期：2026-05-20
关联修复：MEM-FIX-007

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-007`，不把 `MEM-GAP-004` 的 release boundary、`MEM-GAP-005/006` 的 build-tree gate 或 qemu / soak 环境复核混入本轮。
2. authoritative 问题定义固定为：installed maintenance 正向证据是否已在真实安装态 DB 上可重复观测，并稳定落盘为 `memory-maintenance-proof.json`。
3. 若本轮复验通过，则 `MEM-GAP-007` 保持已闭合；若复验失败，才回到 helper / package smoke / maintenance runner 局部实现修复。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| installed helper | `/usr/lib/dasall/dasall-memory-maintenance-proof` 已作为 daemon package-private helper 安装落地 | maintenance proof 不再只停留在 build-tree 工具 |
| package smoke artifact | `scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 已固定产出 `memory-maintenance-proof.json` | installed maintenance evidence 有稳定 artifact contract |
| proof fields | `checkpoint_wal_pages_remaining`、`turns_before/after`、`quarantine_rows_after`、retention flags 已进入 artifact | checkpoint / retention / quarantine cleanup 在真实安装态可观测 |

## 3. 外部参考

1. SQLite WAL 文档说明 checkpoint 会把 WAL 中累积的事务回写到原始数据库文件；若 checkpoint 能完成且没有读者继续占用 WAL，则 WAL 可被重置。`MEM-GAP-007` 的 `checkpoint_wal_pages_remaining=0` 正是用来证明 maintenance 路径已把 WAL checkpoint drain 到可接受状态。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| maintenance helper 必须在真实安装态复用真实 profile/config/install layout | 验收命令：installed package smoke |
| checkpoint / retention / quarantine cleanup 必须产出稳定 JSON 证据 | artifact：`memory-maintenance-proof.json` |
| GAP closeout 必须以本机 authoritative installed evidence 为准，不依赖 qemu / soak | 验收命令：`pkg_smoke_install.sh --explicit-start-check` |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-007`。
2. 依赖方向不变：helper 继续复用 daemon / memory 既有 owner 组合，不引入新的跨模块 owner。
3. 本轮不修改产品代码；若验证失败，才回到 helper / smoke / maintenance proof 相关局部实现面。

## 6. 验证结果

1. `DASALL_DEEPSEEK_API_KEY_FILE="$HOME/.local/share/dasall/secrets/deepseek-prod.secret" DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-mem-gap-007-closeout bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`
	- 结果：通过；installed authoritative smoke 成功执行并生成 artifact。
2. `/tmp/dasall-mem-gap-007-closeout/memory-maintenance-proof.json`
	- 结果：`ok=true`、`checkpoint_executed=true`、`checkpoint_wal_pages_remaining=0`、`turns_before=481`、`turns_after=480`、`retention_turns=480`、`quarantine_rows_after=0`、`protected_turn_retained=true`、`purged_turn_removed=true`、`newest_turn_retained=true`。

## 7. 完成判定

1. `MEM-GAP-007` 已关闭。
2. installed maintenance helper 与 `memory-maintenance-proof.json` 在当前真实安装态上复验通过，checkpoint / retention / quarantine cleanup 继续可重复观测。
3. 本结论不外推到 qemu / soak；这些仍是 packaging / release 环境复核范围。