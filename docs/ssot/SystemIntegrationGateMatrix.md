# SystemIntegrationGateMatrix (Single Source of Truth)

关联任务：INT-TODO-005  
关联阻塞：INT-BLK-05  
关联后续任务：INT-TODO-018、019、020、021、022、023、024、030

## 1. 目标

本文件冻结 DASALL 系统级 Gate 的分层规则、命令权威来源与 worklog 回写责任，避免继续出现以下偏差：

1. 用 subsystem smoke 或 runtime-local fixture gate 冒充系统 true integration gate。
2. 用历史绿灯、局部 ping/liveness、旧二进制或不可复验命令冒充系统 ready。
3. Gate 已变化但 TODO / worklog / 评审记录没有同步回写，导致结论漂移。

## 2. 证据分层规则

### 2.1 subsystem smoke

subsystem smoke 用于证明局部子系统或 facade 仍可运行，但不构成系统 ready 证据。

适用范围：

1. 单子系统 smoke。
2. 单模块 facade / provider / service baseline。
3. 仅验证某一层最小存活性的 focused smoke。

禁止外推：

1. 不得替代任何 `Gate-INT-*` true integration 结论。
2. 不得单独支撑 production ready、default unary ready 或 Access ingress ready。

### 2.2 fixture gate

fixture gate 用于证明某一条局部执行流或共享契约在受控夹具内成立。它可以作为实现前置验证，但不是系统 true integration gate 的替代品。

规则：

1. fixture gate 可以依赖 deterministic fixture、module-local fake、stub runtime 或 controlled test doubles。
2. fixture gate 必须明确标识自身是 fixture gate 或 runtime-local fixture gate，不能被命名为系统 Gate。
3. 若存在对应 true integration gate，fixture gate 只能作为前置或补充证据，不能覆盖 true integration 红灯。

典型边界：

1. `RuntimeUnaryFixtureIntegrationTest` 可以继续作为 runtime-local fixture gate。
2. 它不能替代 `Gate-INT-03` 对 `RuntimeUnaryIntegrationTest` / `CognitionRuntimeIntegrationTest` / `MainFlowContractE2ETest` 的要求。

### 2.3 true integration gate

true integration gate 是系统集成结论的正式通行门，必须满足以下条件：

1. 覆盖跨子系统真实调用链、共享 contracts 或系统级行为边界。
2. 测试名、CMake 注册与 targeted command 稳定可发现。
3. 失败时能明确指出是系统主链未闭合，而不是局部 fixture 问题。

### 2.4 discoverability / one-shot acceptance / worklog

discoverability、one-shot acceptance 与 worklog 不是附属文档工作，而是 `Gate-INT-09` 的组成部分。

规则：

1. `ctest -N` discoverability 负责保证系统 Gate 可被统一发现。
2. one-shot acceptance 命令负责定义当前阶段的正式验收入口。
3. worklog 负责把 Gate 状态、命令、结果、阻塞项与回退动作写回长期记录。

## 3. Gate 矩阵

| Gate | 证据层级 | 正式对象 | 正式命令 / 证据 | 通过标准 | 回写位置 | 回退动作 |
|---|---|---|---|---|---|---|
| `Gate-INT-01` | design / SSOT | 001~007、025、026 的系统级设计冻结 | 相关 SSOT / 详设 / TODO 的 `rg` 一致性命令 | 主链语义、shared surface、diagnostics contract、Access production path、Gate matrix、policy / recovery / health cadence 边界全部冻结 | integration TODO、worklog | 若任一 SSOT 缺失或口径冲突，则保持 blocker / task NotStarted，不提前进入 Build |
| `Gate-INT-02` | compile / contract | shared evidence surface 与 seam compile gate | compile / contract focused command | additive fields、shared projection、memory/runtime seam 可编译且 contract 不回退 | integration TODO、worklog | 若 compile / contract 红灯，则回退到 008~011，不得宣称 evidence path ready |
| `Gate-INT-03` | true integration | default unary 主 Gate | `RuntimeUnaryIntegrationTest`、`CognitionRuntimeIntegrationTest`、`MainFlowContractE2ETest` focused command | default unary 主链真实转绿，fixture gate 不再冒充系统主门 | integration TODO、worklog、review doc | 若仅 fixture gate 绿而 true integration 红，则保持系统未 ready |
| `Gate-INT-04` | true integration | structured evidence preservation gate | `RuntimeEvidenceProjectionIntegrationTest`、`KnowledgeEvidencePreservationTest` focused command | evidence ref / freshness / citation 在主链上可保留或明确降级 | integration TODO、worklog | 若只有知识或 memory 局部 smoke 绿，则不能替代此 Gate |
| `Gate-INT-05` | true integration | diagnostics retained snapshot gate | `InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest` focused command | retained snapshot round-trip 被系统 Gate 稳定守住 | integration TODO、worklog | 若仅 diagnostics 局部对象单测通过，仍不得宣称系统 diagnostics ready |
| `Gate-INT-06` | true integration | required/optional ports 与 degraded semantics gate | `RuntimeRequiredOptionalPortsIntegrationTest`、`RuntimeProfileCompatibilityTest`、必要时 `LLMSubsystemSmokeIntegrationTest` | required / optional / degraded / profile 兼容性在系统级行为上稳定可测 | integration TODO、worklog | 若只看到局部 port smoke，不得替代系统级 degraded 行为 Gate |
| `Gate-INT-07` | focused semantic gate | tools/services result semantics gate | `ServiceResultSemanticsContractTest`、`BuiltinExecutorLaneResultCodeTest`、smoke focused command | success / error / code 三元语义一致且无回退 | integration TODO、worklog | 若局部 smoke 绿但 contract 失败，保持语义未闭合 |
| `Gate-INT-08` | true integration | Access v1 production ingress gate | Access focused integration / contract command | Access production path、mock/test profile 与 production 证据分层清晰 | access TODO、integration TODO、worklog | mock pipeline、ping liveness、局部 envelope 字段不得冒充通过 |
| `Gate-INT-09` | operations / evidence | discoverability、one-shot acceptance、worklog evidence closure | `ctest -N` + focused one-shot acceptance + 文档回写 | Gate discoverability、统一 acceptance、TODO/worklog/review 回写同步完成 | integration TODO、worklog、review doc | 若命令不可发现或记录未回写，则 Gate 结论不能长期生效 |

