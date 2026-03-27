# Infra Integration Topology (Single Source of Truth)

状态：Active
Owner：测试平台组 + Infra PMO
关联任务：INF-PLAT-INT-001

## 1. 目的

本文件是基础设施域 integration 拓扑与标签规范的单一真相来源（SSOT），用于消除跨组件重复描述与口径分叉。

## 2. 顶层接线要求

1. 顶层测试入口必须在 `tests/CMakeLists.txt` 中接入 `add_subdirectory(integration)`。
2. integration 用例必须可被 `ctest -N` 发现。
3. integration 用例命名应包含组件前缀，格式建议：`<Component><Scenario>IntegrationTest`。

## 3. 标签规范

1. 必选标签：`integration`
2. 可选标签：`failure`、`profile`、`slow`
3. 组合规则：
- 失败注入集成测试：`integration;failure`
- profile 差异集成测试：`integration;profile`

## 4. 最小验证命令

```bash
cmake -S . -B build-ci -G Ninja
ctest --test-dir build-ci -N
ctest --test-dir build-ci --output-on-failure -L integration
```

## 5. 准入门禁

1. 若 `ctest -N` 未发现任何 `integration` 用例，则禁止推进 Phase 2/3 任务。
2. 若新增组件进入核心链路，必须补齐至少 1 个 integration smoke 用例。
3. 变更标签规则时，必须同步更新本文件与 `scripts/ci/infra_gate.sh`。
