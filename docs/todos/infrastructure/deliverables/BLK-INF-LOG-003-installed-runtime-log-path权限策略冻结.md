# BLK-INF-LOG-003 installed/runtime log path 权限策略冻结

关联任务：BLK-INF-LOG-003  
日期：2026-05-27  
结论级别：L1 design / SSOT freeze

## 1. blocker 结论

`BLK-INF-LOG-003` 的真实缺口不是 file sink 能否写盘，而是 installed/runtime writable log path 与 permission policy 还没有写成正式 owner 规则，导致 build-tree 临时路径、state_root override 与 package smoke authoritative evidence 容易混写。

本轮冻结后，后续 `INF-LOG-FIX-003` / `INF-LOG-FIX-011` 必须统一遵守以下口径：

1. `DASALL_STATE_ROOT` 是唯一允许的 state_root override。
2. build-tree focused 继续允许默认相对路径 `logs/runtime.log`，但它只代表 L2/L3 build-tree evidence。
3. installed authoritative path 固定为 `state_root/logging/runtime.log`；packaged canonical path 固定为 `/var/lib/dasall/logging/runtime.log`。
4. 其他不可写、越界或权限拒绝路径必须 fail-closed 返回 sink IO failure，不得 silently fallback 到 repo 根、`/tmp` 或 qemu guest-side 路径。
5. package smoke authoritative evidence 只接受 `state_root/logging/runtime.log` 与同目录 rotation family。

## 2. 回写位置

1. `docs/ssot/LoggingProductionAcceptanceMatrix.md`
2. `docs/architecture/DASALL_infra_logging模块详细设计.md`
3. `docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md`
4. `docs/todos/DASALL_子系统查漏补缺专项记录.md`
5. `tests/contract/smoke/LoggingProductionAcceptanceContractTest.cpp`

## 3. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_production_acceptance_contract_test"])`
2. `RunCtest_CMakeTools(tests=["LoggingProductionAcceptanceContractTest"])`
3. 若命中仓库既有 `生成失败`，fallback 直接执行 `build/vscode-linux-ninja/tests/contract/dasall_logging_production_acceptance_contract_test`

## 4. 解阻结果

`BLK-INF-LOG-003` 现可关闭；后续 `INF-LOG-FIX-003` 只需要实现 file / rotating sink adapter 本身，不再重复争论 installed/runtime writable path 与 permission policy 口径。