# RuntimeAppCompositionV1 (Single Source of Truth)

状态：Frozen
Owner：runtime / apps
关联任务：INTFIX-TODO-004
关联阻塞：INTFIX-BLK-03
关联 Gate：Gate-INT-01、Gate-INT-10

## 1. 目标

本文件冻结 daemon / gateway app binary 在 unary v1 场景下的 runtime production composition 规则，统一以下三类语义：

1. `RuntimeDependencySet` 作为 app composition root 的 owner 与输入边界。
2. live dependency composition、degraded unary 与 stub runtime path 的区分。
3. stub path 在开发烟测、binary smoke、release gate 中的允许范围。

本文件不替代 `SingleAgentRuntimePortMatrix` 的 required / optional 端口矩阵；它只说明 app binary 如何把该矩阵装配成 production composition。

## 2. 适用范围与非范围

适用范围：

1. `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp` 或其 app-local helper 如何构造 `RuntimeDependencySet`。
2. `runtime::AgentFacade::init()` 对 live / degraded / stub runtime path 的消费前提。
3. app binary 在 `default unary`、binary smoke 与 release-preflight 层对 runtime composition 的宣称边界。

不适用范围：

1. `RuntimeDependencySet` 内部每个端口的 required / optional 分类细节。
2. `AgentOrchestrator` 的主循环或恢复路径逻辑。
3. installed-package 验收和 packaging 脚本细节。

## 3. 当前代码信号

截至 2026-05-09，与 runtime app composition 直接相关的代码信号如下：

1. `apps/daemon/src/main.cpp` 的 `build_daemon_agent_init_request()` 当前直接创建空 `RuntimeDependencySet`，说明 daemon 仍可走 skeleton / stub runtime path。
2. `runtime/src/AgentFacade.cpp` 在缺少 cognition ports 且未提供相邻 live ports 时，会把 diagnostics 标记为 `cognition_ports=stub_runtime_path`，后续再附加 `readiness=stub_runtime_path`。
3. `runtime/include/RuntimeDependencySet.h` 已可通过 `describe_readiness()` 区分 required ports、optional ports 与 `degraded`，但 app binary 入口尚未把这套信息冻结成 production composition 规则。
4. `apps/gateway/src/main.cpp` 当前尚未建立 runtime init request，也没有显式 `RuntimeDependencySet` owner，这意味着 gateway binary 还不具备受控的 runtime production composition。

这些信号证明：当前系统缺的是 app binary 层的 runtime production composition 规则，而不是新的 runtime 端口字段。

## 4. app composition owner 规则

1. daemon / gateway 的 runtime production composition owner 固定为 app composition root，也就是各自 `main.cpp` 或共享的 app-local helper。
2. `RuntimeDependencySet` 只能由 app composition root 按 profile、install layout、相邻模块 public interface 可用性来构造；不得把装配决定权回流给 `AgentFacade`、`AccessGatewayFactory`、测试调用方或局部 lambda。
3. `AgentFacade::init()` 负责消费 composition 结果并返回 readiness / diagnostics；它不是 production composition owner，也不能以隐藏 fallback 替 app binary 补齐默认 live graph。
4. tests/fixtures 可以显式构造 fake / stub `RuntimeDependencySet`，但这些路径必须停留在测试命名与 fixture helper 内，不得伪装成 app binary 默认行为。

## 5. live / degraded / stub runtime 规则

### 5.1 live dependency composition

满足以下条件时，app binary 才能宣称自己持有 live dependency composition：

1. `RuntimeDependencySet` 中的 required runtime-facing seams 由 live adapter、runtime-owned `null adapter` 或受控 fail-closed seam 显式装配，而不是完全留空。
2. 若 `cognition_engine` / `response_builder` 不由 app 直接注入，app 仍必须提供足以让 `AgentFacade::init()` 通过 policy snapshot 组合出 cognition ports 的 live 输入，而不是依赖空集合走 stub runtime path。
3. `default unary` 所需 required ports 满足，optional ports 则按 `SingleAgentRuntimePortMatrix` 的规则进入 live 或 degraded 分支。

### 5.2 degraded unary

1. `degraded` 只允许表达“required ports 已具备、optional ports 缺失或受控关闭”的 live unary 变体。
2. `degraded` 不等于 stub runtime path；它仍然需要真实 `RuntimeDependencySet` production composition，只是不能宣称 `default unary` ready。
3. 是否允许 `degraded` 继续启动，由 profile / policy snapshot 明确裁定；app binary 不得自行把 optional 缺失路径默认放宽成 ready。

