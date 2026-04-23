# ACC-TODO-010 设计收敛文档

## 1. 任务定义

定义 Access Admission 公共面：

1. `IAdmissionController`：统一表达准入判定、inflight 票据释放与完成回写。
2. `AccessAdmissionResult`：统一表达 admit/replay/conflict/reject/challenge 结果。

本任务只收敛公共接口与 supporting type，不引入 `access/src` 具体实现。

---

## 2. 边界与职责

### 2.1 所有权边界

| 对象 | 所有者 | 边界规则 |
|---|---|---|
| `IAdmissionController` | access module public ABI | 只定义语义，不承载存储实现 |
| `AccessAdmissionResult` | access module-local/public supporting type | 不进入 contracts；仅用于 access admission 阶段 |
| inflight ticket | AdmissionController 实现（后续任务） | 生命周期由 `admit -> release_ticket/record_completion` 驱动 |

### 2.2 与其他子系统边界

1. 不接管 Runtime 调度、恢复与预算裁定（ADR-008 保持）。
2. 不修改 `AgentRequest` / `AgentResult` frozen shared contracts。
3. 不在该接口层定义策略求值细节；策略语义由 `AccessPolicyGate` 先行给出证据，再由 Admission 结合并发/幂等做准入裁决。

---

## 3. 数据结构收敛

### 3.1 `AccessAdmissionResult`

建议最小字段：

```cpp
struct AccessAdmissionResult {
  bool admitted = false;
  bool replay_hit = false;
  bool conflict_hit = false;
  std::optional<std::string> ticket_ref;
  std::optional<std::string> replay_receipt_ref;
  std::optional<std::string> reject_reason;
  std::optional<std::string> challenge_hint;
};
```

字段语义：

1. `admitted`：是否允许进入 RuntimeBridge。
2. `replay_hit`：幂等窗口内命中已有结果或受理记录。
3. `conflict_hit`：幂等键冲突或并发冲突（与 replay 区分）。
4. `ticket_ref`：inflight 票据引用，供后续 release/record_completion。
5. `replay_receipt_ref`：命中 replay 时返回可追踪回执引用。
6. `reject_reason`：拒绝原因（busy/conflict/authz/policy/backend_unavailable 等）。
7. `challenge_hint`：认证补全提示（例如需要 re-auth/challenge）。

---

## 4. 接口语义收敛

### 4.1 `IAdmissionController`

```cpp
class IAdmissionController {
 public:
  virtual ~IAdmissionController() = default;

  virtual AccessAdmissionResult admit(
      const RuntimeDispatchRequest& request) = 0;

  virtual void release_ticket(const std::string& ticket_ref) = 0;

  virtual void record_completion(
      const std::string& ticket_ref,
      const RuntimeDispatchResult& result) = 0;
};
```

语义约束：

1. `admit()`：只做 admission 判定，不触发 publish，不直接执行 runtime 调用。
2. `release_ticket()`：请求在未进入 runtime 或提前失败时释放 inflight 占用。
3. `record_completion()`：请求生命周期完成后回写 admission 账本（用于统计、幂等窗口、回放索引）。

---

## 5. 流程/时序

### 5.1 主路径（admit）

1. 输入：`RuntimeDispatchRequest`。
2. 输出：`AccessAdmissionResult`。
3. 行为：
   - 通过：`admitted=true`，产出 `ticket_ref`。
   - replay：`admitted=false`，`replay_hit=true`，携带 `replay_receipt_ref`。
   - conflict/reject：`admitted=false`，`conflict_hit=true` 或 `reject_reason`。

### 5.2 生命周期闭环

1. submit pipeline 调用 `admit()`。
2. 若后续链路在 dispatch 前失败，调用 `release_ticket(ticket_ref)`。
3. 若 dispatch 已完成（成功/失败/异步受理），调用 `record_completion(ticket_ref, result)`。

该闭环确保 inflight 计数与 admission 账本一致，不产生泄漏。

---

## 6. 文件范围与落盘清单

本任务文件范围：

1. `access/include/AccessTypes.h`：新增 `AccessAdmissionResult`。
2. `access/include/IAdmissionController.h`：定义接口 surface。
3. `tests/unit/access/AdmissionControllerSurfaceTest.cpp`：新增公共面测试。
4. `tests/unit/access/AccessInterfaceSurfaceTest.cpp`：补 discoverability 校验。
5. `tests/unit/access/CMakeLists.txt`：注册新测试。
6. `docs/todos/access/DASALL_access子系统专项TODO.md`：任务状态回写。

---

## 7. 验收与证据

验收命令（按 TODO 约束）：

```bash
cmake --build build-ci --target dasall_unit_tests && \
ctest --test-dir build-ci -R "AdmissionControllerSurfaceTest" --output-on-failure
```

当前仓库已知存在 `tests/unit/knowledge/FreshnessControllerStalePolicyTest.cpp` 编译问题；因此本任务采用等价的 Access 定向构建与定向 ctest 作为可执行证据：

```bash
cmake --build build-ci --target dasall_access_admission_controller_surface_unit_test
ctest --test-dir build-ci -R "AdmissionControllerSurfaceTest" --output-on-failure
```

---

## 8. 与后续任务关系

1. 为 ACC-TODO-017（AdmissionController 实现）提供稳定 public ABI。
2. 为 ACC-TODO-024（AccessGateway facade）提供生命周期回调接口（release/record_completion）。
3. 为 ACC-TODO-032（端到端主链）提供 admit/replay/conflict 统一语义基线。
