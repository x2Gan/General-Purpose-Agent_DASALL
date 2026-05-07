---
title: ACC-TODO-034 计划：CLI/daemon unary smoke + async receipt 集成门
date: 2026-04-24
version: 0.5
phase: v1-access-testing
status: Blocked
---

## 1. 任务概要

创建 CLI→daemon 和 async receipt 的首版集成测试，验证端到端链路。

**前置条件**：ACC-TODO-031/032/033（单元门）已完成 ✅

**阻塞项**：
- 集成测试框架需建立 `tests/integration/access/CMakeLists.txt`
- CLI/daemon 入口二进制需已编译可用
- Mock runtime dispatcher 需提供

---

## 2. 交付内容清单

### 2.1 待建立的集成测试

| 测试 | 目标 | 依赖 |
|---|---|---|
| CliDaemonSmokeIntegrationTest | CLI 通过 UDS 连接 daemon，发送 unary 请求，收到 response | dasall-cli, dasall-daemon |
| AccessAsyncReceiptIntegrationTest | Unary 返回 202 accepted，query 返回 receipt，cancel 生效 | dasall-daemon, AsyncTaskRegistry |

### 2.2 所需工作

1. **创建** `tests/integration/access/CMakeLists.txt`
2. **新建** `tests/integration/access/CliDaemonSmokeIntegrationTest.cpp`
3. **新建** `tests/integration/access/AccessAsyncReceiptIntegrationTest.cpp`
4. **更新** `tests/integration/CMakeLists.txt` 注册子目录

### 2.3 验收命令

```bash
cmake --build build-ci --target dasall_integration_tests
ctest --test-dir build-ci -N | grep -i CliDaemonSmokeIntegrationTest
ctest --test-dir build-ci -R "CliDaemonSmokeIntegrationTest|AccessAsyncReceiptIntegrationTest" --output-on-failure
```

---

## 3. 实现路线

- **Phase A**：创建 CMake 骨架
- **Phase B**：实现 CliDaemonSmokeIntegrationTest
- **Phase C**：实现 AccessAsyncReceiptIntegrationTest  
- **Phase D**：验证端到端链路

---

## 4. 风险

- daemon 与 CLI 进程通信需 IPC 管理与超时控制
- 集成测试耗时较长，需设置合理的 test timeout
- Mock runtime 需精确模拟异步状态与回执

---

**签署**：任务编号 ACC-TODO-034 | 状态 Blocked | 解阻条件：单元门 031-033 完成（✅）+ 集成框架建立
