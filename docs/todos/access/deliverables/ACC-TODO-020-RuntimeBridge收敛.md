# ACC-TODO-020 设计收敛文档

## 1. 任务定义

实现 RuntimeBridge，作为 Access 到 Runtime 的唯一 dispatch/cancel bridge seam，提供 sync/accepted-async/reject 三出口映射，并保证 cancel 仅转发不自裁定。

本任务范围：

1. 落盘 `access/src/RuntimeBridge.h` 与 `access/src/RuntimeBridge.cpp`。
2. 将 RuntimeBridge 接入 `dasall_access` 静态库。
3. 新增 `RuntimeBridgeTest`、`RuntimeBridgeAsyncAcceptTest`、`RuntimeBridgeRejectMappingTest`。
4. 回写 TODO 与 worklog，并完成单任务提交推送。

## 2. 边界与职责

### 2.1 职责

1. 对接 `IAccessRuntimeBridge`，收敛 access -> runtime 调用入口。
2. 在 dispatch 前执行 bridge 侧 fail-closed 前置校验（如 `normalizer_ready`、Allow 决策）。
3. 对 runtime 结果进行统一映射，补齐 request/session/trace 响应上下文。
4. cancel 只做参数校验和后端转发，不在 access 层做最终取消裁定。

### 2.2 非职责

1. 不负责认证、授权、admission 与 normalizer 逻辑。
2. 不负责协议编码和发布。
3. 不实现 runtime 内部调度/恢复策略。
4. 不扩展 runtime public ABI，只保持 bridge-local 适配。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.9 主流程规定 RuntimeBridge 在 RequestNormalizer 之后，负责 dispatch/cancel 语义。
2. Access 详设 6.7 与 IAccessRuntimeBridge 冻结了双接口：`dispatch()` 与 `cancel()`。
3. ACC-TODO-001/011 已冻结 runtime seam 口径，本任务可直接实现 bridge-local 映射。

### 3.2 外部参考

1. 微服务桥接常见原则：桥接层在输入不满足前置条件时应 fail-closed，避免不完整请求进入下游执行器。

## 4. 数据与接口说明

### 4.1 新增类型

1. `RuntimeBridge::DispatchBackend`
   - 语义：bridge-local 可注入 dispatch 后端（测试/适配器）。
2. `RuntimeBridge::CancelBackend`
   - 语义：bridge-local 可注入 cancel 转发后端。

### 4.2 核心接口

1. `dispatch(const RuntimeDispatchRequest&)`
2. `cancel(std::string_view request_id, std::string_view actor_ref)`
3. `map_runtime_result(...)`
4. `map_runtime_reject(...)`

## 5. 流程/时序

1. `dispatch()` 先检查 `normalizer_ready=true` 与 `decision_proof=Allow`。
2. 若后端未配置或前置失败，返回 reject 映射（含 `error_code`）。
3. 调用 dispatch backend 并通过 `map_runtime_result()` 补齐追踪上下文。
4. 对 accepted-async 且无 receipt 的返回自动补齐 fallback receipt。
5. `cancel()` 仅校验 `request_id/actor_ref` 非空后转发。

## 6. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| RuntimeBridge 组件实现 | `access/src/RuntimeBridge.h`、`access/src/RuntimeBridge.cpp` |
| access 静态库接线 | `access/CMakeLists.txt` |
| sync dispatch 映射 | `tests/unit/access/RuntimeBridgeTest.cpp` |
| accepted-async fallback receipt | `tests/unit/access/RuntimeBridgeAsyncAcceptTest.cpp` |
| reject 映射与 cancel 转发 | `tests/unit/access/RuntimeBridgeRejectMappingTest.cpp` |
| 测试注册 | `tests/unit/access/CMakeLists.txt` |

## 7. 文件范围

1. `access/src/RuntimeBridge.h`
2. `access/src/RuntimeBridge.cpp`
3. `access/CMakeLists.txt`
4. `tests/unit/access/RuntimeBridgeTest.cpp`
5. `tests/unit/access/RuntimeBridgeAsyncAcceptTest.cpp`
6. `tests/unit/access/RuntimeBridgeRejectMappingTest.cpp`
7. `tests/unit/access/CMakeLists.txt`
8. `docs/todos/access/DASALL_access子系统专项TODO.md`
9. 本文档

## 8. 验收三件套

### 8.1 代码目标

1. 实现 RuntimeBridge 组件。
2. 完成 `dispatch/cancel/map_runtime_result/map_runtime_reject` 四类能力。

### 8.2 测试目标

1. `RuntimeBridgeTest`
2. `RuntimeBridgeAsyncAcceptTest`
3. `RuntimeBridgeRejectMappingTest`

### 8.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_runtime_bridge_unit_test \
  dasall_access_runtime_bridge_async_accept_unit_test \
  dasall_access_runtime_bridge_reject_mapping_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "RuntimeBridge(Test|AsyncAcceptTest|RejectMappingTest)" --output-on-failure
```

## 9. 风险与回退

1. 当前后端通过可注入函数表达，后续接 runtime 实体适配器时需保持 dispatch/cancel 语义稳定。
2. fallback receipt 仅作为 accepted-async 最小兜底，不应覆盖真实 runtime receipt。
3. 若后续 runtime reject taxonomy 扩展，应在 `map_runtime_reject` 增补映射并补回归测试。
