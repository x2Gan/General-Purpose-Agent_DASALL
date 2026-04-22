# RT-TODO-003 RuntimeDependencySet 与相邻模块 seam 收敛

日期：2026-04-22  
任务：RT-TODO-003  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/runtime/DASALL_runtime子系统专项TODO.md](/home/gangan/DASALL/docs/todos/runtime/DASALL_runtime子系统专项TODO.md) 将 RT-TODO-003 定义为收敛 `RuntimeDependencySet`、`fail-closed stub`、`null adapter` 与相邻模块 public interface seam，用于解阻 RT-BLK-05。
2. [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.13 只给出了 Runtime 与相邻模块的调用面，6.24.12 只给出“组合根职责、注入对象清单、fail-closed stub 策略”的一句轻量说明，仍缺少唯一 seam 规则和依赖矩阵。
3. 8.3 与 blocker 章节已经明确 memory / tools / knowledge 的 runtime-facing public interface 尚未全部落位，因此本任务需要证明 runtime-local fixture gate 应该如何使用 stub/null adapter，而不是假设真实端口已经 ready。

## 2. 外部参考

1. AWS 的 Hexagonal Architecture 指南强调，应用编排逻辑应只通过 ports 与外部组件交互，外部实现通过 adapters 注入，业务编排不应感知底层技术细节。
2. 同一指南也明确，ports/adapters 结构的价值之一是独立测试和可替换性；这直接支持 RuntimeDependencySet 作为统一组合根持有 live adapter、null adapter 与 fail-closed stub 三类接缝实现，而不是把选择逻辑散落到 orchestrator 或测试代码里。

## 3. 设计结论

1. `RuntimeDependencySet` 是 runtime 唯一的依赖组合根，负责把相邻模块的 public interface、profile enablement 和 blocker 状态收敛成稳定的 runtime-owned dependency graph。
2. `null adapter` 只用于 profile 关闭能力或阶段默认禁用的子域，语义是“能力关闭但系统继续运行”；它不能冒充真实依赖可用。
3. `fail-closed stub` 只用于 public interface 未就绪但 runtime 需要验证自身控制平面时，语义是“依赖不可用且结果可审计”；它不能伪造 happy path。
4. 真实适配器只在相邻模块 public interface 已稳定且 blocker 解除后才能注入 true integration 路径。

## 4. seam 矩阵

| 依赖域 | 唯一接缝规则 | 运行时策略 |
|---|---|---|
| llm | 只经 `llm::ILLMManager` 或等价 runtime-facing seam 注入 | profile 禁用时 `null adapter`；接口未匹配时 fail-closed |
| cognition | 只经 cognition public interface 注入 | runtime-local gate 可用 fail-closed stub 验证主循环恢复语义 |
| memory | 只经 session/context/checkpoint public interface 注入 | 未解阻前必须 fail-closed；禁止直连 backend |
| tools | 只经 invoke seam 注入 | profile 关闭工具时 `null adapter`；接口未 ready 时 fail-closed |
| knowledge | 只经 retrieve seam 注入 | profile 关闭检索时 `null adapter`；接口未 ready 时 fail-closed |
| services | 只经 limited self-check/diagnose seam 注入 | 默认 `null adapter`，不得扩成执行主路径 |
| multi_agent | 只经 `MultiAgentGateway` 或等价 seam 注入 | 阶段 J 默认 `null adapter`，不宣称多 Agent ready |

## 5. 流程 / 时序

1. `AgentFacade::init()` 读取 `RuntimePolicySnapshot` 与 enabled_modules。
2. `RuntimeDependencySet` 按 profile 和 blocker 状态，为每个依赖域选择 live adapter、`null adapter` 或 `fail-closed stub`。
3. `AgentOrchestrator` 只消费已经装配完成的 seam，不自行判断“该不该直连真实依赖”。
4. runtime-local fixture gate 运行时，只允许通过 stub/null adapter 证明 runtime 自身控制平面成立；当 true integration gate 需要运行时，再切换到真实适配器。

## 6. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_runtime子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_runtime子系统详细设计.md) 的 6.13 与 6.24.12.1。
2. 本任务交付文档落于 [docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md](/home/gangan/DASALL/docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md)。
3. 后续 Build 落盘目标预留为 `runtime/include/RuntimeDependencySet.h`、`runtime/src/RuntimeDependencySet.cpp`、`tests/fixtures/runtime/` 下的 stub/null adapter 夹具。

## 7. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| RuntimeDependencySet 组合根职责 | `runtime/include/RuntimeDependencySet.h`、`runtime/src/RuntimeDependencySet.cpp` |
| live adapter / null adapter / fail-closed stub 三态选择 | `runtime/src/RuntimeDependencySet.cpp`、`tests/fixtures/runtime/` |
| runtime-local gate 与 true integration gate 的 seam 切换规则 | `tests/integration/agent_loop/`、`tests/fixtures/runtime/` |

## 8. Build 三件套

1. 代码目标：无；本任务只收敛 RuntimeDependencySet 和相邻模块 seam 设计，不修改 runtime 生产代码。
2. 测试目标：通过文档检索确认 `RuntimeDependencySet`、`fail-closed stub`、`null adapter`、`public interface` 已在 architecture/TODO/deliverable 三处形成一致口径。
3. 验收命令：
   - `rg -n "RuntimeDependencySet|fail-closed stub|null adapter|public interface" docs/architecture/DASALL_runtime子系统详细设计.md docs/todos/runtime/DASALL_runtime子系统专项TODO.md docs/todos/runtime/deliverables/RT-TODO-003-RuntimeDependencySet-seam收敛.md`

## 9. 风险与回退

1. 如果后续 RT-TODO-020/021 允许 `AgentOrchestrator` 直接判断并构造 stub/null adapter，会破坏组合根唯一性，应回退到 RuntimeDependencySet 统一装配口径。
2. 如果后续 true integration gate 仍复用 fail-closed stub 结果当作成功证据，会形成假阳性；必须回退到“fixture gate 与 true integration gate 分层”的规则。
3. 本任务完成后，RT-BLK-05 可视为解阻；但 memory / tools / knowledge 的真实 public interface 是否 ready，仍需在后续真集成任务中继续按 blocker 状态核实。