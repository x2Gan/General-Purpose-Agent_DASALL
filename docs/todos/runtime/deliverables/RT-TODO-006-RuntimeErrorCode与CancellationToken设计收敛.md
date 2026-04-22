# RT-TODO-006 RuntimeErrorCode 与 CancellationToken 设计收敛

日期：2026-04-22  
任务：RT-TODO-006  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-006 定义为新增 `runtime/include/RuntimeErrorCode.h`、`runtime/include/CancellationToken.h` 及对应 unit tests，用来给 008/011/017 等后续任务提供共享公共面。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.15 已冻结 1xx~6xx 六个错误码段，且明确 `RuntimeErrorCode` 是 module-local 对象，不复用 infra 或 contracts 的错误枚举。
3. 同一设计文档的 6.16.2 已冻结 `CancellationToken` 的最小语义：每次请求创建一份 token，向 Scheduler 的 Worker Ticket 传播，支持手动取消与 deadline 触发，但不替代 `BudgetController` 的 `max_latency_ms` 检查。

## 2. 外部参考

1. Microsoft 的 `.NET CancellationToken` 文档强调 cancellation token 是 cooperative cancellation 通知对象，可以传播给多个线程或任务，`IsCancellationRequested` 在取消后对所有副本可见，且公开成员是线程安全的；这直接支持 runtime 使用可复制、共享状态的 token 对象，而不是 thread-local 标记。
2. gRPC status code 文档强调错误空间应使用“定义明确、范围固定、应用只消费已知值”的状态码集合；这支持 RuntimeErrorCode 使用稳定的 1xx~6xx 段分类，而不是在后续任务里临时散落裸整数。

## 3. 设计结论

1. `RuntimeErrorCode` 在本轮落为 `enum class`，枚举值直接使用 `RT_E_100_CONFIG_MISSING` 这类显式命名，保持与 6.15.1 的码段表一一对应。
2. `RuntimeErrorCode` 需要提供最小分类 helper，让调用方能按 1xx~6xx 六个段落判断所属域，并对越界值返回 `Unknown`，为单测和后续 telemetry/recovery 代码提供统一入口。
3. `CancellationToken` 在本轮落为 header-only 的 runtime-owned public type，要求可复制、跨线程共享取消状态、支持 `cancel()`、`is_cancelled()`、`bind_deadline()` 三个核心动作。
4. `CancellationToken::is_cancelled()` 的语义是“手动取消或 deadline 已到任一成立即为 true”；deadline 触发应被折叠为已取消状态，但 deadline 本身不替代预算扣减逻辑。
5. 本轮只建立公共面和单测，不提前把 `CancellationToken` 接进 Scheduler、BudgetController 或下游端口实现；真实传播链仍由 008/011/019/030 分任务落盘。

## 4. 文件范围

1. 本轮 Build 目标为 `runtime/include/RuntimeErrorCode.h`、`runtime/include/CancellationToken.h`、`tests/unit/runtime/RuntimeErrorCodeTest.cpp`、`tests/unit/runtime/CancellationTokenTest.cpp`。
2. 为了让聚合 `dasall_unit_tests` 能发现并构建这两个用例，本轮同时更新 `tests/unit/runtime/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt`。
3. `dasall_runtime` 生产源码在本轮不新增 `.cpp`；两类对象都保持 header-only，避免越界到 015/019 的控制器实现任务。

## 5. 流程 / 时序

1. 上游 runtime 入口在创建请求执行上下文时生成 `CancellationToken`。
2. 调度或 worker 组件复制 token 到各执行单元；任何副本观察到取消都应与源状态一致。
3. `bind_deadline()` 用于绑定 step/session deadline；deadline 到期后，`is_cancelled()` 必须稳定返回 true。
4. 运行态错误路径统一引用 `RuntimeErrorCode`；Telemetry、Recovery、Scheduler 等后续任务只消费该枚举和分类 helper，不再自行发明裸整数错误码。

## 6. Design -> Build 映射

| Design 项 | 本轮 Build 落点 |
|---|---|
| RT_E_1xx ~ RT_E_6xx 稳定错误域 | `runtime/include/RuntimeErrorCode.h` |
| 错误码段分类 helper | `runtime/include/RuntimeErrorCode.h`、`tests/unit/runtime/RuntimeErrorCodeTest.cpp` |
| cooperative cancellation + deadline 绑定 | `runtime/include/CancellationToken.h` |
| 跨线程可见性与超时触发断言 | `tests/unit/runtime/CancellationTokenTest.cpp` |
| unit discoverability 接线 | `tests/unit/runtime/CMakeLists.txt`、`tests/unit/CMakeLists.txt` |

## 7. Build 三件套

1. 代码目标：新增 `RuntimeErrorCode` 与 `CancellationToken` 两个 public headers，并接入对应的 runtime unit tests。
2. 测试目标：`RuntimeErrorCodeTest` 验证码段唯一性与越界分类；`CancellationTokenTest` 验证初始态、手动取消、deadline 取消和跨线程可见性。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles" && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -R "RuntimeErrorCodeTest|CancellationTokenTest" --output-on-failure`

## 8. 风险与回退

1. 如果本轮把 `RuntimeErrorCode` 直接做成 contracts 级共享枚举，会违反 runtime-local 边界，应回退到 `runtime/include/RuntimeErrorCode.h`。
2. 如果本轮把 `CancellationToken` 设计成一次性、不可复制的 thread-local 标记，后续 Worker Ticket 无法共享取消状态，应回退到共享状态模型。
3. 如果 `is_cancelled()` 只检查手动取消而不检查 deadline，后续 step timeout 语义会被静默丢失；如果反过来让 deadline 替代预算检查，则会越权侵入 BudgetController 责任。