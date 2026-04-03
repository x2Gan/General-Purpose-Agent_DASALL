# LOG-TODO-010 LoggingErrors 设计收敛

日期：2026-04-03  
任务：LOG-TODO-010  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](/home/gangan/DASALL/docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 的 LOG-TODO-010 明确要求冻结四个 `LOG_E_*` 错误码，并指出当前 blocker 是“与 contracts 映射矩阵未成文”。
2. [docs/architecture/DASALL_infra_logging模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_logging模块详细设计.md) 6.6 已显式列出 `LOG_E_QUEUE_FULL`、`LOG_E_SINK_IO`、`LOG_E_FORMAT_INVALID`、`LOG_E_CONFIG_INVALID` 四个错误语义，并要求“成功写入或返回可判定失败码”。
3. [docs/architecture/DASALL_infra_logging模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_logging模块详细设计.md) 6.8 已把 queue full、sink IO failure、format failure 作为 logging 异常主类，说明 010 的核心任务是把这些失败类别收敛成可测试、可映射的私有错误码域。
4. [infra/include/metrics/MetricsErrors.h](/home/gangan/DASALL/infra/include/metrics/MetricsErrors.h)、[infra/include/plugin/PluginErrorCode.h](/home/gangan/DASALL/infra/include/plugin/PluginErrorCode.h) 与相关 unit/contract 测试已经给出仓库既有模式：子域私有 enum + `*_error_code_name()` + `map_*_error_code()`，并显式限制映射只落入 contracts 已冻结的 `ResultCode` 集合。

## 2. 外部参考

1. spdlog Asynchronous logging 文档明确队列满策略只有 `block` 与 `overrun_oldest` 两条主路径，说明 queue saturation 应被建模为稳定、可断言的错误语义，而不是临时字符串消息。
2. Microsoft Azure Retry pattern 指出瞬时依赖失败应记录、分级处理，并在重试耗尽后进入显式失败或降级路径，支持本轮把 sink IO 保持在已有 contracts Provider/Runtime 范畴内，而不是扩张共享错误码。

## 3. Blocker 修复与 Design 结论

阻塞结论：

1. 010 的真正缺口不是四个名字不存在，而是“logging 私有码 -> contracts::ResultCode”映射矩阵尚未冻结；如果直接写代码而不先固定矩阵，后续 011 恢复骨架将继续散落使用通用 contracts 错误码，无法统一测试断言。

最小 blocker-fix：

1. 以 header-only 方式新增 `LoggingErrors.h`，同时冻结四个码名、数值、来源锚点和一级 contracts 映射，并用 unit/contract 测试把矩阵固化下来。

设计结论：

1. LoggingErrors 采用 logging 私有 header-only 错误码定义，不扩张到新的 shared contracts 枚举。
2. 四个冻结错误码固定为：`LOG_E_QUEUE_FULL`、`LOG_E_SINK_IO`、`LOG_E_FORMAT_INVALID`、`LOG_E_CONFIG_INVALID`。
3. 映射规则限制为既有 contracts 失败域：
   - `LOG_E_QUEUE_FULL` -> `RuntimeRetryExhausted`
   - `LOG_E_SINK_IO` -> `ProviderTimeout`
   - `LOG_E_FORMAT_INVALID` -> `ValidationFieldMissing`
   - `LOG_E_CONFIG_INVALID` -> `ValidationFieldMissing`
4. `LOG_E_SINK_IO` 当前先冻结到 Provider 类别，后续 011 若进入 fallback/retry，只允许在恢复结果层补充 retry/degraded 状态，不替换 010 已冻结的私有码名。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结四个 logging 私有错误码 | infra/include/logging/LoggingErrors.h |
| 冻结错误码名到字符串映射 | logging_error_code_name() |
| 冻结 logging -> contracts 一级映射 | map_logging_error_code() |
| 固化数值、名字、来源锚点与映射矩阵 | tests/unit/infra/LoggingErrorsTest.cpp |
| 阻断 contracts 越权扩张并保持私有命名空间 | tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp |

## 5. Build 三件套

1. 代码目标：新增 [infra/include/logging/LoggingErrors.h](/home/gangan/DASALL/infra/include/logging/LoggingErrors.h)，并在 [infra/CMakeLists.txt](/home/gangan/DASALL/infra/CMakeLists.txt) 中注册 public header。
2. 测试目标：新增 [tests/unit/infra/LoggingErrorsTest.cpp](/home/gangan/DASALL/tests/unit/infra/LoggingErrorsTest.cpp)，冻结四个错误码的数值、名字、来源锚点与 contracts 映射；新增 [tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp](/home/gangan/DASALL/tests/contract/smoke/LoggingErrorsBoundaryContractTest.cpp)，校验仅映射到既有 contracts `ResultCode`，且名字保持在 `LOG_E_*` 私有命名空间。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_logging_errors_unit_test dasall_contract_logging_errors_boundary_test`
   - `ctest --test-dir build-ci -N -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"`
   - `ctest --test-dir build-ci --output-on-failure -R "LoggingErrorsTest|LoggingErrorsBoundaryContractTest"`

## 6. 风险与回退

1. `LOG_E_CONFIG_INVALID` 当前只冻结到 validation 类别，待 LOG-TODO-012 解阻后如果需要更细粒度配置差异，只能在 reason 或配置诊断对象层补充，不得重写 010 的码名或 contracts 映射。
2. 本轮不把 logging 私有错误码并入 [infra/include/InfraErrorCode.h](/home/gangan/DASALL/infra/include/InfraErrorCode.h)，避免在 logging 构建接线尚未完成前引入额外耦合；若后续 facade 需要统一桥接，应新增单独 adapter。
3. 本轮不修改现有 `LogWriteResult` 接口面，只先冻结错误码域；011 可以在不破坏接口签名的前提下逐步把恢复骨架切换到 `LoggingErrors`。