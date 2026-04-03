# LOG-TODO-012 LoggingConfigAdapter 设计收敛

日期：2026-04-03  
任务：LOG-TODO-012  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](/home/gangan/DASALL/docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 的 LOG-TODO-012 已在 LOG-BLK-001 解阻后进入 Not Started，目标固定为“实现 LoggingConfigAdapter 四层配置适配”，测试出口为“unit：层级优先级验证；contract：Profile 不绕过审计主链”。
2. [docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig设计收敛.md](/home/gangan/DASALL/docs/todos/infrastructure/deliverables/LOG-BLK-001-LoggingConfig设计收敛.md) 已冻结 `LoggingConfig`/`LoggingConfigApplyResult`、`infra.logging.*` frozen key set、per-key 层级接受规则以及 `infra.audit.required` 准入门，012 可以直接进入 Build。
3. [infra/include/config/IConfigCenter.h](/home/gangan/DASALL/infra/include/config/IConfigCenter.h) 与 [infra/include/config/ConfigTypes.h](/home/gangan/DASALL/infra/include/config/ConfigTypes.h) 已提供 `get_typed(query)`、`TypedConfig`、`ConfigSourceKind` 和 fallback 查询所需的最小接口，不需要 logging 再定义私有 patch/overlay 契约。
4. [infra/src/config/ConfigCenterFacade.cpp](/home/gangan/DASALL/infra/src/config/ConfigCenterFacade.cpp) 已证明 ConfigCenter 会先完成 typed patch 校验与 source_chain 维护；logging 适配器的职责应收敛为“读取 active typed config + 执行本地 key 语义接受规则”。
5. [tests/unit/profiles/ProfileOverlayComposerTest.cpp](/home/gangan/DASALL/tests/unit/profiles/ProfileOverlayComposerTest.cpp) 已覆盖 deployment/runtime 的四层顺序与非法 override 拒绝，说明 logging 侧不应复制 merge 逻辑，而应消费合并后的 active 值和 source kind。

## 2. 外部参考

1. Microsoft Azure Architecture Center 的 External Configuration Store pattern 强调：配置访问层应基于 typed/structured 数据、版本与作用域来读取与应用配置，而不是允许模块各自接收自由字典；这支持本轮把 logging 配置适配严格收敛到 `IConfigCenter::get_typed()`。
2. Better Stack 的 logging best practices 强调动态日志级别应通过受控配置路径调节，并建议把运行时可调“旋钮”与结构性配置分开治理；这支持本轮仅对白名单 tunable 键放行 runtime override。

## 3. 设计结论

1. `ILogConfigurator` 作为 logging public config surface，仅暴露 `apply(const LoggingConfig&) -> LoggingConfigApplyResult`，不泄露 ConfigCenter 生命周期、patch、rollback 或 subscribe 细节。
2. `LoggingConfigAdapter` 是 logging 内部的 ConfigCenter 适配器，职责固定为：
   - 读取 frozen key set 的 active `TypedConfig`
   - 解析为 `LoggingConfig`
   - 校验每个 key 的来源层是否满足 logging 本地接受规则
   - 校验 `infra.audit.required` 仍为 `true`
   - 成功后保留 active config，失败时返回显式配置错误并保留前一稳定状态
3. runtime override 只允许对 `infra.logging.level`、`infra.logging.file.path`、`infra.logging.redaction.ruleset` 生效；`queue_size`、`overflow_policy`、`async.enabled`、`enable_diag_pull` 等结构性键即使通过 ConfigCenter 的通用结构校验，只要来源层为 `RuntimeOverride`，仍由 logging adapter 本地拒绝。
4. 本轮不把 `LoggingConfigAdapter.cpp` 接到 `dasall_infra` 静态库源码列表，避免越过 LOG-TODO-014 的构建接线任务；新增 unit/contract 测试继续沿用 logging 当前的“测试目标直编 src/logging/*.cpp”模式。
5. 原任务行中的验收命令只有 `cmake --build build-ci --target dasall_infra`，无法满足“三件套里的测试目标与发现性”要求；本轮已将验收证据升级为显式构建新增 unit/contract 目标并执行 CTest 发现与运行。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 logging 配置 public surface | infra/include/logging/ILogConfigurator.h |
| 落 ConfigCenter -> LoggingConfig 的最小适配骨架 | infra/src/logging/LoggingConfigAdapter.h; infra/src/logging/LoggingConfigAdapter.cpp |
| 固化 per-key source acceptance 与 audit gate | infra/src/logging/LoggingConfigAdapter.cpp |
| 正例：四层 active config 与 source kind 保留 | tests/unit/infra/logging/LoggingConfigMergeTest.cpp |
| 负例：非 tunable runtime override 被拒绝 | tests/unit/infra/logging/LoggingConfigMergeTest.cpp |
| 边界：profile 不绕过 audit 主链、结果只使用 contracts 错误类型 | tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp |
| 注册新增头文件与测试目标 | infra/CMakeLists.txt; tests/unit/infra/CMakeLists.txt; tests/unit/CMakeLists.txt; tests/contract/CMakeLists.txt |

## 5. Build 三件套

1. 代码目标：新增 [infra/include/logging/ILogConfigurator.h](/home/gangan/DASALL/infra/include/logging/ILogConfigurator.h)、[infra/src/logging/LoggingConfigAdapter.h](/home/gangan/DASALL/infra/src/logging/LoggingConfigAdapter.h)、[infra/src/logging/LoggingConfigAdapter.cpp](/home/gangan/DASALL/infra/src/logging/LoggingConfigAdapter.cpp)，实现 logging frozen key set 的读取、解析、本地 source acceptance 与 audit gate。
2. 测试目标：新增 [tests/unit/infra/logging/LoggingConfigMergeTest.cpp](/home/gangan/DASALL/tests/unit/infra/logging/LoggingConfigMergeTest.cpp) 覆盖 runtime/deployment/profile 生效路径与非法 runtime override 负例；新增 [tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp](/home/gangan/DASALL/tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp) 守住 contracts 错误边界和 `infra.audit.required` 不可被 profile 关闭。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_logging_config_merge_unit_test dasall_contract_log_configurator_boundary_test`
   - `ctest --test-dir build-ci -N -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"`
   - `ctest --test-dir build-ci --output-on-failure -R "(LoggingConfigMergeTest|LogConfiguratorBoundaryContractTest)"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`

## 6. 风险与回退

1. `LoggingConfigAdapter` 当前只读取 active typed config，不订阅 ConfigChanged 事件；后续若要做自动刷新，只能在现有 `ILogConfigurator` surface 外侧增加 bridge，不能回退到模块私有 patch 或隐藏 source acceptance 规则。
2. 现在对 `infra.audit.required=false` 一律拒绝；如果后续 audit 子域给出更细粒度的降级模式，也应通过 bridge/diagnostic reason 扩展，而不是允许 logging config 直接关闭 audit 主链。
3. `LoggingFormat` 首版只接受 `json_line` 和 `key_value`；若后续 StructuredFormatter 冻结更多格式值，应扩展 parser 与测试矩阵，但不能改写当前两种值的既有语义。