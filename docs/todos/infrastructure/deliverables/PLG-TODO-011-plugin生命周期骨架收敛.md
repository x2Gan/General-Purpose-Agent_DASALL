# PLG-TODO-011 plugin 生命周期骨架收敛

日期：2026-04-07
任务：PLG-TODO-011
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 将 PLG-TODO-011 定义为“新增 PluginLifecycleManager 状态机与转移测试”，完成判定是 load/unload/enable/disable 四个方法的状态转移可预测，且失败路径可审计。
2. docs/architecture/DASALL_infra_plugin模块详细设计.md 6.2/6.3/6.7/6.8 要求 PluginLifecycleManager 执行 load/enable/disable/unload 生命周期与资源回收，输出可重放的 LoadResult/UnloadResult，并在连续失败后触发 plugin_safe_mode。
3. 当前仓库已完成 PLG-TODO-005 与 PLG-TODO-006，具备 PluginValidationPipeline、PluginAuditAdapter、PluginLoadResult/PluginUnloadResult/ActivePluginSet 等冻结边界；受 PLG-BLK-04 影响的真实 PluginRuntimeBridge 平台接入仍未完成，但 TODO Q2 已明确 skeleton 阶段可用 mock bridge 或等价回调绕过。

## 2. 研究学习结果

### 2.1 本地证据

1. infra/include/plugin/IPluginManager.h 已冻结 PluginOperationPhase、PluginLoadResult、PluginUnloadResult 与 ActivePluginSet，说明 011 不缺公共输出边界，真正缺口是生命周期状态机本体尚未落盘。
2. infra/src/plugin/PluginManager.cpp 在本轮前对 load/unload 仍返回统一 skeleton failure，导致 011 的根因不是接口未定义，而是没有把状态推进、失败清理与 active set 维护收敛到专门组件。
3. infra/src/plugin/PluginAuditAdapter.h/.cpp 已完成 load/unload 审计动作映射，可直接作为 011 失败路径的观测出口，而不必在 LifecycleManager 内重复拼装 AuditEvent。
4. docs/todos/infrastructure/DASALL_infrastructure_plugin组件专项TODO.md 的 Q2 已明确：PluginRuntimeBridge 缺失不阻塞当前骨架任务，允许以 mock bridge 或可注入回调验证状态机逻辑。

### 2.2 外部参考

1. PF4J 的 plugin lifecycle 文档强调插件应经过预定义状态集合管理，只有处于已启动状态的插件才应被视为可贡献运行能力；这支持本轮将 Loaded、Active、Disabled 明确区分，而不是把成功 load 直接等同于激活完成。
2. 同一参考还指出插件消费者应保留对 load、unload、enable、disable 的显式控制；这直接支撑 011 采用独立的 enable()/disable() 转移，而不是把所有生命周期变化隐藏在 load()/unload() 的副作用里。

### 2.3 可落地启发

1. 生命周期骨架不需要等待真实 PluginRuntimeBridge，只要把 runtime 交互边界收敛为可注入回调，就能先把状态转移、失败清理与 safe_mode 阈值跑通。
2. ActivePluginSet 的一致性依赖 governance-ready PluginDescriptor；因此即便是 skeleton，也必须生成非 unknown 的 version/abi/trust/source 占位信息，避免状态机条目在 active set gate 下失效。
3. 失败路径的“可审计”可以通过复用 PluginAuditAdapter 落地到 load/unload 动作，不应把审计 schema 再次内联到 LifecycleManager。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 冻结 PluginLifecycleManager 的最小职责边界 | plugin 详细设计 6.2/6.3/6.7/6.8 | infra/src/plugin/PluginLifecycleManager.h/.cpp | 只管理状态转移、失败计数、safe_mode 与 active set，不进入真实平台动态装载细节 |
| D2 | 用可注入 runtime 回调替代未冻结的 PluginRuntimeBridge | TODO Q2 与 PLG-BLK-04 | PluginRuntimeLoadCallback / PluginRuntimeUnloadCallback | 011 可编译可测，后续平台接入只需替换回调来源 |
| D3 | 把 public manager 的 load/unload/list_active 接到生命周期骨架 | PluginManager.cpp 现状 | PluginManager.cpp | 公开方法不再返回统一 skeleton failure |
| D4 | 锁定 011 的 Build 三件套 | plugin 专项 TODO 011 | 本交付物 + TODO 回写 | 有代码目标、测试目标、验收命令 |

## 4. D Gate 结论

### 4.1 Blocker 修复与 Design -> Build 映射

阻塞结论：

1. PluginManager.load()/unload() 仍停留在统一 skeleton failure，导致即使 LoadResult/UnloadResult 已冻结，也没有任何状态机骨架可供 011 落地或验证。
2. 真实 PluginRuntimeBridge 约定尚未冻结，若直接引入 platform 细节，会把 011 从 L2 skeleton 拉进后续平台实现阻塞。
3. ActivePluginSet 的一致性检查要求条目是 governance-ready；若生命周期骨架只写入 plugin_id/status 而保留其他字段为 unknown，则 unit gate 无法通过。

最小 blocker-fix：

