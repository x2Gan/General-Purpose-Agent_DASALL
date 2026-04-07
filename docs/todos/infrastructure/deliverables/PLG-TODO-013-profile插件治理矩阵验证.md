# PLG-TODO-013 Profile 插件治理矩阵验证

日期：2026-04-07
任务：PLG-TODO-013
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-013 定义为“编写 Profile 插件治理行为矩阵测试”，要求覆盖 desktop_full、edge_balanced、edge_minimal 三档 profile 下 plugin.allowlist、infra.plugin.signature.required、infra.plugin.abi.strict_mode 等行为差异与一致性检查。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.9 已冻结 plugin 治理配置面：`infra.plugin.enabled`、`allowlist`、`search_paths`、`load_timeout_ms`、`max_active`、`signature.required`、`trust.min_level`、`abi.strict_mode`、`remote_fetch.enabled`、`safe_mode.fail_threshold`。
3. tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp 已冻结 runtime_policy.yaml 的顶层 key 对齐规则，因此 013 若引入新的 plugin 配置域，必须在五档 profile 资产上同时补齐，而不能只改三档执行 profile。

## 2. 研究学习结果

### 2.1 本地证据

1. profiles/desktop_full、cloud_full、edge_balanced、edge_minimal、factory_test 的 runtime_policy.yaml 在本轮前均缺少 `infra.plugin.*` 键，因此 013 的真实缺口不是“缺一个测试文件”，而是 profile 资产还没有冻结 plugin 治理配置面。
2. profiles/include/RuntimePolicySnapshot.h 当前不承载 plugin policy 域，如果强行把 013 做成 runtime snapshot 行为测试，会把任务面扩大到 profiles 公共对象改动，偏离本轮“矩阵测试”目标。
3. infra/include/config/ConfigLoader.h 与 infra/src/config/ConfigLoader.cpp 已支持把 profile runtime_policy.yaml 扁平化为 typed config，且保留 `source_kind=Profile` 与 `source_id=profiles/<profile>/runtime_policy.yaml`，非常适合做 013 的最小真实链路验证。
4. tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp 会校验所有 profile 顶层 key 集合一致，因此 013 必须把 cloud_full 与 factory_test 也补到同一 schema，否则 contract 会直接失败。

### 2.2 外部参考

1. Twelve-Factor App 的 Config 原则强调配置应显式外置，并允许在不同 deploy/profile 间通过配置矩阵表达行为差异，而不是把环境差异硬编码在代码路径里。
2. 该原则对 013 的直接启发是：profile plugin 行为差异应冻结在 runtime_policy.yaml 中，并通过加载器/契约测试校验其稳定性，而不是通过分支逻辑隐式推导。

### 2.3 可落地启发

1. 013 的最小 blocker-fix 是先把 `infra.plugin.*` schema 冻结到五档 profile，再做三档 profile 的行为矩阵验证。
2. 不需要扩写 RuntimePolicySnapshot；直接用 ConfigLoader 读取 profile 资产即可覆盖“真实 YAML -> typed config -> source provenance”这条链路。
3. schema contract 与 matrix integration 应该并行存在：前者锁结构，后者锁 desktop_full/edge_balanced/edge_minimal 的值语义。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 profile plugin schema | plugin 详细设计 6.9 | 五档 profile 统一补齐 `infra.plugin.*` | schema contract 不再缺失 plugin key |
| D2 | 冻结三档 profile 的行为矩阵 | TODO 013 验收项 | desktop_full / edge_balanced / edge_minimal 的 allowlist、search_paths、load_timeout_ms、max_active、safe_mode.fail_threshold 明确可测 | matrix test 可精确断言每档值 |
| D3 | 锁定安全基线一致性 | plugin 详细设计 6.9、风险表 | 三档 profile 的 `signature.required=true`、`abi.strict_mode=true`、`remote_fetch.enabled=false`、`trust.min_level=internal` | matrix test 可统一断言 |
| D4 | 保持 profiles 公共对象边界不扩张 | RuntimePolicySnapshot 现状 | 不修改 RuntimePolicySnapshot / RuntimePolicyProvider | 013 仍限定在 profile 资产 + contract/integration tests |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. 013 在本轮前存在隐藏 blocker：五档 runtime_policy.yaml 都没有 `infra.plugin.*` 键，导致 TODO 虽然写着“配置项与默认值已列表”，但实际 profile 资产并未冻结该配置面。
2. RuntimePolicySnapshot 当前不承载 plugin policy 域，如果沿 snapshot 扩写，会把 013 扩大为 profiles 公共对象任务，破坏原子粒度。
3. ProfileRuntimePolicySchemaContractTest 强制五档 profile 顶层 key 集合一致，因此不能只改 desktop_full/edge_balanced/edge_minimal 三档。

最小 blocker-fix：

