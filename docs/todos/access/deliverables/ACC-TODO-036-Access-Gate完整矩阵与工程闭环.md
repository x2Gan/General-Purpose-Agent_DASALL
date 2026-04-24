---
title: ACC-TODO-036 交付物：Access 子系统完整 Gate 与工程闭环
date: 2026-04-24
version: 0.5
phase: v1-access-testing
status: Blocked
---

## 1. 任务概要

汇聚前 5 个任务的单元/集成/contracts 门定义，输出 Access 子系统的完整质量门矩阵与 release checklist。

**前置条件**：ACC-TODO-031/032/033/034/035（所有单元与集成门）完成

---

## 2. 完整 Gate 矩阵（待汇聚）

### 2.1 单元门（Unit Gates）

| 门 | 来源 | 验证内容 | 状态 |
|---|---|---|---|
| Interface Surface Gate | ACC-TODO-031 | 接口枚举与方法 | ✅ Done |
| Lifecycle Gate | ACC-TODO-031 | 状态机 | ✅ Done |
| Registry Gate | ACC-TODO-031 | 适配器注册 | ✅ Done |
| Subject Resolution Gate | ACC-TODO-032 | Subject 提取与验证 | ✅ Done |
| Authenticator Chain Gate | ACC-TODO-032 | 多源认证与降级 | ✅ Done |
| Policy Gate | ACC-TODO-032 | Policy decision | ✅ Done |
| Admission Control Gate | ACC-TODO-032 | 超限与冲突 | ✅ Done |
| Request Validator Gate | ACC-TODO-032 | Payload 限制与注入检测 | ✅ Done |
| Request Normalizer Gate | ACC-TODO-032 | AgentRequest 投影 | ✅ Done |
| Result Publisher Gate | ACC-TODO-032 | 发布与错误映射 | ✅ Done |
| Async Receipt Gate | ACC-TODO-033 | 异步回执生命周期 | ✅ Done |
| Ownership Validation Gate | ACC-TODO-033 | 回执所有权保护 | ✅ Done |
| Replay Cache Gate | ACC-TODO-033 | 缓存淘汰 | ✅ Done |
| Daemon Local Trusted Gate | ACC-TODO-033 | UDS peer identity | ✅ Done |
| Simulator Deterministic Gate | ACC-TODO-033 | 重放一致性 | ✅ Done |

**小计**：15 个单元门，全部完成 ✅

### 2.2 集成门（Integration Gates）

| 门 | 来源 | 验证内容 | 状态 |
|---|---|---|---|
| CLI-Daemon Unary Smoke Gate | ACC-TODO-034 | UDS 连接与单程请求 | ⏳ Pending |
| Async Receipt E2E Gate | ACC-TODO-034 | Unary→receipt→query→cancel 链路 | ⏳ Pending |
| Admission Failure Gate | ACC-TODO-035 | 故障注入与系统响应 | ⏳ Pending |
| Observability Gate | ACC-TODO-035 | 事件发出与兼容性 | ⏳ Pending |
| Health Probe Gate | ACC-TODO-035 | Health endpoint 正确性 | ⏳ Pending |
| Profile Compatibility Gate | ACC-TODO-035 | 多 profile 一致性 | ⏳ Pending |

**小计**：6 个集成门，待实现

### 2.3 Contracts Gate

| 门 | 来源 | 验证内容 | 状态 |
|---|---|---|---|
| AgentRequest Contract Gate | ACC-TODO-032 | 契约一致性 | ✅ Verified |
| IdentityMetadata Contract Gate | 前期完成 | 身份元数据结构 | ✅ Verified |

**小计**：2 个 contracts 门，已验证 ✅

### 2.4 延后 Gate（Not Ready）

| 门 | 原因 | 阻塞项 |
|---|---|---|
| Override/Diagnostics Gate | Schema 冻结延后 | ACC-BLK-003 |
| Streaming Receipt Gate | Streaming 链路延后设计 | ACC-BLK-005 |

---

## 3. 完整验收清单

