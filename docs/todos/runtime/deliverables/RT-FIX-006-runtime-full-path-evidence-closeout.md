# RT-FIX-006 runtime full-path evidence closeout

来源任务：RT-FIX-006
完成日期：2026-05-21
关联缺口：RT-GAP-008
关联设计：`docs/architecture/DASALL_runtime子系统详细设计.md`、`docs/ssot/RuntimeAppCompositionV1.md`、`docs/ssot/BusinessChainIntegrationMatrix.md`、`docs/todos/runtime/deliverables/RT-FIX-001-runtime-path-evidence-closeout.md`

## 1. 任务边界

1. 本轮只收口 runtime L3 / L4 / L5 full-path evidence owner，不扩张到 scheduler model、optional degraded semantics、release runner soak 或新的 runtime 控制面语义。
2. authoritative 问题定义固定为：L3 app-binary cognition-positive、L4 installed direct/tool/recovery positive-negative probes、L5 packaging / release handoff 必须由不同 owner 分层记录；单次 installed `dasall run` 的 direct LLM 成功不得再外推为 cognition / tools / recovery full path。
3. 用户已明确禁止使用 qemu / kvm；本轮只使用既有 app-binary owner、fresh Debian package build 与 local installed package smoke 作为权威证据，qemu / release 只保留 handoff contract，不宣称当轮重跑通过。

## 2. 本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| L3 app-binary owner | `Gate-INT-10` / `DaemonBinaryUnarySmokeTest` 继续持有 app-binary cognition-positive unary owner | cognition-positive 不再依赖 installed `dasall run` 或 package-smoke 人工外推 |
| installed proof helper | `apps/daemon/src/RuntimeInstalledProofRunner.h/.cpp`、`apps/daemon/src/RuntimeInstalledProofMain.cpp`，并由 `apps/daemon/CMakeLists.txt` / `debian/dasall-daemon.install` 安装到 `/usr/lib/dasall/dasall-runtime-installed-proof` | installed package 现在拥有 runtime 自己的 tool / waiting / recovery probe owner，而不是借用 CLI 输出或 qemu 历史文档 |
| installed proof regression | `tests/unit/apps/daemon/RuntimeInstalledProofRunnerTest.cpp` 锁定 `tool_positive`、waiting checkpoint、`recovery_positive`、recovery-negative binding reject | helper 的字段口径与 checkpoint persistence 语义已被 focused regression 固定 |
| package-smoke authoritative owner | `scripts/packaging/pkg_smoke_install.sh --explicit-start-check` 同轮写出 `runtime-installed-proof.json` 与 `runtime-proof.json` | runtime direct/tool/recovery 现有 installed authoritative artifact，可被 package-smoke / release archive 统一归档 |
| fresh package build | clean-copy `dpkg-buildpackage -us -uc -b` 通过，fresh `.deb` 内确认存在 `/usr/lib/dasall/dasall-runtime-installed-proof` | package-smoke 使用的是 fresh package 真实安装态，而不是工作树临时二进制 |

## 3. 设计结论

1. runtime full-path evidence 已按 owner 分层：L3 app-binary 负责 cognition-positive，L4 installed package 负责 direct/tool/recovery local evidence，L5 只保留 packaging / release handoff contract。
2. `pkg_smoke_install.sh` 现在是 runtime installed authoritative owner：`runtime-installed-proof.json` 保存 helper 原始字段，`runtime-proof.json` 保存面向 BC 矩阵的直接摘要；两者都不再与 `run-first.json` / `run-second.json` 混用语义。
3. installed proof helper 是 runtime 证据探针，不是第二个 orchestrator owner。它复用 live dependency composition 与 runtime-local stub dependency set，只负责把已存在的 runtime path / waiting / recovery 事实投影为 installed artifact。
4. 本轮不宣称 fresh qemu success。L5 仍然是 packaging / release 的 handoff contract 与历史锚点，后续只在 release-runner / qemu 任务中补当轮 rerun 与归档。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 落点 |
|---|---|
| 为 installed package 增加 runtime full-path probe owner | `apps/daemon/src/RuntimeInstalledProofRunner.h/.cpp`、`apps/daemon/src/RuntimeInstalledProofMain.cpp`、`apps/daemon/CMakeLists.txt`、`debian/dasall-daemon.install` |
| 锁定 installed proof helper 字段口径 | `tests/unit/apps/daemon/RuntimeInstalledProofRunnerTest.cpp` |
| 把 runtime direct/tool/recovery artifact 收口到 package-smoke | `scripts/packaging/pkg_smoke_install.sh`、`scripts/packaging/README.md` |
| 让 fresh package build 在 local cache / offline 环境可复验 | `debian/rules` |
| 把 runtime evidence 分层回写到总账与 BC 矩阵 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/ssot/BusinessChainIntegrationMatrix.md` |

## 5. D Gate

1. 范围单一：只处理 `RT-FIX-006` / `RT-GAP-008`。
2. 本轮不扩张到 `RT-GAP-006` optional degraded semantics、`RT-GAP-007` scheduler / background worker 模型，也不把当前结果外推为当轮 qemu / release PASS。
3. 本轮不使用 qemu / kvm；更高层环境证据继续留给 packaging / release gate。

## 6. 验证结果

1. `./build/vscode-linux-ninja/tests/unit/apps/daemon/dasall-daemon_runtime_installed_proof_runner_unit_test`：通过。
2. clean-copy `dpkg-buildpackage -us -uc -b`：通过；fresh `.deb` 内确认存在 `/usr/lib/dasall/dasall-runtime-installed-proof`。
3. `DASALL_PACKAGE_SMOKE_ARTIFACT_DIR=/tmp/dasall-rt-fix-006-pkg-smoke bash scripts/packaging/pkg_smoke_install.sh --explicit-start-check`：通过，并落盘 `runtime-installed-proof.json` 与 `runtime-proof.json`。
4. `runtime-installed-proof.json` 关键字段为正：`tool_runtime_path=runtime_path:tool_positive`、`waiting_status=PartiallyCompleted`、`recovery_positive_runtime_path=runtime_path:recovery_positive`、`recovery_positive_checkpoint_persisted=true`、`recovery_negative_binding_rejected=true`。
5. `runtime-proof.json` 关键字段为正：`direct_llm_llm_origin_present=true`、`tool_positive_task_completed=true`、`recovery_positive_task_completed=true`、`recovery_negative_binding_rejected=true`。

## 7. 完成判定

1. `RT-GAP-008` 已在当前树关闭。
2. BC-05 / BC-06 / BC-11 / BC-15 现已按 owner 分层记录，不再把 direct/tool/recovery/cognition 证据混写在同一条 installed `run` 结果上。
3. runtime installed proof owner 已固定到 package-smoke artifact；release-runner / qemu 仍只保留 handoff contract 与后续 rerun 入口，不在本轮伪造通过。