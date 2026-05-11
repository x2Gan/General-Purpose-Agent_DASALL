# FULLINT-TODO-004 profile enablement 与 multi_agent 声明边界校准

日期：2026-05-11  
来源任务：FULLINT-TODO-004  
关联阻塞项：FULLINT-BLK-004  
范围：`desktop_full` / `cloud_full` profile source 声明、RuntimePolicySnapshot 投影边界、multi_agent placeholder 状态、installed package 旧资产证据

## 1. 结论

本轮完成 `FULLINT-BLK-004` 的 source 层最小解阻：在没有 `NullMultiAgentCoordinator` / `RealMultiAgentCoordinator` 和 Runtime 装配点之前，`desktop_full` 与 `cloud_full` 的 source profile 不再声明 `enabled_modules.multi_agent: true`，统一校准为禁用态。

当前 installed package `0.1.0-1` 仍包含旧版 `/usr/share/dasall/profiles/{desktop_full,cloud_full}/runtime_policy.yaml`，其中 `multi_agent: true` 仍存在；这只能作为“旧安装资产仍需 package rebuild/reinstall”的证据，不能再作为 source ready 结论。后续 `FULLINT-TODO-013` / `019` 必须在新包中复核安装态 profile 资产。

## 2. 实际 installed-package 证据

命令：

```text
dpkg -L dasall-common dasall | rg 'profiles/.*/runtime_policy.yaml|multi_agent|dasall$|bin/dasall' || true
dasall --help >/tmp/fullint004-help.txt 2>&1 || true
rg -n "multi[-_ ]agent|agent|run|profile|help|command" /tmp/fullint004-help.txt
rg -n "multi_agent:" /usr/share/dasall/profiles/desktop_full/runtime_policy.yaml /usr/share/dasall/profiles/cloud_full/runtime_policy.yaml profiles/desktop_full/runtime_policy.yaml profiles/cloud_full/runtime_policy.yaml
```

结果摘要：

1. installed package 暴露 `/usr/share/dasall/profiles/{desktop_full,cloud_full,edge_balanced,edge_minimal,factory_test}/runtime_policy.yaml`。
2. `dasall --help` 只显示 `help`、`config plan`、`run` 等控制面命令，没有 multi-agent 入口。
3. 本轮校准前，installed package 与 source 的 `desktop_full` / `cloud_full` 均为 `multi_agent: true`；source patch 后仅 source profile 已变为 `false`，installed 资产仍待下一次 package rebuild/reinstall 更新。

## 3. Source 事实

### 3.1 multi_agent 实现状态

当前 `multi_agent` 模块仍只有静态库骨架：

1. `multi_agent/CMakeLists.txt` 定义 `dasall_multi_agent` static library。
2. `multi_agent/src/placeholder.cpp` 仅包含 `keep_library_non_empty()`。
3. 当前未发现 `IMultiAgentCoordinator`、`NullMultiAgentCoordinator`、`RealMultiAgentCoordinator` 或 Runtime `AgentOrchestrator` multi-agent coordinator 注入点。

结论：不能把 profile enablement 宣称为 multi-agent runtime-ready。

### 3.2 profile source 校准

本轮将以下 source profile 从 enabled 校准为 disabled：

1. `profiles/desktop_full/runtime_policy.yaml`：`multi_agent: false`
2. `profiles/cloud_full/runtime_policy.yaml`：`multi_agent: false`

`edge_balanced`、`edge_minimal`、`factory_test` 原本已保持禁用态。

### 3.3 RuntimePolicySnapshot 边界

`profiles/src/RuntimePolicyProvider.cpp` 当前只把 runtime budget、model profile、token budget、prompt policy、capability cache、degrade、timeout、execution、ops、worker_threads 投影进 `RuntimePolicySnapshot`。

`profiles/include/RuntimePolicySnapshot.h` 当前没有 typed `enabled_modules.multi_agent` accessor；因此即使 build manifest 能解析 `enabled_modules`，runtime snapshot 也没有 multi-agent coordinator 装配依据。

结论：在 typed enablement 与 coordinator owner 未落地前，source profile 应保持禁用态。

## 4. 测试 / Gate

本轮通过 CMake Tools 构建以下目标：

```text
Build_CMakeTools(buildTargets=[
  "dasall_runtime_profile_compatibility_integration_test",
  "dasall_contract_profile_runtime_policy_schema_test",
  "dasall_profile_matrix_consistency_unit_test"
])
```

结果：`result code: 0`。

本轮通过 CMake Tools 运行以下测试：

```text
RunCtest_CMakeTools(tests=[
  "RuntimeProfileCompatibilityTest",
  "ProfileRuntimePolicySchemaContractTest",
  "ProfileMatrixConsistencyTest"
])
```

结果：3/3 passed。

测试含义：

1. `RuntimeProfileCompatibilityTest`：`desktop_full` 与 `cloud_full` build manifest 中的 `multi_agent` 均保持 disabled，profile snapshot 仍可加载并通过 compatibility validator。
2. `ProfileRuntimePolicySchemaContractTest`：profile schema freeze 明确要求 full profiles 在 coordinator wired 前保持 `multi_agent: false`。
3. `ProfileMatrixConsistencyTest`：profile matrix 基础一致性未被破坏。

这些测试用于保护 source profile 校准，不替代 installed package rebuild/reinstall 后的 L4 asset smoke。

## 5. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 后继任务 |
|---|---|---|
| MultiAgent 不得形成第二主循环，Runtime 仍是全局主控 | 维持 RuntimePolicySnapshot 无 typed multi-agent runtime enablement；profile source 禁用 `multi_agent` | FULLINT-TODO-018 |
| 没有 coordinator 装配前，full profiles 不宣称 multi_agent ready | `profiles/desktop_full/runtime_policy.yaml`、`profiles/cloud_full/runtime_policy.yaml` 改为 `multi_agent: false` | FULLINT-TODO-013、019 复核安装态资产 |
| 禁用态需要 Gate 保护 | `RuntimeProfileCompatibilityTest`、`ProfileRuntimePolicySchemaContractTest`、`ProfileMatrixConsistencyTest` | FULLINT-TODO-018 扩展 Null/Real coordinator Gate |

## 6. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | FULLINT-BLK-004 的 source 层最小解阻策略明确：禁用态优先，Null/Real coordinator 后续专项落地 |
| B Gate | PASS | source profile 已禁用，相关 profile/schema/matrix tests 通过；installed package 旧资产差异已记录为后续 package rebuild/reinstall 复核项 |

## 7. 残余风险

1. 当前 installed package `0.1.0-1` 仍含旧 profile 资产；本轮不宣称安装态 profile 已更新。
2. `multi_agent` 仍无 Null/Real coordinator、Observation 折叠和 Runtime owner 装配；后续由 `FULLINT-TODO-018` 负责。
3. RuntimePolicySnapshot 未暴露 typed `enabled_modules`，这在禁用态下可接受；若后续启用 multi_agent，必须先冻结 typed projection 与 compatibility Gate。
