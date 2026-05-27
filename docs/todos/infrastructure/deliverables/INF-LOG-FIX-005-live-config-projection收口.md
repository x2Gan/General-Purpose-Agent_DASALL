# INF-LOG-FIX-005 live config projection 收口

关联任务：INF-LOG-FIX-005  
日期：2026-05-27  
结论级别：L3 build-tree live composition evidence

## 1. 本轮目标

把 frozen logging config 从 adapter 独立 parse/apply 推进到 live logger 主链：

1. `LoggingConfigAdapter` 严格拒绝 trailing junk numeric 配置。
2. `compose_live_observability()` 先形成 active `LoggingConfig`，再按该配置组装 live logger backend。
3. `StructuredFormatter`、`RedactionFilter` 与 `LoggingFacade` 提供最小 config surface，使 format/redaction 配置真实生效。
4. `RuntimeLiveDependencyComposition` 只投影当前 `RuntimePolicySnapshot` 已有的 `log_level` / `remote_diagnostics_enabled`，其余 frozen keys 继续走 typed config entries 或 adapter fallback。

## 2. 改动范围

1. `infra/include/ObservabilityLiveComposition.h`
2. `infra/src/ObservabilityLiveComposition.cpp`
3. `infra/src/logging/LoggingConfigAdapter.h`
4. `infra/src/logging/LoggingConfigAdapter.cpp`
5. `infra/src/logging/LoggingFacade.h`
6. `infra/src/logging/LoggingFacade.cpp`
7. `infra/include/logging/StructuredFormatter.h`
8. `infra/src/logging/StructuredFormatter.cpp`
9. `infra/include/logging/RedactionFilter.h`
10. `infra/src/logging/RedactionFilter.cpp`
11. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp`
12. `tests/unit/infra/logging/LoggingConfigAdapterStrictParseTest.cpp`
13. `tests/integration/infra/logging/LoggingLiveCompositionConfigTest.cpp`
14. `tests/unit/CMakeLists.txt`
15. `tests/unit/infra/CMakeLists.txt`
16. `tests/integration/CMakeLists.txt`
17. `tests/integration/infra/logging/CMakeLists.txt`

## 3. 关键结论

1. `LoggingConfigAdapter::parse_uint32_value()` 现要求消费完整字符串，任何尾缀脏数据都会 fail-closed，而不是被 `stoull` 部分吞掉。
2. `compose_live_observability()` 现先经 `LoggingConfigAdapter` 形成 active `LoggingConfig`，再依 `async_enabled` 选择 queue-backed `SinkDispatcher` 或 direct sink dispatcher，并将 `file_path`、rotation、queue、overflow、format、redaction 配置投影到真实 backend。
3. `LoggingFacade` / `StructuredFormatter` / `RedactionFilter` 已补最小 config surface，因此 `key_value` formatter、redaction toggle 与 ruleset 不再停留在 parse 侧，而是进入 live logger 主链。
4. 最小 profile projection 已冻结：当前 `RuntimePolicySnapshot` 只向 live logging 投影 `ops_policy.log_level` 与 `remote_diagnostics_enabled`；其余 frozen logging keys 必须走 typed config entries 或 adapter fallback，避免在本轮隐式扩 public runtime policy schema。

## 4. focused 验证

1. `Build_CMakeTools(buildTargets=["dasall_logging_config_adapter_strict_parse_unit_test","dasall_logging_live_composition_config_integration_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test"])`
   - 结果：通过。
2. `RunCtest_CMakeTools(tests=["LoggingConfigAdapterStrictParseTest","LoggingLiveCompositionConfigTest","DaemonRuntimeLiveDependencyCompositionTest"])`
   - 结果：命中仓库既有泛化 `生成失败`。
3. fallback 直接执行：
   - `./build/vscode-linux-ninja/tests/unit/infra/dasall_logging_config_adapter_strict_parse_unit_test`
   - `./build/vscode-linux-ninja/tests/integration/infra/logging/dasall_logging_live_composition_config_integration_test`
   - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`
   - 结果：3/3 通过。

## 5. 非外推说明

1. 本轮只到 L3 build-tree live composition evidence；recovery/fallback、metrics/health、diagnostics artifact 与 installed package proof 继续留给 `INF-LOG-FIX-006~011`。
2. 当前关闭的是最小 profile projection blocker，不代表 `RuntimePolicySnapshot` 已拥有完整 `infra.logging.*` public schema；若后续需要扩 public schema，必须另开任务。
3. qemu / kvm 仍不属于当前 logging owner 验收前置。