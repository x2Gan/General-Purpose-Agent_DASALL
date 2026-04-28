# DMD-TODO-013 daemon AccessGateway submit pipeline 收敛

- 任务 ID: DMD-TODO-013
- 完成日期: 2026-04-28
- 状态: Done
- 对应设计: docs/architecture/DASALL_daemon本地控制面详细设计.md 6.6.2

## 1. 目标与边界

本任务目标是让 daemon 请求不再走 `AccessGateway` 空 `submit_pipeline`，而是通过 Access 主链完成：

1. RequestValidator
2. SubjectResolver
3. AuthenticatorChain
4. AccessPolicyGate
5. AdmissionController
6. RequestNormalizer
7. RuntimeBridge
8. ResultPublisher

边界保持：

1. `apps/daemon` 只消费 `access/include` 公共面。
2. 不把 daemon 私有协议对象写入 `contracts/`。
3. Runtime 仍通过 bridge seam 调用，不直接触碰 runtime 内部编排器。

## 2. 代码改动

1. 扩展 `access/include/AccessGatewayFactory.h`：
   - 新增 `DaemonAccessPipelineOptions`。
   - 新增 `create_daemon_access_gateway()` 公共工厂。
2. 更新 `access/src/AccessGatewayFactory.cpp`：
   - 新增 `build_daemon_submit_pipeline()` 组装链路。
   - 在 pipeline 中接线 validator/resolver/auth/policy/admission/normalizer/runtime/publisher。
   - 固化三类 fail-closed 路径：`unknown_command`、认证失败、payload 超限。
3. 更新 `apps/daemon/src/main.cpp`：
   - 由 `create_access_gateway()` 切换为 `create_daemon_access_gateway()`。
   - 从 daemon 配置投影 `max_payload_bytes` 到 pipeline。
   - 以当前本地 uid 生成 trusted local subject allowlist。
4. 新增 `tests/unit/apps/daemon/DaemonAccessPipelineFactoryTest.cpp`：
   - 覆盖 unknown/auth deny/payload too large 不进入 runtime。
   - 覆盖 valid submit 能进入 runtime。
5. 更新 `tests/unit/apps/daemon/CMakeLists.txt`：
   - 注册 `DaemonAccessPipelineFactoryTest`。

## 3. 验证证据

1. 构建：
   - 通过 CMake Tools 构建 `dasall_daemon` 与 `dasall_unit_tests`（编译链通过）。
2. 定向测试：
   - `DaemonAccessPipelineFactoryTest` 通过。
   - `AccessGatewayFacadeTest` 通过。
   - `AdmissionControllerTest` 通过。
   - `RequestNormalizerTest` 通过。
   - `ResultPublisherTest` 通过。

## 4. 完成判定

已满足 DMD-TODO-013 完成条件：

1. daemon 请求不再依赖空 submit pipeline。
2. unknown command、auth deny、payload too large 均在进入 runtime 前被拒绝。
3. pipeline 组装点与单测拓扑已落盘并可执行。