1. 新增 PluginLifecycleManager，并把 runtime 交互下沉为 `PluginRuntimeLoadCallback` / `PluginRuntimeUnloadCallback`，以回调形式代替未冻结的 PluginRuntimeBridge。
2. 在 PluginManager.cpp 中把 load/unload/list_active 委托给持久化的 PluginLifecycleManager，而不是继续返回统一 skeleton error。
3. 让生命周期骨架生成 governance-ready 的最小 PluginDescriptor 占位信息，并在单测中验证 Loaded->Active、Loaded->Disabled、failed->cleanup 和 safe_mode 行为。

Design -> Build 映射：

| Design 结论 | Build 落地 |
|---|---|
| 生命周期状态机只管理状态与失败计数 | infra/src/plugin/PluginLifecycleManager.h/.cpp |
| runtime bridge 缺口通过可注入回调隔离 | PluginLifecycleManager.h/.cpp |
| public manager load/unload/list_active 必须接入生命周期骨架 | infra/src/plugin/PluginManager.cpp |
| 失败路径可审计需复用已有 PluginAuditAdapter | PluginLifecycleManager.cpp + PluginLifecycleStateTest.cpp |
| 公共 LoadResult/UnloadResult 边界不得回归 | PluginManagerBoundaryContractTest |

### 4.2 Build 三件套

1. 代码目标：infra/src/plugin/PluginLifecycleManager.h、infra/src/plugin/PluginLifecycleManager.cpp、infra/src/plugin/PluginManager.cpp、infra/CMakeLists.txt、tests/unit/infra/plugin/CMakeLists.txt。
2. 测试目标：
   - tests/unit/infra/plugin/PluginLifecycleStateTest.cpp
   - tests/contract/smoke/PluginManagerBoundaryContractTest.cpp（复用既有合约门验证 LoadResult/UnloadResult 边界稳定）
3. 验收命令：
   - cmake -S . -B build-ci -G "Unix Makefiles"
   - cmake --build build-ci --target dasall_infra dasall_plugin_lifecycle_state_unit_test dasall_contract_plugin_manager_boundary_test
   - ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest|PluginManagerBoundaryContractTest"

### 4.3 D Gate

结论：PASS。

理由：

1. 011 的改动严格停留在私有生命周期骨架、public manager 接线与 unit/contract 证据，不进入真实 dlopen/dlsym、平台句柄管理或 runtime 恢复决策。
2. 关键状态转移、失败清理、safe_mode 阈值和失败路径审计都具备明确的二值验证出口，且不破坏任何 plugin 公共接口。

## 5. Build 落地结果

1. 新增 infra/src/plugin/PluginLifecycleManager.h 与 PluginLifecycleManager.cpp，引入 PluginLifecycleTransitionResult、PluginRuntimeLoadResult、PluginRuntimeUnloadResult、可注入 runtime callbacks、managed plugin 集合、safe_mode 阈值计数以及 load/unload/enable/disable 状态转移逻辑。
2. 更新 infra/src/plugin/PluginManager.cpp，使 load()/unload()/list_active() 统一委托 PluginLifecycleManager，而不是返回统一 skeleton failure。
3. 更新 infra/CMakeLists.txt，把 PluginLifecycleManager.h/.cpp 纳入 plugin 私有源/头清单。
4. 更新 tests/unit/infra/plugin/CMakeLists.txt，注册 `dasall_plugin_lifecycle_state_unit_test`。
5. 新增 tests/unit/infra/plugin/PluginLifecycleStateTest.cpp，覆盖 Loaded->Active、Loaded->Disabled->Unloaded、failed load cleanup + safe_mode、带 PluginAuditAdapter 的 failed unload 审计四类路径。

## 6. Build 合规复核

1. 边界：本轮没有新增公共 plugin 接口；RuntimeBridge 缺口仅以私有 callbacks 占位，仍保持 platform 细节不进入 infra/plugin 公开边界。
2. 根因处理：修复的是生命周期骨架与 active set 维护缺失的根因，而不是在 PluginManager 中继续堆叠另一层临时 skeleton error。
3. 测试出口：unit 覆盖关键状态转移、失败清理和 safe_mode；contract 复用既有 PluginManagerBoundaryContractTest，证明 LoadResult/UnloadResult 公共边界未回归。
4. 兼容性：IPluginManager、PluginLoadResult、PluginUnloadResult、ActivePluginSet 的签名和 contracts 映射均未变；后续真实 PluginRuntimeBridge 只需替换回调来源即可接入。

## 7. 验证结果

1. cmake -S . -B build-ci -G "Unix Makefiles"：通过。
2. cmake --build build-ci --target dasall_infra dasall_plugin_lifecycle_state_unit_test dasall_contract_plugin_manager_boundary_test：通过。
3. ctest --test-dir build-ci --output-on-failure -R "PluginLifecycleStateTest|PluginManagerBoundaryContractTest"：通过，2/2 tests passed。

## 8. 结论

1. PLG-TODO-011 已完成，plugin 生命周期现在具备最小可执行状态机骨架，能够稳定表达 Loaded、Active、Disabled、Unloaded 与 failed->cleanup / safe_mode 行为。
2. 本轮把 runtime bridge 缺口限制在私有可注入回调内，并复用 006 的 PluginAuditAdapter 打通失败路径的审计出口，为后续 012 的失败注入与真实平台桥接提供了稳定基线。