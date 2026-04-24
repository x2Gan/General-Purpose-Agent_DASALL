---
title: ACC-TODO-035 计划：Failure/observability/health/profile/contracts Gate
date: 2026-04-24
version: 0.5
phase: v1-access-testing
status: Blocked
---

## 1. 任务概要

创建 failure injection、observability、health endpoint、profile compatibility 与 contracts 的集成门。

**前置条件**：ACC-TODO-031/032/033/034（单元与集成基础）已完成

**阻塞项**：
- ACC-BLK-003：override/diagnostics schema 需正式结论（当前 Blocked）
- ACC-BLK-005：streaming 延后边界需确认（当前 Blocked）

---

## 2. 交付内容清单

### 2.1 待建立的集成测试

| 测试 | 目标 | 依赖 |
|---|---|---|
| AccessAdmissionFailureIntegrationTest | Admission 超限/冲突时系统正确拒绝 | AdmissionController mock |
| AccessObservabilityIntegrationTest | Auth failed / policy denied / publish failed 事件正确发出 | AccessObservabilityBridge |
| AccessHealthProbeIntegrationTest | /health/live、/health/ready、/health/startup 返回正确状态 | HealthProbeHandler |
| AccessProfileCompatibilityTest | 不同 profile 下 access 行为一致 | profile loader |

### 2.2 所需工作

1. **新建** `tests/integration/access/AccessAdmissionFailureIntegrationTest.cpp`
2. **新建** `tests/integration/access/AccessObservabilityIntegrationTest.cpp`
3. **新建** `tests/integration/access/AccessHealthProbeIntegrationTest.cpp`
4. **新建** `tests/integration/access/AccessProfileCompatibilityTest.cpp`
5. **确认** contracts guard 兼容性（已有 AgentRequestContractTest）
6. **标注** streaming/override/diagnostics 部分为 Not Ready（ACC-GATE-11）

### 2.3 验收命令

```bash
cmake --build build-ci --target dasall_contract_tests dasall_integration_tests

ctest --test-dir build-ci -R \
  "AccessAdmissionFailureIntegrationTest|\
AccessObservabilityIntegrationTest|\
AccessHealthProbeIntegrationTest|\
AccessProfileCompatibilityTest|\
AgentRequestContractTest|IdentityMetadataContractTest" \
  --output-on-failure
```

---

## 3. 实现路线

- **Phase A**：创建 failure injection mock
- **Phase B**：实现四个集成测试
- **Phase C**：运行 contracts guard 验证
- **Phase D**：标注延后 Gate

---

## 4. 风险与已知限制

- 当前 ACC-BLK-003/005 仍 Blocked，override/diagnostics/streaming 部分只能标记为 Not Ready
- Health probe 集成需要 HTTP mock 或 daemon 真实运行
- Observability integration 需要 event sink mock
- Profile compatibility 需要 profile loader 支持

---

**签署**：任务编号 ACC-TODO-035 | 状态 Blocked | 解阻条件：034 完成 + ACC-BLK-003/005 结论
