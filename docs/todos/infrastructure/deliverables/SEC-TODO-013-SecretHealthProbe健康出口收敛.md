# SEC-TODO-013 SecretHealthProbe 健康出口收敛

日期：2026-04-04
任务：SEC-TODO-013
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-013 定义为“实现 SecretHealthProbe 健康出口骨架”，验收出口是 backend down、rotation backlog、cache stale 三路径。
2. ISecretHealthSource 已冻结 `sample_secret_health()` 为 secret 私有健康边界；TODO 的 4.2 明确指出本轮只落 secret 私有健康快照，不提前吸收 `IHealthMonitor` 通用契约。
3. SEC-TODO-009、SEC-TODO-010、SEC-TODO-012 已分别完成 lease、rotation 和 audit 骨架，因此本轮可以直接聚合 backend / rotation / cache 三类现有信号。

## 2. 研究学习结果

### 2.1 本地证据

1. SecretHealthSnapshot 已在 ISecretHealthSource.h 中冻结 `backend_available`、`cache_stale`、`active_lease_count`、`rotation_backlog` 和 `degraded` 五个最小字段，说明 013 只需把现有信号映射到该 snapshot，而不必再扩展对象模型。
2. SecretBackendStatus 与 SecretRotationCoordinatorStatus 已提供 backend state / last_error 和 rotation backlog / rollback failure / degraded 状态，足以作为健康出口的稳定输入面。
3. LoggingHealthProbe 的实现和单测展示了仓库当前对健康状态的最小模式：信号 provider + 私有聚合逻辑 + 可二值判定回归，这适合 SecretHealthProbe 直接复用。

### 2.2 外部参考

1. OWASP Secrets Management Cheat Sheet 要求 secret backend 故障、轮换滞留和缓存过期都必须可观测，这支持 DASALL 在 secret 私有健康快照里直接暴露 backend_available / rotation_backlog / cache_stale 三个关键信号。
2. Azure Key Vault secrets best practices 建议以健康/告警信号反映 secret store 可用性与轮换状态，这支持 DASALL 在 SecretHealthProbe 中把 backlog 和 backend down 统一收敛为 degraded/unhealthy 先验输入。

### 2.3 可落地启发

1. 013 的最小实现应保持为 secret 私有 signal provider + `ISecretHealthSource` 出口，不提前耦合 `IHealthProbe` / `IHealthMonitor`。
2. backend `Available/Degraded/Unavailable` 可以映射为 `backend_available + degraded` 二元组合；rotation backlog 和 cache stale 再叠加 degraded 即可满足首轮验收。
3. active lease count 只需透传，不必在 013 同轮引入 lease 细分原因或 audit side effects。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结健康 signal 聚合输入 | ISecretHealthSource；SecretBackendStatus；RotationStatus | SecretHealthProbe.h | provider 输入可覆盖 backend/cache/rotation 三类路径 |
| D2 | 落盘 secret 私有健康快照逻辑 | TODO 任务要求；secret 设计 6.10 | SecretHealthProbe.cpp | backend down、rotation backlog、cache stale 都能映射到 snapshot |
| D3 | 固化健康回归测试 | TODO 任务要求 | SecretHealthProbeTest.cpp | healthy + 三条故障路径均可二值判定 |
| D4 | 接入最小 CMake discoverability | 任务要求 | infra/CMakeLists.txt；tests/unit/* | target 可被 ctest 发现且 1/1 通过 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| secret 私有健康信号通过 provider 聚合 | infra/src/secret/SecretHealthProbe.h；infra/src/secret/SecretHealthProbe.cpp |
| backend down / backlog / stale 直接映射到 SecretHealthSnapshot | infra/src/secret/SecretHealthProbe.cpp |
| 健康回归用脚本化 provider 固定三类风险路径 | tests/unit/infra/secret/SecretHealthProbeTest.cpp |
| CMake 必须纳入 probe 源码和 unit test target | infra/CMakeLists.txt；tests/unit/CMakeLists.txt；tests/unit/infra/CMakeLists.txt |

### 4.2 Build 三件套

1. 代码目标：新增 SecretHealthProbe 私有头/源，落盘 signal provider 聚合和 `sample_secret_health()` 实现。
2. 测试目标：新增 SecretHealthProbeTest，覆盖 nominal、backend down、rotation backlog、cache stale 四类快照路径。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_infra dasall_secret_health_probe_unit_test`
   - `ctest --test-dir build-ci -N -R SecretHealthProbeTest`
   - `ctest --test-dir build-ci --output-on-failure -R SecretHealthProbeTest`

### 4.3 D Gate

结论：PASS。

理由：

1. 013 的前置依赖 SEC-TODO-002 / 009 / 010 已完成，且健康出口仍保持在 secret 私有边界内。
2. 本轮实现没有提前把 secret 健康信号扩展到通用 health monitor、integration 或 profile 路径。

## 5. Build 落地结果

1. 新增 infra/src/secret/SecretHealthProbe.h，定义 `SecretHealthProbeSignals`、`ISecretHealthSignalProvider` 与 `SecretHealthProbe`。
2. 新增 infra/src/secret/SecretHealthProbe.cpp，落盘 backend availability / degraded 规则、rotation backlog 聚合，以及 cache stale 到 snapshot 的映射。
3. 新增 tests/unit/infra/secret/SecretHealthProbeTest.cpp，覆盖 healthy、backend down、rotation backlog 和 cache stale 四路径。
4. 更新 infra/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/unit/infra/CMakeLists.txt，将 probe 源码和 unit test target 纳入构建图。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_infra dasall_secret_health_probe_unit_test`：通过。
3. `ctest --test-dir build-ci -N -R SecretHealthProbeTest`：通过，发现 1 个定向测试。
4. `ctest --test-dir build-ci --output-on-failure -R SecretHealthProbeTest`：通过，1/1 tests passed。

## 7. 结论

1. SEC-TODO-013 已把 secret 健康链路从“接口已冻结但无实现”推进到“存在 provider 聚合 + 私有 snapshot + unit evidence 的可验证骨架”。
2. SecretHealthProbe 现在已把 backend down、rotation backlog、cache stale 三类风险收敛为稳定 snapshot，为后续 014/015 的 CMake 与测试门禁收口提供明确输入面。