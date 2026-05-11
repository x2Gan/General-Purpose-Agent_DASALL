# FULLINT-TODO-008 gateway shutdown exit-code 断言

日期：2026-05-11

范围：GatewayBinaryUnarySmokeTest、Gate-INT-10 app-binary shutdown confidence

## 1. 结论

`FULLINT-TODO-008` 已完成。`GatewayBinaryUnarySmokeTest` 不再只读取 `gateway_exit_code` 并把它写入后续失败消息，而是在 successful submit 后立即断言 gateway 进程 shutdown exit code 必须为 `0`。

该补丁封住了一个 release-preflight 盲点：如果 HTTP submit 已经返回 200，但 gateway 在测试收尾阶段以非零码退出，Gate-INT-10 现在会直接失败并输出 `artifact_path` 与 `gateway_log`。

## 2. 代码落点

| 文件 | 变化 |
|---|---|
| `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp` | 在 `gateway.stop()` 后新增 `assert_equal(0, gateway_exit_code, ...)`，失败消息包含 `artifact_path` 与完整 `gateway_log` |
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | `FULLINT-TODO-008` 标记 Done；`FULLINT-BLK-005` 标记 shutdown 部分解阻，startup diagnostics 缺口继续归 009 |
| `docs/worklog/DASALL_开发执行记录.md` | 记录 #628 回写验证证据 |

## 3. 验收证据

1. `Build_CMakeTools(buildTargets=["dasall_access_gateway_binary_unary_smoke_integration_test","dasall_gate_int_10"])`
   - 结果：通过。
   - 证据：gateway binary smoke 与 missing-backend regression 执行通过；Gate-INT-10 expected-test discoverability、label discoverability 与 acceptance passed。
2. `RunCtest_CMakeTools(tests=["GatewayBinaryUnarySmokeTest"])`
   - 结果：通过。

## 4. 边界

本任务只解 `FULLINT-BLK-005` 的 gateway shutdown exit-code 部分。runtime dependency composition、runtime init、AccessGateway init、listen/bind failure stage 覆盖仍由 `FULLINT-TODO-009` 继续处理。