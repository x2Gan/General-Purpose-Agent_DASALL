# LLM-TODO-038 llm 专项 Gate 与阶段 G-H 证据回写设计收敛

日期：2026-04-13
任务：LLM-TODO-038
状态：Done

## 1. 本地证据

1. [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md) 已把 038 定义为阶段 I 的统一证据回写 owner，并要求显式补齐 build / unit / contract / integration / risk / blocker 证据，同时对阶段 J 的 036/037 给出 Go/No-Go 结论。
2. [docs/todos/llm/deliverables/LLM-TODO-028-LLM-observability-bridges设计收敛.md](LLM-TODO-028-LLM-observability-bridges%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)、[docs/todos/llm/deliverables/LLM-TODO-029-LLM-smoke-integration设计收敛.md](LLM-TODO-029-LLM-smoke-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md)、[docs/todos/llm/deliverables/LLM-TODO-035-profile-diff-integration设计收敛.md](LLM-TODO-035-profile-diff-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 与 [docs/todos/llm/deliverables/LLM-TODO-042-asset-only-provider-onboarding-integration设计收敛.md](LLM-TODO-042-asset-only-provider-onboarding-integration%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) 已提供阶段 G/H 所需的 provider projection、observability、smoke、profile 与 asset-only onboarding 证据。
3. 本轮重新执行了 038 的验证基线：`ListBuildTargets_CMakeTools`、`ListTests_CMakeTools`、`Build_CMakeTools` 构建 `dasall_llm`、`dasall_unit_tests`、`dasall_contract_tests`、`dasall_integration_tests`。结果分别固定为：模块构建成功、unit `249/249` 通过、contract `152/152` 通过、integration `43/43` 通过。
4. [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 的 7.2、10.1 与 12.1 已冻结阶段 J 的边界：`ResolvedModelRoute`、`PromptPolicyDecision`、`StreamHandle` 在当前阶段继续保持 module-local/deferred，streaming 生命周期与 shared supporting object admission 不能因为阶段 G/H 测试全绿就被误判为可执行。
5. [docs/todos/contracts/DASALL_contracts验收整改TODO.md](../../contracts/DASALL_contracts%E9%AA%8C%E6%94%B6%E6%95%B4%E6%94%B9TODO.md) 仍将 `ModelRoute`、`StreamHandle`、`PromptPolicyDecision` 列为未完成的 contracts 缺口（T009/T010），说明 036/037 的真正解阻 owner 仍在 contracts supporting object / admission 基线，而不是 llm 自身的 unary 代码实现。
6. 本轮额外核对 `runtime/`、`apps/`、`cognition/` 与 `tools/` 中的 `stream_generate`、`IStreamObserver`、`StreamHandle`、`StreamSessionRef` 使用面，当前未发现稳定的下游 streaming 消费者证据，因此 036/037 也不存在“下游已成熟、只差 llm 动手”的现状。

## 2. 外部参考

1. 本轮未引入新的外部规范；038 的 owner 完全由 DASALL 既有 Gate 表、Blocker 表、contracts 差异矩阵与 llm 详细设计驱动。

## 3. Design 结论

1. 038 是证据与门禁收口任务，不新增 llm 生产逻辑；其目标是把阶段 A-H 的现有结果统一映射回 llm 专项 Gate、Blocker 与风险视图。
2. 阶段 I 当前可给出二值结论：`LLM-GATE-01` 到 `LLM-GATE-09` 已闭合，`LLM-GATE-10` 仍保持 Blocked。Gate 10 的阻塞不是测试不足，而是 036/037 的跨模块语义尚未冻结。
3. `LLM-BLK-008` 当前未实质触发。CMake Tools 本轮能够稳定返回 targets/tests，并完成四段验证；但显式 `cmake/ctest` 回退路径仍应保留在文档中，作为后续 IDE 工具态异常时的最低恢复方案。
4. 036 当前仍不具备执行条件。它虽然已经满足 “unary 主链稳定” 这一前置，但 `StreamHandle`/streaming 生命周期仍缺 shared baseline，且没有稳定跨模块消费者能反向约束取消、ownership、observer、backpressure 语义。当前最先需要完成的是 contracts 子系统的 `StreamHandle` 基线冻结（现有锚点为 T009 或等价 owner），随后再回 llm 阶段 J 执行 036。
5. 037 当前同样不具备执行条件。`ResolvedModelRoute`、`PromptPolicyDecision`、`StreamHandle` 的 shared admission 仍缺 contracts 侧的 consumers matrix、迁移窗口与 contract baseline；当前最先需要完成的是 contracts 子系统对 T009/T010 或等价 supporting object/admission owner 的收口，之后再回 llm 阶段 J 做 037 的 Go/No-Go 评审。
6. 因此，036/037 的解锁顺序不是“继续在 llm 内补代码”，而是“先完成 contracts supporting object / admission 基线，再回 llm 做阶段 J 后置评审”。runtime、apps、cognition、tools 当前都不是首要解阻 owner。

## 4. Gate 快照

| Gate | 当前状态 | 证据 |
|---|---|---|
| LLM-GATE-01 include/CMake 接线门 | Pass | 001/002/003 已完成，`ListBuildTargets_CMakeTools` 继续可见 `dasall_llm`、`dasall_unit_tests`、`dasall_integration_tests` |
| LLM-GATE-02 公共接口冻结门 | Pass | 005~011 已完成，`LLMInterfaceSurfaceTest` 持续可发现 |
| LLM-GATE-03 资产基线门 | Pass | 013/014 已完成，Prompt/Provider overlay 证据已固定 |
| LLM-GATE-04 Prompt 治理门 | Pass | 015/016/039/017/018/019 已完成，unit 聚合 `249/249` 通过 |
| LLM-GATE-05 unary 主链门 | Pass | 020/021/040/022/023/024 已完成，`dasall_llm` 构建成功 |
| LLM-GATE-06 adapter/observability 门 | Pass | 041、025、026、027、028 已完成，projection/observability 证据已闭合 |
| LLM-GATE-07 integration discoverability 门 | Pass | `ListTests_CMakeTools` 可见 llm integration 用例，029 smoke 已长期稳定 |
| LLM-GATE-08 contracts 不回退门 | Pass | contract 聚合 `152/152` 通过，未新增未经评审的 shared object |
| LLM-GATE-09 profile 对齐门 | Pass | 035 与 042 已完成，integration 聚合 `43/43` 通过 |
| LLM-GATE-10 streaming/shared admission 评审门 | Blocked | 036/037 仍受 `LLM-BLK-005/006` 约束，contracts supporting object/admission 基线未闭合 |

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| llm 专项 Gate/Blocker 快照 | [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md) |
| 038 设计与评估交付 | [docs/todos/llm/deliverables/LLM-TODO-038-llm专项Gate与阶段G-H证据回写设计收敛.md](LLM-TODO-038-llm%E4%B8%93%E9%A1%B9Gate%E4%B8%8E%E9%98%B6%E6%AE%B5G-H%E8%AF%81%E6%8D%AE%E5%9B%9E%E5%86%99%E8%AE%BE%E8%AE%A1%E6%94%B6%E6%95%9B.md) |
| 038 执行证据与 036/037 评估 | [docs/worklog/DASALL_开发执行记录.md](../../../worklog/DASALL_%E5%BC%80%E5%8F%91%E6%89%A7%E8%A1%8C%E8%AE%B0%E5%BD%95.md) |

## 6. Build 三件套

1. 代码目标：统一回写 llm 专项 Gate、阶段 G/H 验证结果、工具回退策略与 036/037 当前可执行性结论。
2. 测试目标：
   - `ListBuildTargets_CMakeTools` / `ListTests_CMakeTools` 证明 discoverability 正常
   - `Build_CMakeTools` 构建 `dasall_llm` 成功
   - `Build_CMakeTools` 构建 `dasall_unit_tests` 成功，unit 聚合 `249/249` 通过
   - `Build_CMakeTools` 构建 `dasall_contract_tests` 成功，contract 聚合 `152/152` 通过
   - `Build_CMakeTools` 构建 `dasall_integration_tests` 成功，integration 聚合 `43/43` 通过
3. 验收命令：
   - `ListBuildTargets_CMakeTools`
   - `ListTests_CMakeTools`
   - `Build_CMakeTools` 构建目标 `dasall_llm`
   - `Build_CMakeTools` 构建目标 `dasall_unit_tests`
   - `Build_CMakeTools` 构建目标 `dasall_contract_tests`
   - `Build_CMakeTools` 构建目标 `dasall_integration_tests`

## 7. 风险与回退

1. 若后续 VS Code CMake Tools 再次出现 targets/tests 为空、预设异常或 `DartConfiguration.tcl` 噪声扩大为实际失败，应回退到 [docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md) 中已记录的显式 `cmake -S . -B build-ci -G "Unix Makefiles"` 与 `ctest --test-dir build-ci ...` 验证路径。
2. 036 若在 contracts 基线未闭合时提前推进，会把 `StreamSessionRef`、取消语义或 backpressure 细节错误冻结成 llm 私有事实，后续 shared handle 迁移成本会被放大。
3. 037 若在 contracts admission baseline 未收敛时提前推进，会把 `ResolvedModelRoute`、`PromptPolicyDecision` 或 `StreamHandle` 误写入 shared contracts，形成与当前 consumers matrix 不一致的共享 ABI。