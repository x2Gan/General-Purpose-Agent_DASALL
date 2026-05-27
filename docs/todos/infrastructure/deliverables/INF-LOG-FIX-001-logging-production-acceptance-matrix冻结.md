# INF-LOG-FIX-001 logging production acceptance matrix 冻结

- 日期：2026-05-27
- 任务：INF-LOG-FIX-001
- 状态：已完成（本轮只冻结 L1 acceptance matrix；不使用 qemu / kvm）

## 1. 执行前提

1. `docs/todos/DASALL_子系统查漏补缺专项记录.md` 在 13.5 中已明确：logging 当前仍停留在接口/骨架/focused evidence，后续实现必须先有 production acceptance matrix，才能进入真正的 production hardening。
2. `BLK-INF-LOG-001` 的实质不是代码缺失，而是 owner 还没有冻结 production sink backend、installed artifact path 和 non-extrapolation 边界；如果继续跳过设计冻结，后续 `INF-LOG-FIX-002~011` 仍会把 unit/fixture 结果误写成 production-ready。
3. 本轮用户约束已明确删除 qemu / kvm 口径依据，因此 logging owner 当前验收必须统一回收到 local installed authoritative evidence；任何 machine-isolated rerun 只允许作为 packaging / release handoff，不得再回流为本任务硬前置。

## 2. 设计冻结结论

### 2.1 evidence level

1. `LoggingProductionAcceptanceMatrix` 现已统一冻结 `L1 Design / SSOT`、L2 focused、L3 build-tree integrated、`L4 local installed authoritative evidence`、`L5 packaging / release handoff` 五级证据层。
2. 本轮完成判定只到 L1：matrix、gate、artifact taxonomy、Design -> Build 映射与 owner boundary 已冻结；未把任何现有实现上卷为 installed-ready 或 production-ready。
3. 后续 owner 验收统一以 L4 installed artifact 为准；L5 只保留 fresh package + local release handoff 语义，不再把 qemu / kvm 写成 logging owner 当前前置。

### 2.2 backend 与 path policy

1. v1 production primary backend 固定为 `spdlog-backed file / rotating sink`。
2. `stderr + ringbuffer` 只允许作为 degraded fallback，不得作为 primary sink owner。
3. build-tree 默认配置仍保留 `infra.logging.file.path=logs/runtime.log`，用于 focused/build-tree slice。
4. installed canonical path 冻结为 `state_root/logging/runtime.log`；packaged `state_root` 来自 `InstallLayout.state_root=/var/lib/dasall`，因此 installed owner path 固定为 `/var/lib/dasall/logging/runtime.log`。

### 2.3 artifact taxonomy 与 gate

1. installed proof artifact 名称已冻结为 `logging-installed-proof.json` 与 `logging-runtime-proof.json`；两者都要求 redacted proof，不允许明文 secret/token/password/auth value。
2. `INF-LOG-GATE-001~006` 已逐项绑定 code goal、test goal、installed artifact 与 owner boundary；后续任务若绕开这些 gate，视为偏离 matrix。
3. `LOG-TODO-001~019` 现被明确降级为“接口/骨架/focused evidence 已落盘”，不等于 production-ready；真正的 production 结论只能从 `INF-LOG-FIX-002~011` 与 `INF-LOG-SYS-FIX-*` 的 gate 闭合中产生。

### 2.4 ADR 与 industry practice 对齐

1. ADR-006：logging 只记录 owner-safe 标识，不生成 Memory 语义上下文。
2. ADR-007：logging 只发 advisory degraded/fallback/recovered signal，不执行 Runtime recovery。
3. ADR-008：logging 只消费 runtime/app 注入，不成为第二个全局 orchestrator。
4. OpenTelemetry Logs Data Model：本轮吸收 `TraceId / SpanId / Resource` correlation 字段分层与 attributes/body 模型，不引入新的跨模块 owner。
5. spdlog Asynchronous logging：本轮吸收单 worker ordering、`block` / `overrun_oldest` 两类 overflow policy 与 rotating file backend 结构，不把线程池实现细节写入 contracts。

## 3. Design -> Build 映射

| 设计冻结项 | 后续 Build 任务 |
|---|---|
| `spdlog-backed file / rotating sink` primary backend | `INF-LOG-FIX-003` |
| redaction/formatter 默认不可绕过 | `INF-LOG-FIX-002` |
| async queue / flush deadline / backpressure 语义 | `INF-LOG-FIX-004` |
| config projection / recovery / metrics / health / audit / query gate | `INF-LOG-FIX-005~009` |
| cross-subsystem e2e 与 installed proof artifact family | `INF-LOG-FIX-010~011`、`INF-LOG-SYS-FIX-*` |

## 4. 本轮验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_production_acceptance_contract_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingProductionAcceptanceContractTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
3. fallback 直接执行 `./build/vscode-linux-ninja/tests/contract/dasall_logging_production_acceptance_contract_test`
   - 结果：通过；contract 已验证 SSOT matrix、logging 详设、logging 专项 TODO 与系统总账四处 wording 同步闭合。

## 5. 完成判定

1. `BLK-INF-LOG-001` 已解阻：backend policy、artifact path、evidence level 与 non-extrapolation 边界都已冻结。
2. `INF-LOG-FIX-001` 已闭合：matrix 现在可以逐项绑定后续代码目标、测试目标、installed artifact 与 owner boundary。
3. 本轮不使用 qemu / kvm；logging owner 当前验收标准已经统一回收到 local installed authoritative evidence。