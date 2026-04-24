# ACC-TODO-021 设计收敛文档

## 1. 任务定义

实现 ResultPublisher 与 ProtocolErrorMapper 协作路径，确保 `AgentResult` 到协议响应语义映射稳定，并在发布通道失败时显式返回 `PublishChannelUnavailable`。

本任务范围：

1. 落盘 `access/src/ResultPublisher.h` 与 `access/src/ResultPublisher.cpp`。
2. 扩展 `access/src/ProtocolErrorMapper.cpp`，新增 `AgentResult` 到协议矩阵映射入口。
3. 更新 `access/include/AccessTypes.h`，让 `PublishEnvelope` 可携带 `AgentResult` 事实源。
4. 新增 `ResultPublisherTest`、`ProtocolErrorMapperTest`、`ResultPublisherChannelFailureTest`。
5. 回写 TODO 与 worklog，并完成单任务提交推送。

## 2. 边界与职责

### 2.1 职责

1. `ResultPublisher` 负责构造 `PublishEnvelope`、映射协议状态、执行发布发送。
2. `ProtocolErrorMapper` 负责 `AccessErrorCode` 与 `AgentResultStatus` 的协议语义映射。
3. 发布失败时返回可审计错误，不吞掉业务结果或静默降级。

### 2.2 非职责

1. 不负责任务调度与 runtime 执行。
2. 不负责认证、授权、admission。
3. 不修改 `AgentResult` shared 契约定义。
4. 不引入 WS/MQTT streaming 语义扩张。

## 3. 本地证据与外部参考

### 3.1 本地证据

1. Access 详设 6.8 要求发布链路以 `AgentResult` 为事实源。
2. Access 详设 6.10/6.17 要求 protocol mapping 稳定，channel unavailable 必须可观测。
3. ACC-TODO-007 已冻结 `AccessErrorCode -> Protocol` 矩阵，本任务在其上扩展 `AgentResultStatus -> Protocol` 映射。

### 3.2 外部参考

1. API 发布语义实践：业务状态映射与通道错误应分离，避免将传输失败误写成业务成功或反之。

## 4. 数据与接口说明

### 4.1 新增/扩展结构

1. `PublishEnvelope.agent_result`
   - 类型：`std::optional<contracts::AgentResult>`
   - 语义：发布链路显式携带 shared 事实源。

2. `PublishAttemptResult`
   - 字段：`published`、`envelope`、`error`
   - 语义：统一表达发布尝试结果。

### 4.2 核心接口

1. `ResultPublisher::build_envelope(...)`
2. `ResultPublisher::map_protocol_status(...)`
3. `ResultPublisher::emit_publish(...)`
4. `ResultPublisher::publish(...)`
5. `map_agent_result_to_protocol(const AgentResult&)`

## 5. 流程/时序

1. `build_envelope()` 从 request sidecar 与 `AgentResult` 组装统一发布 envelope。
2. `map_protocol_status()` 调用 `map_agent_result_to_protocol()` 生成协议状态映射。
3. `emit_publish()` 调用可注入 backend 发送 envelope。
4. `publish()` 在发送失败时返回 `PublishChannelUnavailable` 显式错误。

## 6. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| ResultPublisher 组件实现 | `access/src/ResultPublisher.h`、`access/src/ResultPublisher.cpp` |
| AgentResult 协议映射扩展 | `access/src/ProtocolErrorMapper.h`、`access/src/ProtocolErrorMapper.cpp` |
| PublishEnvelope 事实源增强 | `access/include/AccessTypes.h` |
| 发布成功路径 | `tests/unit/access/ResultPublisherTest.cpp` |
| 协议映射路径 | `tests/unit/access/ProtocolErrorMapperTest.cpp` |
| 通道失败路径 | `tests/unit/access/ResultPublisherChannelFailureTest.cpp` |
| 测试注册 | `tests/unit/access/CMakeLists.txt` |

## 7. 文件范围

1. `access/include/AccessTypes.h`
2. `access/src/ProtocolErrorMapper.h`
3. `access/src/ProtocolErrorMapper.cpp`
4. `access/src/ResultPublisher.h`
5. `access/src/ResultPublisher.cpp`
6. `access/CMakeLists.txt`
7. `tests/unit/access/ResultPublisherTest.cpp`
8. `tests/unit/access/ProtocolErrorMapperTest.cpp`
9. `tests/unit/access/ResultPublisherChannelFailureTest.cpp`
10. `tests/unit/access/CMakeLists.txt`
11. `docs/todos/access/DASALL_access子系统专项TODO.md`
12. 本文档

## 8. 验收三件套

### 8.1 代码目标

1. 实现 ResultPublisher 三个核心函数与 publish 封装。
2. 实现 `map_agent_result_to_protocol` 映射入口。
3. 确保 channel failure 显式映射到 `PublishChannelUnavailable`。

### 8.2 测试目标

1. `ResultPublisherTest`
2. `ProtocolErrorMapperTest`
3. `ResultPublisherChannelFailureTest`

### 8.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_access_result_publisher_unit_test \
  dasall_access_protocol_error_mapper_unit_test \
  dasall_access_result_publisher_channel_failure_unit_test && \
ctest --test-dir build/vscode-linux-ninja -R "ResultPublisherTest|ProtocolErrorMapperTest|ResultPublisherChannelFailureTest" --output-on-failure
```

## 9. 风险与回退

1. 当前 `AgentResultStatus::PartiallyCompleted` 映射为 202 Accepted 语义；若后续协议标准细化，需要保持向后兼容并补回归测试。
2. `PublishEnvelope.agent_result` 为可选字段，后续若链路未填充需保持 fail-closed 或显式错误，禁止静默丢失事实源。
3. 发布 backend 当前通过函数注入；接真实通道适配器时需保留错误码映射一致性。
