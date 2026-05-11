# FULLINT-TODO-010 packaging preflight 与 Gate-INT-10 命令关系

日期：2026-05-11

范围：SystemIntegrationGateMatrix、packaging README、全量业务链集成专项 TODO

## 1. 结论

`FULLINT-TODO-010` 已完成。`dasall_gate_int_10` 与 `dasall_packaging_preflight_tests` 继续共同组成 build-tree `release-preflight` 的正式入口，但两个 target 的证据语义已经明确拆开：

1. 单跑 `dasall_gate_int_10` 只能证明 app-binary smoke slice passed。
2. 单跑 `dasall_packaging_preflight_tests` 只能证明 package preflight slice passed。
3. 只有两个 target 在同一证据包中都通过，才能写为 build-tree `release-preflight` complete。
4. build-tree `release-preflight` complete 仍不代表 installed-package ready、qemu passed 或 production release-ready。

因此，`dasall_packaging_preflight_tests` 单独通过不得再被写成 gateway binary ready、daemon/CLI binary ready 或 Gate-INT-10 passed。

## 2. 命令关系

| 命令 / target | Owner | 允许结论 | 禁止外推 |
|---|---|---|---|
| `Build_CMakeTools(buildTargets=["dasall_gate_int_10"])` | Gate-INT-10 / app-binary | app-binary smoke slice passed；daemon/gateway binary smoke 与 startup diagnostics 当前通过 | packaging preflight passed、installed-package ready、qemu passed |
| `Build_CMakeTools(buildTargets=["dasall_packaging_preflight_tests"])` | packaging preflight / package contract slices | package preflight slice passed；package metadata / contract / daemon preflight 切片当前通过 | gateway binary ready、daemon/CLI binary ready、Gate-INT-10 passed、build-tree release-preflight complete |
| `Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])` | build-tree release-preflight | build-tree `release-preflight` complete | installed-package ready、qemu passed、production release-ready |
| `sh scripts/packaging/validate_gate_int_10_installed_package_qemu.sh -- qemu <image-or-config>` | handoff / ordering | build-tree release-preflight 先通过，再构包并执行 qemu autopkgtest | 不改变 Gate-INT-10 与 PKG-GATE-07 owner 边界 |

## 3. 文档落点

| 文件 | 变化 |
|---|---|
| `docs/ssot/SystemIntegrationGateMatrix.md` | 在 `Gate-INT-10` / `release-preflight` fallback rule 中新增单目标结果词汇：单跑 packaging preflight 不得宣称 gateway/daemon binary ready；单跑 Gate-INT-10 不得宣称 packaging/installed ready；两个 target 同轮均过才是 build-tree release-preflight complete |
| `scripts/packaging/README.md` | 在 Gate 入口章节新增单目标结果词汇表，明确 `dasall_packaging_preflight_tests` 的允许结论与禁止外推 |
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | `FULLINT-TODO-010` 标记 Done，并把完成判定改为两个 target 的同轮证据关系 |
| `docs/worklog/DASALL_开发执行记录.md` | 记录 #630 回写验证证据 |

## 4. 验收证据

1. `rg -n "dasall_gate_int_10|dasall_packaging_preflight_tests|gateway binary|daemon/CLI|release-preflight" docs/ssot/SystemIntegrationGateMatrix.md scripts/packaging/README.md docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md`
   - 结果：通过；三个文档均可检索到 `dasall_gate_int_10`、`dasall_packaging_preflight_tests`、`gateway binary`、`daemon/CLI` 与 `release-preflight` 相关说明。
2. `git diff --check`
   - 结果：通过；无 trailing whitespace 或补丁格式问题。

## 5. 边界

本任务只梳理 build-tree `release-preflight` 的命令语义，不新增 CMake target、不重跑 package build，也不宣称 qemu / installed-package 权威结果。release runner / qemu / lintian / installed LLM smoke 仍由 `FULLINT-TODO-019` 与 packaging gate 承接。