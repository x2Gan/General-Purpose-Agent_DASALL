# INF-FIX-005 optional backend profile-gated 接入收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `INF-FIX-005`。
2. 本轮目标：把 secret KMS、metrics OTLP、tracing exporter 等 optional backend 从“只有 interface / local fallback”收口为 profile-gated runtime policy 事实，补齐 package asset、dependency availability 与 unavailable error 语义，但不越权宣称 external backend 已进入 production ready。
3. 用户附加约束：按 `project-implementation-cycle` 串行推进；如存在前置 blocker 先解阻；逐文件落盘；完成后按仓库规范提交推送；禁止使用 qemu / kvm 采集收敛证据，优先本地 build-tree / installed direct evidence。

## 2. 本地证据

1. `docs/architecture/DASALL_profiles模块详细设计.md` 已冻结 `RuntimePolicySnapshot` 作为 runtime consumer 的 authoritative profile 输入，profiles 只能提供能力裁剪与策略基线，不能绕过 runtime 主控链路。
2. `docs/architecture/DASALL_infrastructure子系统详细设计.md` 已把 `infra.tracing.enabled`、`infra.metrics.*` 与 `infra.secret.backend` 列为 profile/deployment 侧受管配置域；因此 optional backend 必须经 profile/runtime policy 投影，而不是只在 adapter interface 层存在。
3. 变更前 `infra/src/metrics/MetricsExporterAdapter.cpp` 与 `infra/src/tracing/SpanExporterAdapter.cpp` 在 unsupported exporter 场景会把请求 silently collapse 成 local/noop 语义，导致 external backend request 无法与“本地 baseline 就是 noop/file”区分。
4. 变更前 `profiles/src/RuntimePolicyProvider.cpp` 与 `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 没有把 `runtime_policy.yaml` 中的 metrics/tracing/secret backend 选择投影到 `RuntimePolicySnapshot` 与 live observability composition；即使 profile 资产声明了 exporter type，runtime_support 仍默认走 noop trace/noop metrics。
5. `tests/integration/profiles/ProfilesBuildRuntimeIntegrationTest.cpp`、`tests/unit/profiles/RuntimePolicyProviderTest.cpp` 与 `tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp` 提供了最接近 owner 的 profile asset -> snapshot -> schema freeze 证据；`tests/integration/access/DaemonRuntimeLiveDependencyCompositionTest.cpp` 则提供 runtime_support live composition 是否仍能正常组合 shared observability 的近端验证。

## 3. 设计结论

### 3.1 根因收口

1. `INF-FIX-005` 的根因不是 OTLP/KMS backend interface 缺失，而是 optional backend request 没有被 profile/runtime policy authoritative surface 接住。
2. 若 profile 资产、runtime snapshot 与 live composition 不传递 backend type / package asset，系统就会把“external backend unavailable”误写成“baseline 本来就是 noop/file”，从而错误放大为 production ready 幻觉。
3. 因此本轮最小闭环必须同时覆盖三层：profile schema、runtime snapshot、runtime_support observability composition；仅改 adapter unavailable error 不足以完成任务。

### 3.2 profile-gated 边界

1. optional backend 现在以 `RuntimePolicySnapshot::ops_policy().optional_backends` 作为 runtime 消费视图，冻结 metrics exporter、trace exporter、trace OTLP endpoint、secret backend 及对应 package asset。
2. `RuntimePolicyProvider` 现在要求 `runtime_policy.yaml` 明确提供这些键；缺失时 fail-closed 为 invalid snapshot，而不是静默回退默认值。
3. metrics / tracing adapter 在 unsupported exporter 场景保留请求 backend type，并返回 unavailable error，避免 silent fallback 淹没 dependency availability 信号。

### 3.3 范围边界

1. 本轮只把 secret backend 收口到 profile/schema/snapshot/package asset 视图，不创建 `ISecretManager` live composition，也不把 secret backend 注入 daemon / gateway Access ownership seam。
2. 该 live composition 与 app 注入责任继续由 `INF-FIX-007` / `INF-FIX-008` 持有，避免把 `secret backend selected in profile` 误写成 `SecretManager production-ready`。
3. 本轮同样不宣称 qemu、autopkgtest、release-runner soak 或 external backend positive proof 已闭合。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | optional backend 必须进入 runtime snapshot 的 authoritative policy view | `profiles/include/RuntimePolicySnapshot.h`、`profiles/src/RuntimePolicyProvider.cpp` |
| D2 | profile schema 必须冻结 backend type 与 package asset，而不是靠 provider 默认值兜底 | 5 个 `profiles/*/runtime_policy.yaml`、`tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp` |
| D3 | runtime_support 必须把 metrics/tracing exporter type 从 snapshot 投影进 shared observability composition | `infra/include/ObservabilityLiveComposition.h`、`infra/src/ObservabilityLiveComposition.cpp`、`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` |
| D4 | unsupported external backend 必须显式 unavailable，而不是 silent fallback | `infra/src/metrics/MetricsExporterAdapter.cpp`、`infra/src/tracing/SpanExporterAdapter.cpp`、对应 unit tests |
| D5 | secret backend 只冻结 profile-gated config，不越权宣称 SecretManager live composition ready | `profiles/*/runtime_policy.yaml`、`profiles/src/RuntimePolicyProvider.cpp`、`docs/todos/DASALL_子系统查漏补缺专项记录.md` |

## 5. Build 三件套

1. 代码目标：
   - 在 `RuntimePolicySnapshot::OpsPolicy` 中新增 `optional_backends` 视图，承载 metrics / trace / secret backend type 与 package asset。
   - 更新 `RuntimePolicyProvider`、5 个 `runtime_policy.yaml` 与 profile schema contract，使 optional backend 成为显式 profile 字段，并在缺键时 fail-closed。
   - 更新 `compose_runtime_observability_bundle()` 与 shared observability options，把 metrics / trace exporter type 和 OTLP endpoint 传入 live composition；同时保持 unsupported exporter 返回 unavailable error。
2. 测试目标：
   - `ProfilesBuildRuntimeIntegrationTest`
   - `RuntimePolicyProviderTest`
   - `ProfileRuntimePolicySchemaContractTest`
   - `DaemonRuntimeLiveDependencyCompositionTest`
   - `SecretTypesTest`
   - `ServiceMetricsBridgeTest`
   - `ServiceTraceBridgeTest`
3. 验收命令：
   - `Build_CMakeTools(buildTargets=["dasall_profiles_build_runtime_integration_test","dasall_runtime_policy_provider_unit_test","dasall_contract_profile_runtime_policy_schema_test","dasall_access_daemon_runtime_live_dependency_composition_integration_test","dasall_service_metrics_bridge_unit_test","dasall_service_trace_bridge_unit_test","dasall_secret_types_unit_test"])`
   - `RunCtest_CMakeTools(tests=["ProfilesBuildRuntimeIntegrationTest","RuntimePolicyProviderTest","ProfileRuntimePolicySchemaContractTest","DaemonRuntimeLiveDependencyCompositionTest"])` 当前仍返回仓库已知泛化 `生成失败`，因此按 fallback 直接执行：
     - `./build/vscode-linux-ninja/tests/integration/profiles/dasall_profiles_build_runtime_integration_test`
     - `./build/vscode-linux-ninja/tests/unit/profiles/dasall_runtime_policy_provider_unit_test`
     - `./build/vscode-linux-ninja/tests/contract/dasall_contract_profile_runtime_policy_schema_test`
     - `./build/vscode-linux-ninja/tests/integration/access/dasall_access_daemon_runtime_live_dependency_composition_integration_test`
     - `./build/vscode-linux-ninja/tests/unit/services/bridges/dasall_service_metrics_bridge_unit_test`
     - `./build/vscode-linux-ninja/tests/unit/services/bridges/dasall_service_trace_bridge_unit_test`
     - `./build/vscode-linux-ninja/tests/unit/infra/dasall_secret_types_unit_test`

## 6. Rollout Checklist

1. 5 个 profile 资产都显式声明了 metrics exporter package asset、tracing exporter type/package asset 与 secret backend package asset。
2. `RuntimePolicyProvider` 不再对这些字段 silent fallback；缺键时直接拒绝 snapshot。
3. runtime_support shared observability 现在消费 profile 选定的 metrics / trace exporter type，而不是硬编码默认 noop。
4. unsupported metrics / trace exporter 现在保留请求 backend type，并返回 unavailable error，避免把 unavailable 混写成本地 baseline。
5. secret backend 目前只完成 profile-gated 选择与 package asset freeze，不外推成 `ISecretManager` live composition ready。
6. 本轮未使用 qemu / kvm，也未把结果外推到 installed external backend positive proof、release-runner 或 soak。

## 7. 风险与回退

1. 若后续把 `runtime_policy.yaml` 的 optional backend 键删回 provider 默认值，profile/runtime snapshot 将再次失去 authoritative gate，`INF-FIX-005` 会回退成 interface-only 幻觉。
2. 若后续把 unsupported exporter 再次写成 silent noop/file fallback，external backend unavailable 将重新被误判为 baseline ready。
3. 若后续把 secret backend selected 直接宣称为 SecretManager live composition ready，会与 `INF-FIX-007` / `INF-FIX-008` owner 范围混写。

## 8. D Gate

1. optional backend 已从 profile asset -> runtime snapshot -> runtime_support observability composition 形成闭环。
2. package asset、unavailable error、fail-closed schema freeze 与 focused build-tree evidence 已落盘。
3. SecretManager live composition、daemon/gateway secret injection、qemu/release/soak 继续保留在后续任务，不被本轮误收口。

结论：D Gate = PASS；`INF-FIX-005` 已以 profile-gated optional backend、package asset freeze、unavailable semantics 与 runtime observability projection 收口。