```bash
# 单元测试汇聚验收（已完成）
cmake --build build-ci --target dasall_access_*_unit_test

ctest --test-dir build-ci -R \
  "AccessInterface|AccessGatewayLifecycle|ProtocolAdapterRegistry|\
SubjectResolver|Authenticator|AccessPolicy|AdmissionController|\
RequestValidator|RequestNormalizer|ResultPublisher|\
AsyncTaskRegistry|ResultReplayCache|AccessTaskQueryHandler|\
DaemonProtocolAdapter|SimulatorProtocolAdapter" \
  --output-on-failure

# Result: 22/22 tests passed ✅

# Contracts 验证（已完成）
ctest --test-dir build-ci -R "AgentRequest.*Contract|IdentityMetadata.*Contract" --output-on-failure

# Result: 2/2 contracts tests passed ✅

# 集成测试验收（待完成）
cmake --build build-ci --target dasall_integration_tests

ctest --test-dir build-ci -R \
  "CliDaemonSmokeIntegrationTest|\
AccessAsyncReceiptIntegrationTest|\
AccessAdmissionFailureIntegrationTest|\
AccessObservabilityIntegrationTest|\
AccessHealthProbeIntegrationTest|\
AccessProfileCompatibilityTest" \
  --output-on-failure

# Expected: 6/6 tests passed
```

---

## 4. Release Gate 检查表

### 4.1 已完成

- [x] 15 个单元门 100% 通过（ACC-TODO-031/032/033）
- [x] 2 个 contracts 门验证完成
- [x] 所有单元测试覆盖 7 阶段链路
- [x] 本地链路（daemon/simulator）验证
- [x] 异步回执所有权保护
- [x] 所有门定义文档化
- [x] 工作日志记录 (entries #462-#466)

### 4.2 待完成

- [ ] 6 个集成门实现与通过
- [ ] CLI-daemon 端到端验证
- [ ] Failure injection 与 observability
- [ ] Health probe 与 profile 兼容性
- [ ] ACC-BLK-003/005 阻塞项解决（override/streaming）

### 4.3 已知限制

- 不覆盖：Streaming receipt、override/diagnostics（ACC-GATE-11 标记）
- 不覆盖：并发压力测试、网络故障重试
- 不覆盖：Multi-tenant isolation（Profile 层验证）

---

## 5. 与依赖子系统的衔接

| 子系统 | 当前状态 | Access 依赖 |
|---|---|---|
| contracts (M1-M3) | ✅ Done | AgentRequest, IdentityMetadata |
| runtime (Phase 1) | 🔄 In Progress | RecoveryManager, AgentOrchestrator |
| knowledge | 🔄 In Progress | ContextMetadata |
| llm | 🔄 In Progress | PolicyEngine 对接 |

---

## 6. 工程闭环

### 6.1 交付清单

- ✅ ACC-TODO-031：Interface/lifecycle/registry 单元门（8 章文档）
- ✅ ACC-TODO-032：Admission/normalizer/publisher 核心单元门（9 章文档）
- ✅ ACC-TODO-033：Async receipt/ownership/cancel 本地链路门（8 章文档）
- ⏳ ACC-TODO-034：CLI/daemon 集成门计划（待实现）
- ⏳ ACC-TODO-035：Failure/health/contracts 集成门计划（待实现）
- ⏳ ACC-TODO-036：本文档

### 6.2 依赖与阻塞

| 任务 | 解阻条件 | 当前状态 |
|---|---|---|
| ACC-TODO-034 | 031-033 ✅ | 就绪，可立即启动 |
| ACC-TODO-035 | 034 ✅ + ACC-BLK-003/005 结论 | 待 034 完成 + blocker 处理 |
| ACC-TODO-036 | 034-035 ✅ | 待 034-035 完成 |

### 6.3 推荐执行顺序

```
ACC-TODO-034 (CLI/daemon integration)
    ↓
ACC-TODO-035 (Failure/observability/health)
    ↓
ACC-TODO-036 (Final Gate writeup)
    ↓
Access Phase 1 Release Candidate
```

---

## 7. 签署与交付

**任务编号**：ACC-TODO-036

**当前状态**：Blocked（待 034-035 完成）

**预期完成日期**：2026-04-25

**验收负责人**：DASALL Access Subsystem Lead

**交付物**：
- 本文档（完整 gate 矩阵与 release checklist）
- ACC-TODO-034 集成测试结果
- ACC-TODO-035 故障/可观测性门结果
- 最终工作日志（#467+）

---

**END OF ACC-TODO-036 PLAN DOCUMENT**
