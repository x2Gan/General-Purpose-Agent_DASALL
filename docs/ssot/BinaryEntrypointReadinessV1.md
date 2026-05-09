# BinaryEntrypointReadinessV1 (Single Source of Truth)

状态：Frozen
Owner：runtime / access / apps
关联任务：INTFIX-TODO-001
关联阻塞：INTFIX-BLK-01
关联 Gate：Gate-INT-01、Gate-INT-08、Gate-INT-10

## 1. 目标

本文件冻结 daemon / gateway 二进制入口层的 readiness 语义，避免继续把以下概念混写成同一个布尔值：

1. `accepted`
2. `degraded`
3. `stub-ready`
4. `default-ready`
5. `bridge-reachable`
6. `health-ready`

冻结目标不是新增第二套 runtime 状态机，而是给 app binary、release gate 与 startup diagnostics 提供统一的对外投影规则。

## 2. 适用范围与非范围

适用范围：

1. `runtime::AgentInitResult` 对 daemon / gateway entrypoint 的投影规则。
2. app binary 对 `AccessGateway`、runtime bridge、listener bind、health endpoint 的 readiness 判定。
3. `Gate-INT-08`、`Gate-INT-10` 与 packaging preflight 对二进制入口证据的采信边界。

不适用范围：

1. `AgentOrchestrator` 内部状态迁移。
2. streaming / async receipt / multi-agent readiness。
3. service、knowledge、llm 子域自己的健康探针实现细节。

## 3. 当前实现信号

截至 2026-05-09，仓库内与入口层语义直接相关的代码信号如下：

1. `runtime/include/AgentTypes.h` 中 `AgentInitResult::is_ready()` 当前只返回 `accepted`，尚未区分 `degraded`、`stub-ready` 与 `default-ready`。
2. `apps/daemon/src/main.cpp` 当前以 `runtime_init_result.is_ready()` 作为 daemon 启动放行条件，并直接把它投影成 `daemon_bridge_reachable`。
3. `apps/gateway/src/main.cpp` 当前尚未装配 `runtime_dispatch_backend`，但 health readiness 已直接消费 `gateway->is_ready()`。
4. `runtime/include/RuntimeDependencySet.h` 已经具备 `describe_readiness()`，可以区分 required / optional port 缺失与 `degraded`，但 app binary 入口还未把这些差异提升为独立外部语义。

这些信号证明：当前系统缺的不是更多布尔字段，而是 entrypoint 对现有状态的统一解释规则。

## 4. 术语冻结

### 4.1 readiness 术语表

| 术语 | owner | 定义 | 允许对外宣称的结论 | 不允许外推为 |
|---|---|---|---|---|
| `accepted` | runtime | `AgentFacade::init()` 或等价 app-local runtime composition 没有 fail-closed 拒绝，组合根已被接纳进入后续启动流程 | 入口初始化请求被接受 | default unary ready、release ready |
| `degraded` | runtime | required ports 满足，但 optional ports 或受控能力缺失，只允许在显式降级标签下继续运行 | limited unary service available | default-ready |
| `stub-ready` | apps/runtime | 入口能以 runtime-local stub、skeleton path 或 empty dependency set 启动，用于开发烟测或局部自检 | stub smoke 可运行 | production unary ready、release ready |
| `default-ready` | runtime + apps | default single-agent unary 所需 live ports、production runtime bridge 与 app-local required checks 全部满足 | production default unary ready | installed-package ready 以外的更高保证 |
| `bridge-reachable` | access/apps | app binary 到 runtime submit path 已有真实 backend / bridge 可调用，且不是 mock-only / empty pipeline | submit bridge reachable | default-ready、health-ready |
| `health-ready` | apps/access | 对外 health/readiness probe 可以宣称 ready，且其判定规则已经绑定真实 unary entrypoint，而不是 ping/liveness 占位 | readiness probe ready | default-ready 以外的隐藏语义 |

### 4.2 关系约束

1. `accepted=false` 时，其余所有 ready 派生语义都必须视为 false。
2. `degraded=true` 与 `accepted=true` 可以同时成立，但此时只能宣称 degraded unary ready，不能宣称 `default-ready`。
3. `stub-ready=true` 只能表达“入口壳层与 runtime-local skeleton 可启动”，不得被 gate、deliverable 或 review 外推为 production 默认链路 ready。
4. `bridge-reachable=true` 只说明 submit handoff 可达，不代表 upstream listener、AccessGateway、health probe 或 default unary 必然 ready。
5. `health-ready=true` 的前提必须包含 `bridge-reachable=true`，并且 health 规则不能绕过 `accepted` / `degraded` / `default-ready` 的 owner 语义。