1. 在五档 runtime_policy.yaml 中统一新增 `infra.plugin.*` schema，并为 cloud_full/factory_test 提前冻结占位策略值。
2. 在 ProfileRuntimePolicySchemaContractTest 中把 `infra.plugin.*` 纳入 required path 集合，防止后续 profile 资产漂移。
3. 新增 ProfilePluginMatrixIntegrationTest，直接用 ConfigLoader.load_profile() 对三档 profile 做 typed config 行为断言，而不修改 RuntimePolicySnapshot。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| profile plugin schema 必须五档对齐 | profiles/*/runtime_policy.yaml |
| schema contract 需要冻结 plugin key | tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp |
| 三档 profile 行为差异应走真实 YAML 加载链 | tests/integration/profiles/ProfilePluginMatrixIntegrationTest.cpp |
| 013 不应扩写 profiles 公共对象 | 不修改 profiles/include/RuntimePolicySnapshot.h |

### 4.2 Build 三件套

1. 代码目标：
   - profiles/desktop_full/runtime_policy.yaml
   - profiles/cloud_full/runtime_policy.yaml
   - profiles/edge_balanced/runtime_policy.yaml
   - profiles/edge_minimal/runtime_policy.yaml
   - profiles/factory_test/runtime_policy.yaml
   - tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp
   - tests/integration/profiles/CMakeLists.txt
2. 测试目标：
   - tests/integration/profiles/ProfilePluginMatrixIntegrationTest.cpp
   - tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_contract_profile_runtime_policy_schema_test dasall_profile_plugin_matrix_integration_test
   - ctest --test-dir build-ci -N | grep -E "Profile(RuntimePolicySchemaContractTest|PluginMatrixIntegrationTest)"
   - ctest --test-dir build-ci --output-on-failure -R "ProfileRuntimePolicySchemaContractTest|ProfilePluginMatrixIntegrationTest"

### 4.3 D Gate

结论：PASS。

理由：

1. blocker 已通过“五档 schema 对齐 + loader 真实链路测试”被最小化处理，不需要改动 RuntimePolicySnapshot 或 RuntimePolicyProvider 公共边界。
2. 013 的矩阵验证现在既锁住了结构，又锁住了三档 profile 的具体治理值，可进入独立提交。

## 5. Build 落地结果

1. 更新五档 profile runtime_policy.yaml，统一新增 `infra.plugin.*` 配置域，并按 profile 能力冻结 allowlist、search_paths、load_timeout_ms、max_active 与 safe_mode.fail_threshold。
2. 更新 tests/contract/smoke/ProfileRuntimePolicySchemaContractTest.cpp，把 `infra.plugin.*` 纳入 required path 集合，并补充 desktop_full/cloud_full/edge_balanced/edge_minimal 的 plugin allowlist 基线断言。
3. 更新 tests/integration/profiles/CMakeLists.txt，注册 `ProfilePluginMatrixIntegrationTest`。
4. 新增 tests/integration/profiles/ProfilePluginMatrixIntegrationTest.cpp，直接使用 ConfigLoader.load_profile() 对 desktop_full、edge_balanced、edge_minimal 三档 profile 的 typed config 矩阵进行断言，并检查 `source_kind=Profile` 与 `source_id` 来源追溯。

## 6. Build 合规复核

1. 边界：013 没有修改 RuntimePolicySnapshot、RuntimePolicyProvider 或任何 plugin public interface；变更严格限定在 profile 资产与 tests。
2. 根因处理：修复的是真实缺失的 `infra.plugin.*` profile schema，而不是在测试里伪造配置对象。
3. 值语义：desktop_full / edge_balanced / edge_minimal 三档 profile 分别冻结了不同的 allowlist、search_paths、load_timeout_ms、max_active 与 safe_mode.fail_threshold，满足行为矩阵验证目标。
4. 安全基线：三档 profile 统一保持 `signature.required=true`、`abi.strict_mode=true`、`remote_fetch.enabled=false`、`trust.min_level=internal` 与 `enabled=true`。
5. discoverability：新的 integration/contract 用例都已通过 CTest 图发现，不依赖手工执行 target 文件。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_contract_profile_runtime_policy_schema_test dasall_profile_plugin_matrix_integration_test：通过。
3. ctest --test-dir build-ci -N | grep -E "Profile(RuntimePolicySchemaContractTest|PluginMatrixIntegrationTest)"：通过，发现 2 个相关用例。
4. ctest --test-dir build-ci --output-on-failure -R "ProfileRuntimePolicySchemaContractTest|ProfilePluginMatrixIntegrationTest"：通过，2/2 tests passed。

## 8. 结论

1. PLG-TODO-013 已完成，profile runtime policy 资产现在正式承载 `infra.plugin.*` 治理配置域，三档执行 profile 的行为矩阵也具备稳定的 typed config 验证。
2. 本轮把原本隐含的 schema blocker 显式清除，并用 contract + integration 双重测试把结构与值语义一起冻结，为后续 plugin policy/compatibility 子任务提供真实 profile 基线。