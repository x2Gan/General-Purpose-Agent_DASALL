# KNO-TODO-029 Profile Compatibility Integration 设计收敛

## 1. 目标

1. 使用真实 frozen profile 资产验证 KnowledgeConfigProjector 的 build/runtime 投影行为。
2. 锁定 `knowledge=false`、`knowledge=true && memory_vector=false`、`knowledge=true && memory_vector=true` 三种模式的兼容语义。
3. 证明 knowledge 子系统的 profile 行为来自统一投影规则，而不是 integration test 手写平行配置。

## 2. 边界与职责

### 2.1 本任务负责

1. 通过 `ProfileCatalog -> BuildProfileResolver -> RuntimePolicyProvider` 读取五个 frozen profiles：`desktop_full`、`cloud_full`、`edge_balanced`、`edge_minimal`、`factory_test`。
2. 通过 `KnowledgeConfigProjector::project(...)` 投影 KnowledgeConfigSnapshot。
3. 在 integration 层验证以下稳定规则：
   - `knowledge_enabled` 来自 build manifest 的 `knowledge` module 开关；
   - `vector_enabled` 来自 build manifest 的 `memory_vector` module 开关；
   - `knowledge=false` 时默认 mode 必须退化为 lexical-only；
   - `knowledge=true && memory_vector=false` 合法，且默认 mode 必须是 lexical-only；
   - `knowledge=true && memory_vector=true` 时默认 mode 允许进入 hybrid；
   - edge-like profile 与 stale/degrade 派生规则稳定。
4. 验证 resolved manifest + runtime snapshot 仍可通过共享 `ProfileCompatibilityValidator`。

### 2.2 本任务不负责

1. 不新增新的 frozen profile 资产。
2. 不手写 YAML 解析或平行构造 runtime policy。
3. 不执行 retrieval smoke、failure/degrade、quality regression；这些由 027/028/030 承担。

## 3. 数据与接口

### 3.1 输入资产

1. `profiles/<profile>/profile.cmake`
2. `profiles/<profile>/runtime_policy.yaml`

### 3.2 关键接口

1. `dasall::profiles::ProfileCatalog`
2. `dasall::profiles::BuildProfileResolver::resolve_build_manifest(...)`
3. `dasall::profiles::RuntimePolicyProvider::load_snapshot(...)`
4. `dasall::knowledge::config::KnowledgeConfigProjector::project(...)`
5. `dasall::profiles::ProfileCompatibilityValidator::validate(...)`

### 3.3 Synthetic compatibility 场景

当前五个 frozen profiles 中不存在 `knowledge=true && memory_vector=false` 组合；但专项 TODO 明确要求该组合保持合法。因此测试采用以下最小策略：

1. 从真实 `desktop_full` manifest 出发；
2. 仅移除 `memory_vector` module；
3. 保持 runtime snapshot 与其余 manifest 字段不变；
4. 验证 projector 产出 `knowledge_enabled=true`、`vector_enabled=false`、`retrieval_mode_default=LexicalOnly`，且结果结构仍一致。

该 synthetic case 只覆盖投影兼容规则，不替代真实 frozen profile 资产验证。

## 4. 流程

1. integration harness 读取目标 profile 的 real build/runtime assets。
2. resolver 基于 `enabled_modules.*` 生成 BuildProfileManifest。
3. provider 加载 RuntimePolicySnapshot。
4. projector 投影出 KnowledgeConfigSnapshot。
5. 断言真实矩阵：
   - profile id / target platform 解析正确；
   - enabled/vector/retrieval mode 符合 build 开关；
   - stale/degrade/timeout/evidence budget 等派生字段符合 runtime policy 规则；
   - shared profile validator 仍报告 Compatible。
6. 断言 synthetic downgrade：`knowledge=true && memory_vector=false` 不应导致投影失败。

## 5. 断言矩阵

### 5.1 真实 frozen profiles

1. `desktop_full` / `cloud_full` / `edge_balanced`
   - `knowledge_enabled=true`
   - `vector_enabled=true`
   - `retrieval_mode_default=Hybrid`
2. `edge_minimal` / `factory_test`
   - `knowledge_enabled=false`
   - `retrieval_mode_default=LexicalOnly`
3. 派生规则
   - `request_deadline_ms = clamp(max_latency_ms / 3, 300, 1500)`
   - `ingest_timeout_ms = 10000` for edge-like / `30000` otherwise
   - `allow_stale_read` 来自 capability cache policy
   - `allow_budget_degrade` 来自 degrade policy

### 5.2 Synthetic downgrade

1. `desktop_full` manifest 去掉 `memory_vector` 后：
   - `knowledge_enabled=true`
   - `vector_enabled=false`
   - `retrieval_mode_default=LexicalOnly`
   - `KnowledgeConfigSnapshot::has_consistent_values()` 仍为 true

## 6. 文件范围

1. 新增 `tests/integration/knowledge/KnowledgeProfileCompatibilityTest.cpp`
2. 更新 `tests/integration/knowledge/CMakeLists.txt`
3. 新增本设计收敛文档

## 7. 验证策略

1. 定向构建 `dasall_knowledge_profile_compatibility_integration_test`
2. 若 `RunCtest_CMakeTools` 继续报 `生成失败`，沿用仓库稳定回退链：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_knowledge_profile_compatibility_integration_test`
   - `ctest --test-dir build-ci -R dasall_knowledge_profile_compatibility_integration_test --output-on-failure`