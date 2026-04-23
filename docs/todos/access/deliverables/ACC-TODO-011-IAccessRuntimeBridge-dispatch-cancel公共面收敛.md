# ACC-TODO-011 设计收敛文档

## 1. 任务定义

对齐 Access runtime bridge 公共面，确保 `IAccessRuntimeBridge` 明确提供：

1. `dispatch(const RuntimeDispatchRequest&) -> RuntimeDispatchResult`
2. `cancel(std::string_view request_id, std::string_view actor_ref) -> bool`

本任务范围是 public ABI 与接口可发现性验证，不进入 `RuntimeBridge` 实现细节。

---

## 2. 边界与职责

### 2.1 组件边界

| 对象 | 职责 | 边界规则 |
|---|---|---|
| `IAccessRuntimeBridge` | Access 到 Runtime 的唯一 module-local 调用面 | 不暴露 runtime 内部状态机与调度对象 |
| `RuntimeDispatchRequest` | dispatch 输入容器 | 保持 shared request + module-local sidecar 边界 |
| `RuntimeDispatchResult` | dispatch 返回容器 | 统一表达 Rejected/Completed/AcceptedAsync/StreamAttached |
| `cancel(request_id, actor_ref)` | 取消转发入口 | 只转发，不承担最终业务裁定 |

### 2.2 与相邻模块边界

1. Access 负责入口准入与主体归属校验；Runtime 负责执行期裁定。
2. `cancel()` 前置授权和 ownership 校验在 Access 链路完成；bridge 只负责转发。
3. `cancel()` 不引入新的 shared contracts 对象，维持 ACC-TODO-001 既有 seam 冻结。

---

## 3. 数据与接口说明

### 3.1 dispatch

```cpp
virtual RuntimeDispatchResult dispatch(const RuntimeDispatchRequest& request) = 0;
```

语义：

1. 接收 RequestNormalizer 生成的 `RuntimeDispatchRequest`。
2. 输出 `RuntimeDispatchResult`，由 bridge 负责把 runtime 返回映射为 AccessDisposition。
3. 不允许在接口层泄漏 runtime provider 私有类型。

### 3.2 cancel

```cpp
virtual bool cancel(std::string_view request_id, std::string_view actor_ref) = 0;
```

语义：

1. `request_id`：取消目标请求标识。
2. `actor_ref`：发起取消的主体锚点，用于 ownership / 审计关联。
3. 返回 `true` 表示转发请求已被 runtime seam 接受；`false` 表示转发失败或目标不可取消。
4. 返回值不等同于业务最终状态，最终结果仍由 runtime / query path 给出。

---

## 4. 流程与时序

### 4.1 dispatch 主路径

1. Access submit pipeline 产生 `RuntimeDispatchRequest`。
2. 调用 `IAccessRuntimeBridge::dispatch()`。
3. Bridge 转 runtime seam。
4. 返回 `RuntimeDispatchResult`，供 publisher/receipt 链路消费。

### 4.2 cancel 路径

1. 调用方携带 `request_id` 与主体信息。
2. Access 上游完成授权与 ownership 检查。
3. 调用 `IAccessRuntimeBridge::cancel(request_id, actor_ref)`。
4. Bridge 转发到 runtime cancel seam，返回布尔转发结果。

---

## 5. 文件范围

本任务落盘文件：

1. `access/include/IAccessRuntimeBridge.h`
2. `tests/unit/access/RuntimeBridgeSurfaceTest.cpp`
3. `tests/unit/access/AccessInterfaceSurfaceTest.cpp`
4. `tests/unit/access/CMakeLists.txt`
5. `docs/todos/access/DASALL_access子系统专项TODO.md`
6. 本文档：`docs/todos/access/deliverables/ACC-TODO-011-IAccessRuntimeBridge-dispatch-cancel公共面收敛.md`

---

## 6. 验收三件套

### 6.1 代码目标

1. `IAccessRuntimeBridge` 增加 `cancel(request_id, actor_ref)`。
2. 头文件补齐 `std::string_view` 所需 include。

### 6.2 测试目标

1. 新增 `RuntimeBridgeSurfaceTest`，验证 dispatch/cancel 方法存在性与签名可调用。
2. 更新 `AccessInterfaceSurfaceTest`，确保 bridge surface 在总接口可发现。

### 6.3 验收命令

```bash
cmake --build build-ci --target dasall_access_runtime_bridge_surface_unit_test && \
ctest --test-dir build-ci -R "RuntimeBridgeSurfaceTest|AccessInterfaceSurfaceTest" --output-on-failure
```

说明：由于仓库已知全量 `dasall_unit_tests` 受 knowledge 既有编译问题影响，本任务采用 Access 定向目标验证。

---

## 7. 风险与回退

1. 若后续 runtime seam 改为结构化取消对象，优先在 bridge 实现层做适配，不修改本任务已冻结 public ABI。
2. 若调用方误把 `cancel=true` 解读为业务已终止，需在上层 query/publish 文档中明确“仅表示转发受理”。
3. 若未来确需 richer cancel status，应作为后续原子任务新增类型，不在本任务内扩边界。
