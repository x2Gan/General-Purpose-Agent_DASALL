# LOG-BLK-001 LoggingConfig 设计收敛

日期：2026-04-03  
任务：LOG-BLK-001  
状态：解阻 PASS

## 1. 本地证据

1. [docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md](/home/gangan/DASALL/docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md) 将 LOG-TODO-012 标记为 Blocked，并把根因明确为“logging config 模型未冻结：apply(config) 的 config 结构、冲突裁定规则、运行时 patch 形状不明确”。
2. [docs/architecture/DASALL_infra_logging模块详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infra_logging模块详细设计.md) 6.6 已给出 `ILogConfigurator::apply(config)`，6.9 已给出 logging 配置键表，但当前仍停留在裸键名和口头覆盖层级，尚未把对象形状、键名前缀、局部接受规则与 audit 主链保护冻结成可执行设计。
3. [docs/architecture/DASALL_infrastructure子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_infrastructure子系统详细设计.md) 6.6/6.9 已冻结 ConfigCenter 四层模型、`get_typed(query)` 读取入口、runtime override 的 typed patch 契约、以及 `profile_meta.*`/`enabled_modules.*` 保护路径。
4. [infra/include/config/ConfigTypes.h](/home/gangan/DASALL/infra/include/config/ConfigTypes.h)、[infra/include/config/IConfigCenter.h](/home/gangan/DASALL/infra/include/config/IConfigCenter.h)、[infra/src/config/ConfigCenterFacade.cpp](/home/gangan/DASALL/infra/src/config/ConfigCenterFacade.cpp) 已落盘 `TypedConfig`、`ConfigQuery`、`ConfigSourceKind` 与 runtime override 校验逻辑，说明 logging 不应再发明第二套 patch 协议。
5. [tests/unit/profiles/ProfileOverlayComposerTest.cpp](/home/gangan/DASALL/tests/unit/profiles/ProfileOverlayComposerTest.cpp) 已验证四层优先级与 runtime override TTL/白名单约束，说明 logging 侧只需要冻结局部键域接受规则，而不是重做 overlay composer。

## 2. 外部参考

1. Microsoft Azure Architecture Center 的 External Configuration Store pattern 强调配置接口应暴露 typed/structured 数据、版本与作用域控制，并在启动或运行期故障时保留 fallback；这支持 logging 侧复用 ConfigCenter 的 typed config 入口，而不是接受自由字典或模块私有热改。
2. Better Stack 的 logging best practices 指出动态调整日志级别应走受控配置路径，并且日志配置要区分“可运行时调节的旋钮”和“应保持稳定的结构性选项”；这支持本轮把 runtime override 只开放给少数安全 tunable 键，而不是对全部 logging 配置开放热改。

## 3. 阻塞修复与设计结论

阻塞结论：

1. LOG-BLK-001 的真实缺口不是 ConfigCenter 不可用，而是 logging 侧缺少“本地配置对象 + 键名前缀 + 每个 key 允许来自哪些层”的正式收敛，导致 LOG-TODO-012 无法稳定判断哪些 runtime override 应接受、哪些应拒绝。

最小 blocker-fix：

1. 在 logging 详细设计中冻结 `ILogConfigurator` 的最小输入对象 `LoggingConfig`、结果对象 `LoggingConfigApplyResult`，并要求它们携带来源 `TypedConfig` 证据，而不是裸值集合。
2. 将 logging 配置键统一收敛到 `infra.logging.*` 前缀，并把 `infra.audit.required` 作为 logging 配置接受门的一部分，以显式阻止 profile/deployment/runtime 任一层绕过 audit 主链。
3. 把 runtime override 的局部接受规则固定为“只允许对安全 tunable 键生效，仍复用 ConfigCenter typed patch 契约，不开放 logging 私有 patch 结构”。

设计结论：

1. `ILogConfigurator` 只接受已经过 ConfigCenter 结构校验后的 `LoggingConfig`，不直接接受 YAML、JSON、CLI 参数或 logging 私有 patch 对象。
2. `LoggingConfig` 的最小字段冻结为：
   - `level`
   - `format`
   - `async_enabled`
   - `queue_size`
   - `overflow_policy`
   - `file_path`
   - `rotate_max_size_mb`
   - `rotate_max_files`
   - `redaction_enabled`
   - `redaction_ruleset`
   - `enable_diag_pull`
   - `audit_required`
   - `source_entries`