## 5. app binary 投影规则

### 5.1 daemon projection rule

daemon entrypoint 必须按以下顺序投影 readiness：

1. `accepted`：来自 `AgentInitResult.accepted`。
2. `degraded`：来自 `AgentInitResult.degraded` 或等价 runtime readiness summary。
3. `stub-ready`：仅当 daemon 通过 runtime-local stub / skeleton path 启动，且 dependency set 不满足 live unary 要求时成立。
4. `bridge-reachable`：仅当 `runtime_dispatch_backend` 已连接到真实 runtime facade dispatch path，且该 path 不是 test-only fake / empty lambda 时成立。
5. `health-ready`：仅当 listener bind 成功、`AccessGateway` ready、`bridge-reachable=true`，并且当前对外暴露的状态未把 `stub-ready` 冒充为 `default-ready` 时成立。
6. `default-ready`：仅当 `accepted=true`、`degraded=false`、runtime dependency graph 满足 default unary live port 规则、且 daemon listener / gateway / bridge 都为 production path 时成立。

### 5.2 gateway projection rule

gateway entrypoint 必须按以下顺序投影 readiness：

1. `accepted`：来自 gateway production composition root 完成，且 `AccessGateway` init 未 fail-closed。
2. `bridge-reachable`：仅当 `GatewayAccessPipelineOptions::runtime_dispatch_backend` 已装配真实 runtime backend 时成立。
3. `health-ready`：`/health/ready` 只能在 `accepted=true` 且 `bridge-reachable=true` 的情况下返回 ready；缺 backend 的 gateway 只能 fail-closed，不能继续暴露 ready。
4. `stub-ready`：若 gateway 仅凭空壳 `AccessGateway`、mock pipeline 或 ping-only health 启动，只能记为 `stub-ready` 或 not-ready，不能记为 production ready。
5. `default-ready`：在 gateway binary 中仅当 production submit pipeline、runtime backend、AccessGateway readiness 与 default unary runtime readiness 同时满足时成立。

## 6. Gate 采信规则

1. `Gate-INT-08` 只负责 Access focused integration 与 production ingress 语义，不覆盖 app-binary / packaging preflight 的二进制入口结论。
2. `Gate-INT-10` 是 `default-ready`、`bridge-reachable`、`health-ready` 在 app binary / release preflight 层的正式 gate owner。
3. `stub-ready` 只能作为开发烟测、局部 bootstrap 或 blocker 复现证据；不得写入 release-ready、package-ready 或 production default unary ready 结论。
4. `accepted` 与 `bridge-reachable` 都不是 release 结论；二者必须与 `default-ready`、`health-ready` 分层记录。

## 7. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| `AgentInitResult` 不再只靠 `accepted` 暴露 entrypoint ready | `INTFIX-TODO-007` | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp`、`apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` | `AgentInitResultReadinessTest`、`RuntimeRequiredOptionalPortsIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06"])` |
| daemon/gateway 必须显式区分 `stub-ready` / `bridge-reachable` / `health-ready` | `INTFIX-TODO-005`、`INTFIX-TODO-006`、`INTFIX-TODO-011` | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、binary smoke tests | `DaemonBinaryUnarySmokeTest`、`GatewayBinaryUnarySmokeTest`、startup diagnostics tests | `Build_CMakeTools(buildTargets=["dasall_packaging_preflight_tests","dasall_access_gateway_binary_unary_smoke_integration_test"])` |
| release 证据必须单列 app-binary / preflight gate | `INTFIX-TODO-003`、`INTFIX-TODO-010` | `docs/ssot/SystemIntegrationGateMatrix.md`、`tests/CMakeLists.txt` | Gate discoverability / release preflight focused checks | `Build_CMakeTools(buildTargets=["dasall_gate_int_10","dasall_packaging_preflight_tests"])` |

## 8. 验证锚点

```bash
rg -n "stub-ready|default-ready|bridge-reachable|health-ready|BinaryEntrypointReadiness" \
  docs/ssot/BinaryEntrypointReadinessV1.md \
  docs/ssot/SystemIntegrationGateMatrix.md \
  docs/todos/integration/DASALL_系统集成修复补充优化专项TODO-2026-05-09.md
```

## 9. 结论

1. `accepted` 是 boot-time accept/reject 结果，不再等价于 default unary ready。
2. `degraded`、`stub-ready`、`default-ready`、`bridge-reachable`、`health-ready` 各自有独立 owner、输出语义和 gate 归属。
3. app binary 入口层以后只能投影这些语义，不能再用单一 `is_ready()` 混写二进制启动、bridge reachability、release gate 与 health probe 结论。
