# KNO-GAP-010 local installed failure-injection closeout

来源任务：KNO-GAP-010
完成日期：2026-05-20
关联修复：KNO-FIX-006、KNO-FIX-010

## 1. 任务边界

1. 本轮只收口 `KNO-GAP-010` 的剩余本机 failure-injection 与 release boundary 口径，不把 `KNO-GAP-011` 的边界守卫自动化混入本轮。
2. authoritative 问题定义固定为：Knowledge 是否已经在真实 installed-package 环境中给出 active snapshot 损坏后的恢复证据，并把 qemu / machine-isolation rerun 重新归回 packaging / release 环境复核。
3. 本轮明确不使用 qemu / kvm；若需 guest-side rerun，只能由 release gate owner 在对应环境继续执行，不能再回流成 Knowledge owner blocker。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| local failure proof owner | `scripts/packaging/knowledge_failure_injection_installed_proof.sh` 已固定 fresh-install、active snapshot 损坏、daemon 重启、catalog / ledger / provider retrieve 复核，并落盘 `knowledge-failure-injection-proof.json`、`corpus-catalog-after-recovery.json`、`version-ledger-after-recovery.jsonl` | Knowledge 本机安装态已具独立于 qemu 的 failure-injection authoritative owner |
| release-runner contract | `.github/workflows/release-package-gate.yml` 已在 qemu gate 前固定 `knowledge-failure` artifact 目录；`scripts/packaging/README.md` 已把 failure proof 纳入 Knowledge local authoritative evidence | release workflow 现在能先落盘本机 failure-injection 证据，再进入更高层环境 gate |
| startup recovery owner | `KnowledgeServiceFactory` 与 `IndexStartupRecoveryTest` 已固定 startup recovery 要求：active snapshot 损坏时回退到上一版 LKG，并对齐 persisted catalog | 本轮实机 proof 不再是孤立脚本假设，而是对既有 recovery owner 的 installed-package 实机复验 |

## 3. 设计回链

1. Knowledge 详设已要求 startup recovery 在 active snapshot 缺失或损坏时回退 last-known-good，并把 `corpus_catalog.json` 的 active snapshot 校正到实际恢复出的 manifest；本轮 closeout 直接以该设计边界为裁决基线。
2. `KNO-FIX-006` 已在 build-tree 证明 `VersionLedger` / `CorpusCatalog` persistence 与 startup recovery 主链存在；`KNO-GAP-010` 的职责是把这条能力补成真实 installed-package authoritative evidence，而不是重复声明 qemu guest-side rerun。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| active snapshot 损坏后必须恢复到上一版 LKG 或从其重新派生新 active snapshot | 验收命令：`bash scripts/packaging/knowledge_failure_injection_installed_proof.sh --artifact-dir /tmp/dasall-kno-gap-010-failure` |
| recovery 后 catalog 不得继续指向损坏 snapshot，provider retrieve 必须继续返回 installed evidence | artifact：`corpus-catalog-after-recovery.json`、`knowledge-retrieve-after-recovery.json`、`knowledge-failure-injection-proof.json` |
| release workflow 必须在 qemu gate 前固定本机 failure-injection artifact | 代码目标：`.github/workflows/release-package-gate.yml`、`scripts/packaging/README.md` |
| qemu / machine-isolation 只能保留为 packaging / release follow-up，不再占用 Knowledge owner gap | 文档目标：总账 `KNO-GAP-010` 改写为 closeout |

## 5. D Gate

1. 范围单一：只处理 `KNO-GAP-010` 的本机 installed failure-injection 与 release boundary 口径。
2. 本轮不新增 Knowledge 业务语义 owner；只补 proof harness、workflow contract 与 traceability 文档。
3. 本轮不把 qemu / autopkgtest guest-side rerun 重新引回 Knowledge acceptance。

## 6. 验证结果

1. `sh -n scripts/packaging/knowledge_failure_injection_installed_proof.sh`
	- 结果：通过。
2. `bash scripts/packaging/knowledge_failure_injection_installed_proof.sh --artifact-dir /tmp/dasall-kno-gap-010-failure`
	- 结果：通过；脚本输出 `[knowledge-failure-proof] knowledge local installed failure injection proof passed`。
3. `/tmp/dasall-kno-gap-010-failure/knowledge-failure-injection-proof.json`
	- 结果：`recovery_mode=refreshed-from-last-known-good`、`recovered_parent_snapshot_id` 命中 `initial_active_snapshot_id`、`recovered_differs_from_corrupted=true`、`provider_slice_count_after_recovery=3`、`catalog_aligned_to_recovered_snapshot=true`、`corrupted_snapshot_removed_from_catalog=true`。
4. `/tmp/dasall-kno-gap-010-failure/corpus-catalog-after-recovery.json`
	- 结果：所有 installed corpora 的 `active_snapshot_id` 均已对齐恢复后的 snapshot，未继续指向损坏 snapshot。
5. `/tmp/dasall-kno-gap-010-failure/version-ledger-after-recovery.jsonl`
	- 结果：恢复后的 active snapshot entry 以初始 LKG snapshot 为 `parent_snapshot_id`，证明损坏 snapshot 未继续占据读路径 lineage。

## 7. 完成判定

1. `KNO-GAP-010` 已关闭。
2. Knowledge owner 现已具备真实 installed-package 的 failure-injection authoritative evidence，且 release workflow 已固定对应 artifact owner。
3. 本结论不外推为 qemu / autopkgtest guest-side PASS，也不外推为更高层 production soak；这些仅作为 packaging / release 环境后续复核。