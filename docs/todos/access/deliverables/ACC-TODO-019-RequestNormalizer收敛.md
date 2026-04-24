# ACC-TODO-019 设计收敛文档

## 1. 任务定义

实现 RequestNormalizer，收敛入口请求到 `AgentRequest` 与 access sidecar，稳定生成并传播 `request_id/session_id/trace_id`，并构造发布上下文，确保后续 RuntimeBridge/ResultPublisher 在统一输入上工作。

本任务范围：

1. 落盘 `access/src/RequestNormalizer.h` 与 `access/src/RequestNormalizer.cpp`。
2. 将 RequestNormalizer 接入 `dasall_access` 静态库。
3. 新增 `RequestNormalizerTest`、`RequestNormalizerIdentityProjectionTest`、`RequestNormalizerConstraintProjectionTest`、`RequestNormalizerContractCompatibilityTest`。
4. 回写 TODO 与 worklog，并完成单任务提交推送。

## 2. 边界与职责

### 2.1 职责

1. 将入口事实归一化为 `AgentRequest`，并保持与 contracts guard 兼容。
2. 生成或复用 `request_id/session_id/trace_id`，保证拒绝路径与成功路径都可追踪。
3. 只投影白名单上下文字段（如 `constraint_set`），防止污染 shared contracts。
4. 构造 `PublishEnvelope` 初始上下文，供后续发布链路复用。

### 2.2 非职责

1. 不做认证、授权、admission 判定。
2. 不直接调用 runtime。
3. 不做协议响应编码和发布发送。
4. 不修改 contracts 定义，只做投影和兼容性保证。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.8 指定数据流：`InboundPacket -> AgentRequest -> RuntimeDispatchRequest -> PublishEnvelope`。
2. Access 详设 6.9 主流程规定 RequestNormalizer 位于 Admission 后、RuntimeBridge 前。
3. `contracts/include/agent/AgentRequestGuards.h` 已冻结 required/boundary/field 规则，可作为投影正确性的可执行判据。

### 3.2 外部参考

1. OWASP 输入与上下文边界实践：只投影白名单字段，避免将未治理上下文隐式带入主契约。

## 4. 数据与接口说明

### 4.1 新增数据结构

1. `RequestNormalizationOutput`
   - 字段：`normalized`、`runtime_request`、`publish_context`、`agent_request`、`error`
   - 用途：统一返回归一化结果与错误。

2. `TraceIdentityBundle`
   - 字段：`request_id`、`session_id`、`trace_id`
   - 用途：统一标识生成/复用结果。

### 4.2 核心接口

1. `normalize(const RuntimeDispatchRequest&)`
2. `ensure_trace_ids(const RuntimeDispatchRequest&)`
3. `project_agent_request(const RuntimeDispatchRequest&, const TraceIdentityBundle&)`
4. `build_publish_context(const RuntimeDispatchRequest&, const TraceIdentityBundle&)`

## 5. 流程/时序

1. `normalize()` 校验必要入口字段（packet metadata + actor_ref）。
2. 调用 `ensure_trace_ids()`：优先复用请求上下文已有标识，否则按稳定策略生成。
3. 调用 `project_agent_request()`：将入口字段与白名单上下文映射到 `AgentRequest`。
4. 调用 `build_publish_context()`：构造统一发布上下文初始值。
5. 回写 runtime sidecar：注入 `request_id/session_id/trace_id/normalizer_ready`。

## 6. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| Normalizer 组件与输出模型 | `access/src/RequestNormalizer.h`、`access/src/RequestNormalizer.cpp` |
| access 静态库接线 | `access/CMakeLists.txt` |
| trace id 生成与 ready 标记 | `tests/unit/access/RequestNormalizerTest.cpp` |
| 主体/通道投影正确性 | `tests/unit/access/RequestNormalizerIdentityProjectionTest.cpp` |
| 白名单约束投影 | `tests/unit/access/RequestNormalizerConstraintProjectionTest.cpp` |
| contracts 兼容性 | `tests/unit/access/RequestNormalizerContractCompatibilityTest.cpp` |
| 测试注册 | `tests/unit/access/CMakeLists.txt` |

## 7. 文件范围

1. `access/src/RequestNormalizer.h`
2. `access/src/RequestNormalizer.cpp`
3. `access/CMakeLists.txt`
4. `tests/unit/access/RequestNormalizerTest.cpp`
5. `tests/unit/access/RequestNormalizerIdentityProjectionTest.cpp`
6. `tests/unit/access/RequestNormalizerConstraintProjectionTest.cpp`
7. `tests/unit/access/RequestNormalizerContractCompatibilityTest.cpp`
8. `tests/unit/access/CMakeLists.txt`
9. `docs/todos/access/DASALL_access子系统专项TODO.md`
10. 本文档

## 8. 验收三件套

### 8.1 代码目标

1. 实现 RequestNormalizer 组件与四个目标函数。
2. 完成 trace id、contracts 投影、publish context 的统一收敛。

### 8.2 测试目标

1. `RequestNormalizerTest`
2. `RequestNormalizerIdentityProjectionTest`
3. `RequestNormalizerConstraintProjectionTest`
4. `RequestNormalizerContractCompatibilityTest`

### 8.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_request_normalizer_unit_test \
  dasall_access_request_normalizer_identity_projection_unit_test \
  dasall_access_request_normalizer_constraint_projection_unit_test \
  dasall_access_request_normalizer_contract_compatibility_unit_test \
  dasall_contract_tests && \
ctest --test-dir build/vscode-linux-ninja -R "RequestNormalizer(Test|IdentityProjectionTest|ConstraintProjectionTest|ContractCompatibilityTest)|AgentRequestContractTest|AgentResultContractTest" --output-on-failure
```

## 9. 风险与回退

1. 当前 ID 生成策略为模块内稳定生成，后续若接入集中式 trace 服务，需保持字段语义不变。
2. 当前只投影白名单字段；若新增业务字段，必须先更新设计与测试再扩展投影。
3. 若 contracts guard 规则升级，优先通过 `RequestNormalizerContractCompatibilityTest` 暴露回归，再调整投影逻辑。
