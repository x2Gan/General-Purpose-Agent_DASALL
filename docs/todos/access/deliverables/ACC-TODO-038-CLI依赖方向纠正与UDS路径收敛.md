# ACC-TODO-038 设计收敛文档

## 1. 任务定义

纠正 `apps/cli` 依赖方向并固定 `CLI -> IIPC/UDS -> daemon` 路径，确保 CLI 不再 direct link runtime。

## 2. 边界与职责

### 2.1 边界

1. CLI 作为本地控制面客户端，仅负责命令封装、IPC 发起与结果呈现。
2. daemon 作为本地控制面服务端 owner，承接 access 主链。
3. CLI 不直接依赖 runtime 执行路径，不持有 runtime 主控语义。

### 2.2 职责划分

| 组件 | 职责 | 非职责 |
|---|---|---|
| `CliIpcClient` | 连接 UDS endpoint、发送请求载荷 | 不做 Admission/Policy 判定 |
| `apps/cli main` | 组装 client 与基础参数 | 不直连 runtime |
| `apps/daemon` | 服务端入口（本任务仅作链路目标） | 不在本任务扩展业务处理 |

## 3. 数据与接口说明

### 3.1 CliIpcClient 接口

1. 构造参数：`std::shared_ptr<IIPC>` + `IpcEndpoint`。
2. `ping_daemon()`：发送最小 ping 载荷，用于连通性检查。
3. `send_payload(std::string_view payload)`：发送任意请求载荷，返回发送成功与否。

### 3.2 依赖方向约束

1. `apps/cli/CMakeLists.txt` 移除 `dasall_runtime` 直连依赖。
2. CLI 最小依赖为：`dasall_platform`（IPC）、`dasall_contracts`、`dasall_infra`、`dasall_access`。

## 4. 流程与时序

1. CLI 启动后构建 `CliIpcClient`。
2. client 通过 `IIPC::connect()` 建立 UDS channel。
3. client 通过 `IIPC::send()` 发起 ping/request payload。
4. daemon 侧消费该路径（本任务只收敛客户端路径，不实现 daemon 处理逻辑）。

## 5. Design -> Build 映射

| 设计项 | Build 落点 |
|---|---|
| CLI UDS client 骨架 | `apps/cli/src/CliIpcClient.h`, `apps/cli/src/CliIpcClient.cpp` |
| CLI 入口接线 | `apps/cli/src/main.cpp` |
| CLI 依赖纠偏 | `apps/cli/CMakeLists.txt` |
| unit 测试 | `tests/unit/access/CliIpcClientTest.cpp`, `tests/unit/access/CMakeLists.txt` |
| integration 测试 | `tests/integration/access/CliDaemonPingIntegrationTest.cpp`, `tests/integration/access/CMakeLists.txt` |

## 6. 文件范围

1. `apps/cli/CMakeLists.txt`
2. `apps/cli/src/CliIpcClient.h`
3. `apps/cli/src/CliIpcClient.cpp`
4. `apps/cli/src/main.cpp`
5. `tests/unit/access/CMakeLists.txt`
6. `tests/unit/access/CliIpcClientTest.cpp`
7. `tests/integration/access/CMakeLists.txt`
8. `tests/integration/access/CliDaemonPingIntegrationTest.cpp`

## 7. 验收三件套

### 7.1 代码目标

1. CLI 不再 direct link runtime。
2. CLI 具备基于 IIPC/UDS 的最小 client 能力。

### 7.2 测试目标

1. `CliIpcClientTest`：验证 UDS payload 发送成功与失败分支。
2. `CliDaemonPingIntegrationTest`：验证 CLI 到 daemon 路径的最小 ping 连通骨架。

### 7.3 验收命令

```bash
cmake --build build-ci --target \
  dasall-cli \
  dasall-daemon \
  dasall_access_cli_ipc_client_unit_test \
  dasall_access_cli_daemon_ping_integration_test && \
ctest --test-dir build-ci -R "CliIpcClientTest|CliDaemonPingIntegrationTest" --output-on-failure
```

说明：当前仓库全量 `dasall_unit_tests` 仍受 knowledge 既有编译问题影响，本任务使用定向目标验收。

## 8. 风险与回退

1. 若后续引入真实 daemon 协议，保留 `CliIpcClient` 接口稳定，逐步扩展 payload 语义。
2. 若 UDS endpoint 不可用，CLI 必须 fail-closed 返回失败，不能隐式回退到 runtime 直连。
3. 本任务为公共接口与骨架收敛，不宣称 daemon 业务处理链 ready。
