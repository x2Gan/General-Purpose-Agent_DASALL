# DMD-TODO-014 DaemonRuntimeBridge unary 收敛

## 1. 任务范围

- 任务 ID：DMD-TODO-014
- 目标：接线 DaemonRuntimeBridge 到 runtime AgentFacade unary，且 Runtime 未初始化时 fail-closed 映射为 bridge unavailable。
- 设计锚点：daemon 详设 6.3、6.6.2。

## 2. 实施结果

### 2.1 daemon 组合根接线

在 apps/daemon 组合根中为 daemon pipeline 注入 runtime dispatch backend：

1. 新增 `AgentFacade` 实例并注入 `runtime_dispatch_backend`。
2. backend adapter lambda 将 `RuntimeDispatchRequest` 投影为 `contracts::AgentRequest`。
3. backend 调用 `AgentFacade::handle()` 执行 unary 路径。
4. 当 runtime 返回未初始化失败语义时，映射为 `runtime_bridge_unavailable` 并返回 fail-closed Rejected。

### 2.2 集成测试补齐

新增 `DaemonUnaryRuntimeBridgeTest`，覆盖：

1. happy path：初始化后的 `AgentFacade` 经 adapter lambda 处理请求并返回 Completed。
2. fail-closed：未初始化 `AgentFacade` 返回 Rejected，且 `error_ref=runtime_bridge_unavailable`。

## 3. 修改文件

1. apps/daemon/src/main.cpp
2. tests/integration/access/DaemonUnaryRuntimeBridgeTest.cpp
3. tests/integration/access/CMakeLists.txt
4. docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 4. 验证证据

执行命令：

```bash
cmake --build build-ci --target dasall_access_runtime_bridge_unit_test dasall_access_runtime_bridge_reject_mapping_unit_test dasall_access_daemon_unary_runtime_bridge_integration_test
ctest --test-dir build-ci -R "RuntimeBridgeTest|RuntimeBridgeRejectMappingTest|DaemonUnaryRuntimeBridgeTest" --output-on-failure
```

结果：

- RuntimeBridgeTest：Passed
- RuntimeBridgeRejectMappingTest：Passed
- DaemonUnaryRuntimeBridgeTest：Passed

## 5. 约束符合性

1. 未直接调用 runtime 内部 orchestrator，仍通过 `AgentFacade::handle()` 接缝进入 runtime。
2. 未初始化 runtime 路径 fail-closed，不绕过 Access 主链。
3. 未引入 contracts 层对象越界或新增 daemon 第二主控语义。
