# FULLINT-TODO-003 runtime / cognition / memory / llm 主链证据包

日期：2026-05-11  
来源任务：FULLINT-TODO-003  
范围：runtime single-agent unary、memory context assembly、cognition decision boundary、LLM production generation、checkpoint / persistence evidence

## 1. 结论

本轮证据包结论：当前 installed-package `dasall run` 已能证明 runtime -> memory context -> production `ILLMManager` -> checkpoint / session persistence -> `AgentResult` 的 L4 主功能路径不是空响应，也没有使用 `agent.dataset` fallback；但当前 production LLM direct path 并不会在该 installed run 中执行 cognition `decide()`，所以 cognition decision / reflection / belief writeback 只能记录为 L2/source evidence，不能被本轮 L4 installed run 冒充为已证明。

本任务完成的是证据包冻结，不是把 cognition installed-package 路径补齐为 GA 结论。后续若要把 cognition decision 纳入 installed-package 主功能证据，必须新增触发 cognition action / reflection path 的安装态正向场景或调整 runtime production path，并补实际 package smoke。

## 2. 本轮真实运行证据

### 2.1 installed-package run

命令：

```text
sudo -n dasall run '{"prompt":"请用LLM回答：3+4等于几？只给出简短答案。"}' --json --timeout-ms 120000
```

结果摘要：

1. `disposition=completed`
2. `task_completed=true`
3. `response_text` 首行包含 `llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop`
4. 响应正文为 `7`
5. `/tmp/fullint003-run.json` 未出现 `agent.dataset`

### 2.2 daemon runtime readiness 日志

命令：

```text
sudo -n journalctl -u dasall-daemon.service -n 80 --no-pager
```

结果摘要：

1. 最近 daemon 启动日志多次出现 `[dasall-daemon] runtime readiness=degraded-ready`。
2. 因此本轮只能把 `run` 成功写成 installed LLM 主功能可用，不能外推为 `default-ready` 或 production readiness 全闭环。

## 3. 当前源码主链证据

### 3.1 Runtime live dependency composition

`apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 当前 `compose_minimal_live_dependency_set()` 实际装配：

1. `memory::create_memory_manager(memory_config)`，且 `memory_config.storage.backend = memory::StorageBackend::Memory`。
2. `llm::create_production_llm_manager(*policy_snapshot)`，失败则 composition failed。
3. `cognition::create_cognition_engine(*policy_snapshot, {.llm_manager = dependency_set->llm_manager, .policy_snapshot = policy_snapshot})`。
4. `cognition::create_response_builder(...)`。
5. `ToolManager` 与 `visible_tools = {"agent.dataset"}`。
6. `external_evidence` 写入 `runtime:<owner>:required-live-baseline`，这是 `AgentOrchestrator` 识别 production LLM direct path 的条件之一。

结论：installed daemon 的 live dependency set 不再是空桩；memory、LLM、cognition、response builder、tools 都有真实 composition root。但 composition root 存在不等于每次 installed `run` 都执行每个组件的主方法。

### 3.2 Runtime / memory / optional knowledge

`runtime/src/AgentOrchestrator.cpp` 当前 `make_memory_context_request()`：

1. 将 request / session / stage / goal / runtime budget 映射成 `memory::MemoryContextRequest`。
2. 将 `composition.dependency_set->visible_tools` 投影进 memory context request；若为空才补 `agent.dataset`。
3. 将 `composition.dependency_set->external_evidence` 投影进 context request。
4. 若 `knowledge_service != nullptr`，调用 `knowledge_service->retrieve(make_knowledge_query(...))` 并追加 context projection 与 retrieval evidence refs。

结论：当前 installed production LLM path 在发起 LLM 前必须先调用 `memory_manager->prepare_context()`，且 optional knowledge 只在 dependency 存在时参与。缺失 knowledge 不会被本轮 silently 伪装成 package-ready。

### 3.3 Runtime / LLM production direct path

`AgentOrchestrator::run_once()` 当前在 `has_live_unary_ports(composition_) && has_production_llm_direct_path(composition_)` 成立时进入 production LLM direct path：

1. 调用 `memory_manager->prepare_context(make_memory_context_request(...))`。
2. 若 context assembly 返回 failure 或 degraded，直接失败为 `runtime llm path could not assemble a non-degraded context packet`。
3. 进入 `RuntimeState::Reasoning` 后调用 `llm_manager->generate(make_runtime_response_llm_request(...))`。
4. 若 LLM 无 response 或 route 为空，失败文案为 `runtime llm request failed; agent.dataset fallback is disabled`。
5. LLM 成功后进入 `Responding`，并记录 `production run completed through ILLMManager response path`。
6. Tool round 明确记录 `skipped because production llm response path does not use agent.dataset`。
7. Terminalize 阶段保存 checkpoint，tags 包含 `path=llm`、`origin=ILLMManager`，再持久化 session，最终 `make_result(..., make_llm_response_text(llm_result), ...)`。

结论：本轮 installed `run` 与代码路径一致：它证明 LLM direct path 和 memory context guard 有效，但它刻意跳过 `agent.dataset` tool round。

### 3.4 Cognition path 当前证据边界

当前源码同时存在非 direct LLM 的 live unary path：

1. `ICognitionEngine::decide(const CognitionStepRequest&)` 和 `reflect(const ReflectionRequest&)` 是 cognition public seam。
2. `AgentOrchestrator` 在非 production direct LLM path 中会调用 `cognition_engine->decide(...)`，并处理 context reload、conflicting decision、belief update hint writeback。
3. 当 `belief_update_hint` 可写回时，会调用 `memory_manager->write_back(make_belief_writeback_request(...))`，并把结果记录为 best-effort / partial / committed。

结论：cognition decision 和 belief writeback 的源码边界存在，但本轮 installed `dasall run` 进入的是 production LLM direct path，没有实际执行 cognition `decide()`。因此 cognition 在本证据包中标为 L2/source evidence + L4 not proven。

## 4. 证据矩阵

| Segment | 当前最高实证层 | 本轮证据 | 结论 | 残余缺口 |
|---|---:|---|---|---|
| Runtime request / FSM / terminalize | L4 | installed `run` completed；daemon service active；源码中 preflight -> reasoning -> responding -> auditing -> persisting -> completed | runtime 主控不是空响应 | readiness 日志仍是 `degraded-ready`，不能外推 default-ready |
| Memory context assembly | L4/source-correlated | `run_once()` direct path 在 LLM 前强制 `memory_manager->prepare_context()`；context degraded/failure 会 fail | installed run 成功间接证明 context packet 未失败 / 未 degraded | memory installed state persistence 未证明，留给 FULLINT-TODO-015 |
| Optional knowledge evidence | L2/source only | `make_memory_context_request()` 只在 `knowledge_service != nullptr` 时 retrieve | 缺 knowledge 不会被 silent 当作 package-ready | installed knowledge retrieve/refresh/health 缺口留给 FULLINT-TODO-014 |
| LLM generation | L4 | installed `run` 返回 `llm.origin=deepseek-prod/deepseek-reasoner model=deepseek-v4-flash finish_reason=stop` | production `ILLMManager` path 可用 | 外部 provider 抖动与 CI secret/testbed 策略仍需 FULLINT-TODO-019 |
| Tool / `agent.dataset` fallback | L4 negative | run output 未出现 `agent.dataset`；源码失败文案禁用 fallback | 本轮没有 dataset 假绿 | tools runtime production caller 仍需 FULLINT-TODO-016 |
| Cognition decide / reflection | L2/source only | `ICognitionEngine` seam、composition root、non-direct path 源码存在 | cognition 不应被算作 L4 installed run 已证明 | 需要安装态 cognition-positive scenario 或 focused evidence package 后续补强 |
| Memory writeback / belief hint | L2/source only | `AgentOrchestrator` 对 `belief_update_hint` 调用 `memory_manager->write_back()` | writeback seam 存在 | installed package persistence / writeback continuity 未证明 |
| Checkpoint / session persistence | L4/source-correlated | direct LLM path保存 checkpoint，tags `path=llm` / `origin=ILLMManager`，再 `persist_terminal_session()` | completed run 经过 terminalize code path | checkpoint artifact 不对 CLI 暴露，后续 recovery 需 FULLINT-TODO-011 |

## 5. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 后继任务 |
|---|---|---|
| installed single-agent run 成功必须以 `llm.origin` 为准 | `sudo -n dasall run ... --json --timeout-ms 120000` | FULLINT-TODO-013、019 |
| memory context 是 production LLM direct path 的前置 guard | `AgentOrchestrator::run_once()` direct path + installed completed result | FULLINT-TODO-015 |
| cognition composition root 不等于 installed run 已执行 cognition decision | `RuntimeLiveDependencyComposition.cpp` + `AgentOrchestrator` direct / non-direct path split | FULLINT-TODO-012、016 |
| readiness 成功与 default-ready 必须分开 | installed run completed + journal `runtime readiness=degraded-ready` | FULLINT-TODO-007 |

## 6. 验收命令

```text
sudo -n dasall run '{"prompt":"请用LLM回答：3+4等于几？只给出简短答案。"}' --json --timeout-ms 120000
sudo -n journalctl -u dasall-daemon.service -n 80 --no-pager
rg -n "has_production_llm_direct_path|prepare_context|llm_manager->generate|agent.dataset fallback is disabled|build_and_save_checkpoint|cognition_engine->decide|write_back" runtime/src/AgentOrchestrator.cpp apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp cognition/include/ICognitionEngine.h docs/todos/integration/deliverables/FULLINT-TODO-003-runtime-cognition-memory-llm主链证据包.md
```

## 7. Gate 判定

| Gate | 判定 | 证据 |
|---|---|---|
| D Gate | PASS | runtime / memory / llm / cognition 的当前实证层级与不可外推边界已冻结 |
| B Gate | PASS | installed `run` 当轮完成；源码证据证明 direct LLM path 前置 memory context、禁用 dataset fallback、保存 checkpoint；cognition L4 未证明被显式记录为残余缺口 |

## 8. 残余风险

1. 当前 production LLM direct path 的成功不能证明 cognition decision / reflection 已在 installed package 中运行。
2. daemon 日志仍显示 `degraded-ready`，FULLINT-TODO-007 前不得宣称 default-ready。
3. 本证据包不运行现有单测 / 集测作为验收依据；后续若需要 focused regression，可在 FULLINT-TODO-012 中单独执行并标注为 L2。
