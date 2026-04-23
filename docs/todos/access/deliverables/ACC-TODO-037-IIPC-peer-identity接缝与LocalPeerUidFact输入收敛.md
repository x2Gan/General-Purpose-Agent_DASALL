# ACC-TODO-037 设计收敛文档

## 1. 任务定义

补齐 IIPC peer identity 接缝并固化 LocalPeerUidFact 输入，形成 daemon 本地 trusted 判定的最小事实来源。

本任务范围：

1. 在 `platform/include/IIPC.h` 增加 peer identity 数据结构与接口。
2. 在 `platform/include/linux/UnixIpcProvider.h` 与 `platform/src/linux/UnixIpcProvider.cpp` 实现 `describe_peer()`。
3. 在 `access/include/AccessTypes.h` 增加 `LocalPeerUidFact`。
4. 新增 `access/src/daemon/DaemonProtocolAdapter.cpp` 作为接缝骨架，消费 `describe_peer()` 输出。
5. 新增两类单测：`UnixIpcProviderPeerIdentityTest`、`DaemonProtocolAdapterLocalTrustedTest`。

## 2. 边界与职责

### 2.1 边界

1. `IIPC::describe_peer()` 只返回连接对端身份事实，不做授权裁定。
2. `LocalPeerUidFact` 只承载本地 peer uid/gid/pid 事实，不进入 shared contracts。
3. `DaemonProtocolAdapter` 只做“事实转换 + 本地 trusted 资格判定输入”，不替代 Admission/Policy。

### 2.2 职责划分

| 组件 | 职责 | 非职责 |
|---|---|---|
| `IIPC` | 暴露 peer identity 查询接口 | 不做认证授权决策 |
| `UnixIpcProvider` | 提供 Linux/UDS 对端身份快照 | 不存储 Access 业务语义 |
| `DaemonProtocolAdapter` | 将平台事实投影为 `LocalPeerUidFact` | 不直接放行请求 |

## 3. 数据与接口说明

### 3.1 新增数据结构

1. `PeerIdentitySnapshot`
   - 字段：`peer_uid`、`peer_gid`、`peer_pid`、`is_local_socket_peer`。
   - 约束：`is_local_socket_peer=true` 时 uid/gid/pid 必须非零。

2. `LocalPeerUidFact`
   - 字段：`actor_ref`、`peer_uid`、`peer_gid`、`peer_pid`、`is_local_socket_peer`、`eligible_for_local_trusted`。
   - 用途：供 daemon 本地 trusted 入口判定链路使用。

### 3.2 接口变更

1. `IIPC` 新增：
   - `PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle& handle)`

## 4. 流程与时序

1. daemon 接收 IIPC channel handle。
2. `DaemonProtocolAdapter` 调用 `IIPC::describe_peer()` 拉取平台事实。
3. adapter 生成 `LocalPeerUidFact`。
4. 后续 Admission/Policy 依据该 fact 做受控判定（本任务不实现裁定逻辑）。

## 5. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| peer identity 接口扩展 | `platform/include/IIPC.h` |
| Linux provider describe_peer 实现 | `platform/include/linux/UnixIpcProvider.h`, `platform/src/linux/UnixIpcProvider.cpp` |
| local trusted 输入事实对象 | `access/include/AccessTypes.h` |
| daemon 接缝骨架 | `access/include/daemon/DaemonProtocolAdapter.h`, `access/src/daemon/DaemonProtocolAdapter.cpp` |
| 行为校验 | `tests/unit/platform/linux/UnixIpcProviderPeerIdentityTest.cpp`, `tests/unit/access/DaemonProtocolAdapterLocalTrustedTest.cpp` |

## 6. 文件范围

1. `platform/include/IIPC.h`
2. `platform/include/linux/UnixIpcProvider.h`
3. `platform/src/linux/UnixIpcProvider.cpp`
4. `access/include/AccessTypes.h`
5. `access/include/daemon/DaemonProtocolAdapter.h`
6. `access/src/daemon/DaemonProtocolAdapter.cpp`
7. `access/CMakeLists.txt`
8. `tests/unit/platform/linux/CMakeLists.txt`
9. `tests/unit/platform/linux/UnixIpcProviderPeerIdentityTest.cpp`
10. `tests/unit/access/CMakeLists.txt`
11. `tests/unit/access/DaemonProtocolAdapterLocalTrustedTest.cpp`

## 7. 验收三件套

### 7.1 代码目标

1. `IIPC` 暴露 peer identity 查询接口。
2. `UnixIpcProvider` 可返回稳定且一致的 peer identity。
3. `DaemonProtocolAdapter` 能生成 `LocalPeerUidFact` 并判断是否具备 local trusted 资格。

### 7.2 测试目标

1. `UnixIpcProviderPeerIdentityTest`：验证 `describe_peer()` 正常路径与错误路径。
2. `DaemonProtocolAdapterLocalTrustedTest`：验证 local trusted 正负例。

### 7.3 验收命令

```bash
cmake --build build-ci --target \
  dasall_unix_ipc_provider_peer_identity_unit_test \
  dasall_access_daemon_protocol_adapter_local_trusted_unit_test && \
ctest --test-dir build-ci -R "UnixIpcProviderPeerIdentityTest|DaemonProtocolAdapterLocalTrustedTest" --output-on-failure
```

说明：当前仓库全量 `dasall_unit_tests` 仍受 knowledge 既有编译问题影响，本任务使用定向目标验收。

## 8. 风险与回退

1. 后续若切换真实 `SO_PEERCRED`，应保持 `PeerIdentitySnapshot` 字段与一致性约束不变。
2. 若 peer identity 缺失，必须 fail-closed，不能推断 trusted。
3. 本任务只建立输入接缝，不宣称 daemon 本地控制面完整 ready。
