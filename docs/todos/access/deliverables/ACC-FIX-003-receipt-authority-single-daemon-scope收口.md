# ACC-FIX-003 receipt authority / multi-instance 口径收口

来源任务：ACC-FIX-003
完成日期：2026-05-27

## 1. 任务边界

1. 本轮只收口 Access async receipt 的 authority 口径，不引入 receipt store/provider seam，也不把 streaming、release security matrix 或更高层环境 gate 混入本轮。
2. authoritative 问题定义固定为：当前 `AsyncTaskRegistry` 是否已经被明确为 v1 single-daemon / in-memory authority，以及 sibling daemon / restart 是否仍会 fail-closed，而不是被误读成共享权威事实。
3. 本轮不使用 qemu / kvm；证据仅来自本地 build-tree focused tests 与现有 installed receipt 事实的非外推校准。

## 2. 设计回链

1. `docs/architecture/DASALL_access子系统详细设计.md` 的 `AsyncTaskRegistry` 章节已经明确它“不做长期持久化”；`ACC-FIX-003` 将这一定义继续收紧为“v1 authority scope = 单 daemon 实例内的内存 registry”。
2. `docs/todos/access/DASALL_access子系统专项TODO.md` 中 `ACC-BLK-006` / `ACC-BLK-010` 先前只把 multi-instance authoritative sync 留在风险口径；本轮把风险文案收敛为精确边界：当前证据只覆盖 single-daemon authority，不能外推为 restart / multi-instance shared authority。
3. `ACC-FIX-001` 已证明 installed package 存在真实 async receipt 正向链路，但那条证据只说明“当前 daemon 可 authoritative 地回答自己的 receipt”，不说明“不同 daemon 或重启后仍共享 authority”。
4. OWASP Authorization Cheat Sheet 的 `Deny by Default`、`Validate the Permissions on Every Request` 与 `Create Unit and Integration Test Cases for Authorization Logic` 直接支持本轮收口策略：未知 authority 不得被默认放行，跨实例或重启后的 receipt 查询必须继续显式 fail-closed，并由自动化测试锁定。

## 3. 实现摘要

1. 新增 `tests/integration/access/AccessReceiptAuthorityIntegrationTest.cpp`。
   - 使用两个独立 `DaemonInProcessFixture + AsyncTaskRegistry` 组合根，验证 issuing daemon 产生的 receipt 在 sibling daemon 上查询时返回 `status_missing`。
   - 使用 fresh registry 重启 daemon，验证 pre-restart receipt 在 restarted daemon 上继续返回 `status_missing`。
   - 测试刻意复用同一 ownership secret，证明“同 secret”不等于“共享 authority”。
2. 更新 `tests/integration/access/CMakeLists.txt`。
   - 注册 `dasall_access_receipt_authority_integration_test` 与 `AccessReceiptAuthorityIntegrationTest`，使该边界成为可发现、可回归的 focused integration gate。
3. 本轮不修改生产代码。
   - 结论不是“已经具备跨实例 authority”，而是“现状边界已被显式声明并由测试锁定”。

## 4. Design -> Build 映射

| Design 目标 | Build / Validation 落点 |
|---|---|
| v1 receipt authority 只能属于 issuing daemon 的本地 registry | `AccessReceiptAuthorityIntegrationTest` 断言 sibling daemon 查询同一 receipt 返回 `status_missing` |
| daemon restart 不得把 fresh registry 伪装成旧 receipt 的共享 authority | `AccessReceiptAuthorityIntegrationTest` 断言 restarted daemon 对 pre-restart receipt 返回 `status_missing` |
| 现有 TTL / ownership / cancel 语义不能因口径收口而回退 | `DaemonReceiptTtlCleanupIntegrationTest`、`DaemonReceiptFlowIntegrationTest` 同轮回归通过 |
| installed async positive evidence 继续只代表 single-daemon scope | 总账 / Access TODO / 详设统一改写为 single-daemon authoritative wording，不再把 local registry 暗示为 global fact |

## 5. D Gate

1. 不新增 public ABI、contracts 字段或 daemon/gateway surface；所有改动仅限于测试与文档边界冻结。
2. 不宣称 restart / multi-instance authority 已实现；若未来需要跨实例共享 receipt，需要新开 receipt store/provider seam 任务承接。
3. installed async receipt positive evidence 继续有效，但其含义被显式限制为 single-daemon authoritative scope。
4. 不把本轮结论外推到 streaming、release security hardening、qemu 或 release-runner 证据。

## 6. 验证结果

1. `Build_CMakeTools(buildTargets=["dasall_access_receipt_authority_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["AccessReceiptAuthorityIntegrationTest","DaemonReceiptTtlCleanupIntegrationTest","DaemonReceiptFlowIntegrationTest"])`
   - 结果：命中仓库既有泛化错误 `生成失败`。
3. fallback：
   - 先执行 `Build_CMakeTools(buildTargets=["dasall_access_daemon_receipt_flow_integration_test"])` 以刷新 CLI binary compile definition。
   - 再执行 direct binaries：
     - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_receipt_authority_integration_test`
     - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_receipt_ttl_cleanup_integration_test`
     - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_receipt_flow_integration_test`
   - 结果：3/3 通过。

## 7. 完成判定

1. `ACC-FIX-003` 已完成。
2. Access 不再把本地 `AsyncTaskRegistry` 误写成跨实例 / 重启共享 authority；v1 receipt authority 已冻结为 single-daemon / in-memory scope。
3. 如果后续要宣称 restart / multi-instance authoritative receipt，需要单独新增 receipt store/provider seam 与对应 installed evidence；当前结论不外推。