3. `source_entries` 复用 `config::TypedConfig`，用于保留当前生效值的来源层与来源标识，使 logging adapter 可以在本地执行“每个键允许的层级”校验，而不发明第二套 provenance 模型。
4. 键名冻结为：
   - `infra.logging.level`
   - `infra.logging.format`
   - `infra.logging.async.enabled`
   - `infra.logging.async.queue_size`
   - `infra.logging.async.overflow_policy`
   - `infra.logging.file.path`
   - `infra.logging.file.rotate.max_size_mb`
   - `infra.logging.file.rotate.max_files`
   - `infra.logging.redaction.enabled`
   - `infra.logging.redaction.ruleset`
   - `infra.logging.export.enable_diag_pull`
   - `infra.audit.required`
5. 局部层级接受规则冻结为：
   - `infra.logging.level`：默认/Profile/部署/运行时
   - `infra.logging.format`：默认/Profile/部署
   - `infra.logging.async.enabled`：默认/Profile
   - `infra.logging.async.queue_size`：默认/Profile/部署
   - `infra.logging.async.overflow_policy`：默认/Profile/部署
   - `infra.logging.file.path`：默认/部署/运行时
   - `infra.logging.file.rotate.max_size_mb`：默认/Profile/部署
   - `infra.logging.file.rotate.max_files`：默认/Profile/部署
   - `infra.logging.redaction.enabled`：默认/Profile
   - `infra.logging.redaction.ruleset`：默认/部署/运行时
   - `infra.logging.export.enable_diag_pull`：默认/Profile/部署
   - `infra.audit.required`：默认/Profile，且值必须保持 `true`
6. 运行时覆盖规则只开放给安全 tunable 键：`infra.logging.level`、`infra.logging.file.path`、`infra.logging.redaction.ruleset`。其它 logging 键即便通过了 ConfigCenter 的通用 patch 校验，只要来源层是 `RuntimeOverride`，仍由 LoggingConfigAdapter 在本地拒绝并返回 `LOG_E_CONFIG_INVALID`。
7. `infra.audit.required` 不是 logging 私有键，但 logging adapter 必须将其作为准入前置条件读取；任何层若试图把它改为 `false`，都视为“绕过 audit 主链”的无效配置。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 logging 配置 public 输入对象 | infra/include/logging/ILogConfigurator.h |
| 冻结 logging 配置结果对象与 contracts 错误边界 | infra/include/logging/ILogConfigurator.h |
| 复用 ConfigCenter typed config 证据而不定义 logging 私有 patch | infra/src/logging/LoggingConfigAdapter.cpp |
| 固化 per-key 层级接受规则与 runtime tunable 白名单 | infra/src/logging/LoggingConfigAdapter.cpp |
| 验证四层优先级读取与非法 runtime override 拒绝 | tests/unit/infra/logging/LoggingConfigMergeTest.cpp |
| 验证 profile 不绕过 audit 主链与接口结果只使用 contracts 错误类型 | tests/contract/smoke/LogConfiguratorBoundaryContractTest.cpp |

## 5. 对 LOG-TODO-012 的直接交接

1. 012 可以从 Blocked 转为 Not Started，并在本轮实现中直接落以下最小闭环：
   - 新增 `ILogConfigurator.h`
   - 新增 `LoggingConfigAdapter.h/.cpp`
   - 通过 `IConfigCenter::get_typed()` 读取 frozen key set
   - 在 adapter 本地执行 per-key source acceptance 与 audit gate 校验
   - 追加 unit/contract 测试与 CMake 注册
2. 012 不应额外发明 `LoggingConfigPatch` 或第二套 override 元数据；runtime override 仍以 ConfigCenter 的 `ConfigPatch`/`TypedConfig` 为唯一来源协议。

## 6. 风险与回退

1. 如果后续 infra/config 修改 `ConfigSourceKind` 或 protected path 契约，logging 侧必须同步 review `source_entries` 接受规则；若不同步，LOG-BLK-001 应重新转为 Blocked。
2. `infra.audit.required` 当前被冻结为 logging 配置准入门的一部分；若后续 audit TODO 明确更细粒度的 fallback 语义，只能通过 bridge 或 reason 扩展，不得把该键改成 profile/deployment/runtime 可关闭。
3. 本轮只冻结对象和规则，不提前落盘真实 sink/file rotation/formatter 实现；若 012 实现中出现跨到 metrics/health/audit bridge 的需求，应视为越界并停止。 