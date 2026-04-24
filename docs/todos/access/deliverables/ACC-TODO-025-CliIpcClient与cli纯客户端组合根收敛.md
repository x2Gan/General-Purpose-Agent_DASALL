# ACC-TODO-025 设计收敛文档

## 任务元数据

- **任务 ID**：ACC-TODO-025
- **任务标题**：实现 CliCommandParser、CliOutputFormatter 与 apps/cli 纯客户端组合根
- **设计锚点**：CLI 本地控制面详设 1.2、6.2、6.4；Access 详设 6.14.8、8.1
- **前置依赖**：013(Done), 018(Done), 024(Done), 038(Done)
- **阻塞项**：无

---

## 1. 边界与职责

### 1.1 CLI 组件职责分工

| 组件 | 职责 |
|---|---|
| `CliCommandParser` | 纯函数式 CLI 参数解析；不依赖 I/O；支持 `ping` / `submit` |
| `CliIpcClient` | 通过 IIPC/UDS 向 daemon 发送请求；fail-closed 设计 |
| `CliOutputFormatter` | 纯字符串变换，将原始响应格式化为人类可读文本 |
| `main.cpp` | 组合根：parse → client → format → print → exit code |

### 1.2 架构约束

- CLI 为**纯客户端**：不 direct link runtime，不持有 AccessGateway
- 所有请求经 IIPC/UDS 进入 daemon → Access 主链（ADR-007/008 边界）
- fail-closed：连接失败 / 解析失败均以非零退出码退出
- `CliCommandParser` 和 `CliOutputFormatter` 零 I/O 依赖，可独立单测

---

## 2. 设计决策

### 2.1 命令集（v1）

| 命令 | 格式 | 说明 |
|---|---|---|
| `ping` | `dasall_cli ping` | 向 daemon 发送健康检查请求 |
| `submit` | `dasall_cli submit <json>` | 向 daemon 提交 InboundPacket JSON |

### 2.2 fail-closed 行为

| 场景 | 行为 |
|---|---|
| 参数解析失败 | 打印 usage，exit(1) |
| daemon 连接失败 | 打印 failure 消息，exit(1) |
| deadline=0 超时 | 连接直接返回 failure |
| socket 路径为空 | `endpoint.has_consistent_values()` 返回 false，直接 fail |

### 2.3 UnixIpcProvider Stub 说明

测试使用 `UnixIpcProvider`（内存 stub）：
- `connect(deadline=0)` → 返回 timeout failure（模拟 daemon 不可达）
- `connect(valid_endpoint, deadline>0)` → 总是成功（stub 不做真实 socket）

---

## 3. 文件范围

| 文件 | 操作 | 说明 |
|---|---|---|
| `apps/cli/src/CliCommandParser.h` | 新增 | 命令解析器头文件 |
| `apps/cli/src/CliCommandParser.cpp` | 新增 | 命令解析器实现 |
| `apps/cli/src/CliOutputFormatter.h` | 新增 | 输出格式化头文件 |
| `apps/cli/src/CliOutputFormatter.cpp` | 新增 | 输出格式化实现 |
| `apps/cli/src/main.cpp` | 更新 | 使用 CliCommandParser + CliOutputFormatter |
| `apps/cli/CMakeLists.txt` | 更新 | 增加新源文件 |
| `tests/unit/access/CliIpcClientUnavailableTest.cpp` | 新增 | fail-closed 行为测试 |
| `tests/unit/access/CliOutputFormatterTest.cpp` | 新增 | 格式化函数测试 |
| `tests/unit/access/CMakeLists.txt` | 更新 | 注册两个新测试目标 |

---

## 4. 验收命令

```bash
cmake --build /home/gangan/DASALL/build-ci --target dasall_cli \
  dasall_access_cli_ipc_client_unit_test \
  dasall_access_cli_ipc_client_unavailable_unit_test \
  dasall_access_cli_output_formatter_unit_test \
  && ctest --test-dir /home/gangan/DASALL/build-ci \
  -R "CliIpcClientTest|CliIpcClientUnavailableTest|CliOutputFormatterTest" \
  --output-on-failure
```

---

## 5. 验收结果（Done）

- **构建**：`dasall_cli`、`dasall_access_cli_ipc_client_unit_test`、`dasall_access_cli_ipc_client_unavailable_unit_test`、`dasall_access_cli_output_formatter_unit_test` 全部通过 ✅
- **测试**：3/3 通过（`CliIpcClientTest`、`CliIpcClientUnavailableTest`、`CliOutputFormatterTest`）✅
- **完成时间**：2025-07-10
- **状态**：Done
