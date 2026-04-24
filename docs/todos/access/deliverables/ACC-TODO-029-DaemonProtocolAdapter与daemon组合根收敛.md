# ACC-TODO-029 设计收敛文档

## 任务元数据

- **任务 ID**：ACC-TODO-029
- **任务标题**：实现 DaemonProtocolAdapter 与 apps/daemon 本地控制面组合根
- **设计锚点**：daemon 本地控制面详设 1.2、6.1、6.2；Access 详设 6.14.9、6.15.3、8.1
- **前置依赖**：013(Done), 014(Done), 015(Done), 017(Done), 019(Done), 024(Done), 037(Done)
- **阻塞项**：ACC-BLK-007(已解阻), ACC-BLK-006(v1 单实例允许)

---

## 1. 边界与职责

### 1.1 DaemonProtocolAdapter 职责

| 职责 | 说明 |
|---|---|
| IProtocolAdapter 实现 | 实现 `can_handle("daemon", "ipc_uds")`、`decode()`、`encode()` |
| peer identity 接缝 | 持有 `describe_local_peer_uid_fact()` 以提供本地 trusted 判定事实 |
| 协议解析 | 将 UDS 上收到的 JSON payload 解析为 `InboundPacket` |
| 响应编码 | 将 `PublishEnvelope` 序列化为 JSON 并经 UDS 发回 |
| 无状态主体 | 每次 `set_active_channel()` 建立单次连接上下文；不保存跨连接状态 |

### 1.2 DaemonBootstrap 职责

| 职责 | 说明 |
|---|---|
| 服务端监听 | 在 UDS 端点上调用 `IIPC::listen()` 并循环 `accept()` |
| 单连接事务 | 对每个接受的连接：receive → peer identity → decode → AccessGateway.submit → encode |
| 信任注入 | 将 `LocalPeerUidFact` 注入 `InboundPacket.peer_ref` 以驱动后续 SubjectResolver |
| 优雅关闭 | 接收 stop 信号后退出 accept 循环，调用 `AccessGateway::shutdown()` |

### 1.3 apps/daemon main.cpp 职责

- 纯组合根：构造所有依赖（IIPC provider、AccessGateway、DaemonBootstrap）
- 装配 `AccessGateway` 组合（inject SubmitPipeline 和 PublishBackend）
- 调用 `DaemonBootstrap::run()` 进入事件循环

### 1.4 硬边界约束

- Daemon 是 Access 主链的本地 owner，不是 runtime 的替代品
- 所有请求经 AccessGateway（即经过 Admission / Policy / Normalizer / RuntimeBridge）
- Local trusted 判定只在 peer uid ≠ 0 且 is_local_socket_peer 为 true 时生效
- 单线程同步模型（v1）：每次只处理一个连接；多连接并发延后

---

## 2. 数据结构与接口

### 2.1 DaemonProtocolAdapter 扩展接口

```cpp
class DaemonProtocolAdapter : public access::IProtocolAdapter {
 public:
  explicit DaemonProtocolAdapter(std::shared_ptr<platform::IIPC> ipc);

  // IProtocolAdapter 实现
  bool can_handle(std::string_view entry_type, std::string_view protocol_kind) const override;
  InboundPacket decode() override;
  bool encode(const PublishEnvelope& envelope) override;

  // peer identity 接缝（复用 037 成果）
  LocalPeerUidFact describe_local_peer_uid_fact(
      const platform::IpcChannelHandle& handle, std::string actor_ref) const;

  // 连接上下文注入（每次 accept 后调用）
  void set_active_channel(platform::IpcChannelHandle channel, std::vector<uint8_t> payload);
};
```

### 2.2 DaemonBootstrap 接口

```cpp
class DaemonBootstrap {
 public:
  DaemonBootstrap(std::shared_ptr<platform::IIPC> ipc,
                  std::shared_ptr<access::AccessGateway> gateway);

  bool run(const platform::IpcEndpoint& endpoint);
  void stop();

 private:
  bool handle_connection(const platform::IpcChannelHandle& channel);
};
```

### 2.3 JSON 协议规约（v1 最小集）

**CLI -> Daemon 请求**：
```json
{"op":"ping"}
{"op":"submit","entry_type":"daemon","protocol_kind":"ipc_uds","payload":"...","peer_ref":"cli","async_preferred":false}
```

**Daemon -> CLI 响应**：
```json
{"status":"ok"}
{"status":"accepted","disposition":"completed","result":"..."}
{"status":"rejected","reason":"..."}
```

---

## 3. 流程/时序

```
CLI                    DaemonBootstrap         DaemonProtocolAdapter  AccessGateway
 |                           |                          |                    |
 |--connect(UDS)------------>|                          |                    |
 |                           |--accept()--------------->|                    |
 |--send(JSON payload)------>|--receive()               |                    |
 |                           |--describe_peer()-------->|                    |
 |                           |--set_active_channel()--->|                    |
 |                           |--decode()--------------->|                    |
 |                           |<--InboundPacket----------|                    |
 |                           |--submit(InboundPacket)---------------------------->|
 |                           |<--RuntimeDispatchResult-----------------------------|
 |                           |--encode(PublishEnvelope)-->|                    |
 |<--JSON response-----------|                          |                    |
```

---

## 4. 文件范围

| 文件 | 操作 | 说明 |
|---|---|---|
| `access/include/daemon/DaemonProtocolAdapter.h` | 扩展 | 增加 IProtocolAdapter 继承和新方法 |
| `access/src/daemon/DaemonProtocolAdapter.cpp` | 扩展 | 实现 can_handle/decode/encode/set_active_channel |
| `apps/daemon/src/DaemonBootstrap.h` | 新增 | Daemon 服务端生命周期头文件 |
| `apps/daemon/src/DaemonBootstrap.cpp` | 新增 | Daemon 服务端事件循环实现 |
| `apps/daemon/src/main.cpp` | 更新 | 组合根装配所有依赖 |
| `apps/daemon/CMakeLists.txt` | 更新 | 增加源文件和 dasall_platform 依赖 |
| `tests/unit/access/DaemonProtocolAdapterTest.cpp` | 新增 | 基础 adapter 功能测试 |
| `tests/unit/access/CMakeLists.txt` | 更新 | 注册新测试目标 |

---

## 5. 验收命令

```bash
cmake --build /home/gangan/DASALL/build-ci --target dasall_daemon \
  dasall_access_daemon_protocol_adapter_unit_test \
  dasall_access_daemon_protocol_adapter_local_trusted_unit_test \
  && ctest --test-dir /home/gangan/DASALL/build-ci \
  -R "DaemonProtocolAdapterTest|DaemonProtocolAdapterLocalTrustedTest" \
  --output-on-failure
```

---

## 6. 验收结果（Done）

- **构建**：`dasall_daemon`、`dasall_access_daemon_protocol_adapter_unit_test`、`dasall_access_daemon_protocol_adapter_local_trusted_unit_test` 全部通过 ✅
- **测试**：2/2 通过（`DaemonProtocolAdapterTest`、`DaemonProtocolAdapterLocalTrustedTest`）✅
- **完成时间**：2025-07-10
- **状态**：Done