### 5.3 stub runtime path

以下场景允许使用 stub runtime path：

1. runtime-local 开发烟测。
2. 受控 binary smoke / blocker 复现，用于证明入口壳层与控制平面可启动。
3. tests/fixtures 显式命名的 stub runtime 组合根。

以下场景禁止把 stub runtime path 当成 production composition：

1. `default unary` ready 结论。
2. `Gate-INT-10` 的 app-binary / release-preflight 通过结论。
3. build-tree packaging preflight 与 installed-package ready 宣称。

使用 stub runtime path 时，必须满足：

1. diagnostics 中保留 `stub_runtime_path` 或等价标签。
2. 不得把该状态映射成 `default-ready` 或 production unary ready。
3. app binary 对外输出必须能区分 stub、degraded 与 live。

## 6. daemon / gateway 入口规则

### 6.1 daemon

1. daemon binary 可在本地控制平面、开发烟测或 blocker 复现中暂时使用 stub runtime path。
2. daemon 若继续使用空 `RuntimeDependencySet`，只能宣称 skeleton / stub smoke ready，不能宣称 `default unary` 或 release-preflight ready。
3. daemon production composition 的目标态必须是至少具备 required live ports 的 runtime graph，knowledge / llm 缺失时再按 `degraded` 语义处理。

### 6.2 gateway

1. gateway binary 不得以“尚未接线 runtime backend”为理由默认为 stub runtime path ready。
2. gateway 若没有显式 runtime init request 与 `RuntimeDependencySet` owner，就只能保持 fail-closed，不得把空壳 `AccessGateway` 或 health probe 当成 runtime production composition 已就绪。
3. gateway production path 只有在 app-local runtime bridge 与 `RuntimeDependencySet` 都进入 live composition 时，才允许参与 `default unary` / `Gate-INT-10` 结论。

## 7. 与相邻 SSOT 的关系

1. `SingleAgentRuntimePortMatrix` 定义 required / optional / degraded 的端口口径；本文件定义 app binary 如何把这些口径装配成 runtime production composition。
2. `BinaryEntrypointReadinessV1` 定义 accepted、degraded、stub-ready、default-ready 的 entrypoint 语义；本文件定义这些语义在 runtime composition 层的输入来源。
3. `GatewayBinaryProductionPathV1` 负责 gateway binary 的 access/runtime bridge owner；本文件负责 runtime side 的 dependency graph owner 与 stub/live 边界。

## 8. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| app binary 不再把空 `RuntimeDependencySet` 当成 production composition | `INTFIX-TODO-008` | `apps/daemon/src/main.cpp`、`apps/gateway/src/main.cpp`、必要时 runtime/app-local helper | `DaemonRuntimeLiveDependencyCompositionTest`、`GatewayRuntimeLiveDependencyCompositionTest` | `Build_CMakeTools(buildTargets=["dasall_runtime_unary_integration_test","dasall_access_daemon_runtime_live_dependency_composition_test","dasall_access_gateway_runtime_live_dependency_composition_test"])` |
| stub / degraded / default unary 语义必须由 runtime init 显式区分 | `INTFIX-TODO-007` | `runtime/include/AgentTypes.h`、`runtime/src/AgentFacade.cpp`、daemon/gateway entrypoint | `AgentInitResultReadinessTest`、`RuntimeRequiredOptionalPortsIntegrationTest` | `Build_CMakeTools(buildTargets=["dasall_runtime_agent_init_result_readiness_unit_test","dasall_gate_int_06"])` |

## 9. 验证锚点

```bash
rg -n "RuntimeAppComposition|stub runtime|RuntimeDependencySet|production composition|default unary" \
  docs/ssot/RuntimeAppCompositionV1.md \
  docs/architecture/DASALL_runtime子系统详细设计.md \
  apps/daemon/src/main.cpp \
  apps/gateway/src/main.cpp \
  runtime/src/AgentFacade.cpp
```

## 10. 结论

1. `RuntimeDependencySet` 的 production composition owner 是 app binary 组合根，不是 `AgentFacade` 的隐藏 fallback。
2. `degraded unary` 与 stub runtime path 是两条不同语义：前者是 live composition 的受控降级，后者只是局部开发/复现路径。
3. daemon/gateway 以后只能把显式 live dependency composition 写成 `default unary` / release-preflight 证据，不能再让空 dependency set 或 stub runtime 路径冒充 production ready。