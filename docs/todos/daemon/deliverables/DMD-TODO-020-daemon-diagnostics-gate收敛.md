# DMD-TODO-020 daemon diagnostics gate 收敛

完成时间：2026-04-29

## 任务结论

已为 daemon 本地控制面补齐只读 diagnostics command gate：

1. daemon 诊断命令只允许走只读白名单
2. 诊断请求必须通过 local trusted daemon subject 的专用 policy gate
3. `diag.enabled=false` 时默认 fail-closed
4. 未开放远程导出与写操作命令

## 解阻说明

`DMD-BLK-006` 已在本轮最小解组：

1. 只读命令白名单直接复用 `infra/include/diagnostics/DiagnosticsTypes.h` 中的 `kReadOnlyCommandWhitelist`
2. daemon 侧与 infra 的绑定 seam 冻结为 `IDiagnosticsService`
3. `DaemonDiagnosticsHandler` 仅依赖公共接口与白名单，不引入 infra 私有实现依赖

## 代码变更

1. 更新 `access/src/AccessPolicyGate.h` / `access/src/AccessPolicyGate.cpp`
   - 新增 `evaluate_diagnostics_request()`
   - 新增 `PolicyBackendSnapshot.allow_diagnostics`
2. 新增 `access/src/daemon/DaemonDiagnosticsHandler.h`
3. 新增 `access/src/daemon/DaemonDiagnosticsHandler.cpp`
4. 更新 `access/include/AccessGatewayFactory.h`
   - 增加 daemon diagnostics service seam
5. 更新 `access/src/AccessGatewayFactory.cpp`
   - 接入 `diag/diagnostics` routing
   - 解析 `command_name=...` payload
   - 在 policy gate 通过后调用 `DaemonDiagnosticsHandler`
6. 更新 `access/CMakeLists.txt`
   - 注册 diagnostics handler 实现
7. 新增测试：
   - `tests/unit/access/DaemonDiagnosticsHandlerTest.cpp`
   - `tests/unit/access/DiagnosticsCommandPolicyTest.cpp`
   - `tests/integration/access/DaemonDiagDenyIntegrationTest.cpp`
8. 更新测试注册：
   - `tests/unit/access/CMakeLists.txt`
   - `tests/integration/access/CMakeLists.txt`

## 验收

1. 构建：`Build_CMakeTools`（通过）
2. 定向测试：`RunCtest_CMakeTools(tests=["DaemonDiagnosticsHandlerTest","DiagnosticsCommandPolicyTest","DaemonDiagDenyIntegrationTest"])`（通过）

## 完成判定对照

1. diag 默认关闭：已满足，`diag_disabled`
2. 未授权拒绝：已满足，`daemon_peer_identity_required`
3. 只读白名单外拒绝：已满足，`diag_command_not_allowed`
4. 远程导出/写操作不可达：已满足，daemon 侧仅接受 infra 白名单命令名，不暴露 export/write 命令面

## 边界说明

1. 本轮未让 `apps/daemon` 直接依赖 `DiagnosticsServiceFacade` concrete，实现上仍守住公共依赖面
2. 真正的 concrete diagnostics service 装配可在后续交付中通过 public factory 或 composition seam 收口