## 4. 命令权威来源

### 4.1 当前阶段的正式验收命令

在 `INT-TODO-023` 完成之前，系统级正式验收命令以各任务行中定义的 targeted build / ctest / rg 命令为准；聚合 one-shot 命令仍属于待收口对象，不能反向覆盖任务行的 focused acceptance。

### 4.2 `INT-BLK-05` 下的执行规则

`INT-BLK-05` 已明确指出 aggregate build/test 容易被外部或无关问题污染，因此：

1. targeted build / ctest 是当前阶段的权威命令。
2. 若 aggregate 噪音存在，但 focused command 可稳定复现，则以 focused command 为正式验收依据。
3. 不得以 IDE 瞬时状态、preset 漂移、旧二进制缓存或口头结论替代命令证据。

### 4.3 `INT-TODO-023` 完成后的权威升级

当 `INT-TODO-023` 完成后，权威命令升级为：

1. `ctest -N` discoverability。
2. 系统 one-shot acceptance 命令。
3. 每个 `Gate-INT-*` 的 focused regex command。

## 5. worklog 与 TODO 回写责任

1. design / SSOT 任务：完成后必须回写 SSOT、相关 TODO 和 worklog，明确 Design -> Build 映射、阻塞项与后继任务。
2. code / gate 任务：只有在 focused command 通过后，才允许把任务标记为 Done 并写入 worklog。
3. 若 subsystem smoke 通过但 true integration gate 失败，worklog 必须明确记录“局部绿、系统红”的事实，不能只写通过的局部结果。
4. 每条 worklog 记录至少包含：任务号、Gate 或 blocker、正式命令、结果、回退动作或残余风险、下一步任务。
5. integration TODO 的 Gate / blocker 状态、worklog 记录与评审结论必须保持同一天口径一致；若三者不一致，以 focused command 最新结果回滚结论。

## 6. 禁止采信的伪证据

以下证据不得被用于系统 ready、production ready 或 Gate 通过结论：

1. 仅 subsystem smoke 通过。
2. 仅 fixture gate 通过。
3. 旧 build 目录中的历史绿色结果。
4. ping / liveness / health baseline 替代主业务链路。
5. 只更新 TODO 而未补 worklog 或 review 回写。
6. 未写出 targeted command、只写“人工验证通过”的结论。

## 7. Design -> Build 映射

1. `INT-TODO-018~022` 负责把 `Gate-INT-03~07` 从设计层收敛为可执行 Gate。
2. `INT-TODO-023` 负责把 discoverability 与 one-shot acceptance 固化为 `Gate-INT-09` 的正式命令层。
3. `INT-TODO-024` 负责将 Gate、交付物、review 与 worklog 形成长期证据闭环。
4. `INT-TODO-030` 负责将 Access v1 production ingress 证据分层纳入系统 Gate 口径。

## 8. 完成判定

当且仅当以下条件同时成立时，才允许认定系统级 Gate 矩阵与 evidence stratification 已冻结：

1. `Gate-INT-01~09` 均有唯一命名、层级说明、正式命令或前置任务。
2. subsystem smoke、fixture gate、true integration、discoverability/worklog 四层责任边界明确。
3. `INT-BLK-05` 下的命令权威来源明确，且 focused acceptance 优先级被正式写定。
4. TODO / worklog / review 的回写责任已经被定义为 Gate 的组成部分，而不是后置补文档动作。