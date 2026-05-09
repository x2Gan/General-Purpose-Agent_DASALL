# GatewayBinaryProductionPathV1 (Single Source of Truth)

状态：Frozen
Owner：access / apps / runtime
关联任务：INTFIX-TODO-002
关联阻塞：INTFIX-BLK-02
关联 Gate：Gate-INT-08、Gate-INT-10

## 1. 目标

本文件冻结 `apps/gateway` 二进制入口在 unary v1 场景下的 production composition rule，统一以下四类口径：

1. `GatewayAccessPipelineOptions::runtime_dispatch_backend` 的 production 来源。
2. gateway binary 允许消费的 profile / config 输入。
3. 缺 backend 或缺 production submit pipeline 时的 fail-closed 语义。
4. production path 与 mock/test profile path 的证据边界。

本文件不定义 live dependency set 的完整 runtime 组合根细节；该部分继续由 `RuntimeAppCompositionV1` 负责。

## 2. 适用范围与非范围

适用范围：

1. `apps/gateway/src/main.cpp` 的 binary composition root。
2. `access::create_gateway_access_gateway()` 对 `runtime_dispatch_backend` 的生产路径要求。
3. gateway binary 的 health/readiness 对 production submit pipeline 的依赖关系。

不适用范围：

1. HTTP handler 细节、CORS 响应头或 publish payload 映射细节。
2. daemon binary 的本地控制平面装配。
3. runtime live dependency graph 的 owner 归属。

## 3. 当前代码信号

截至 2026-05-09，与 gateway production composition 直接相关的代码信号如下：

1. `apps/gateway/src/main.cpp` 当前创建 `GatewayAccessPipelineOptions` 后直接调用 `create_gateway_access_gateway()`，但没有装配 `runtime_dispatch_backend`。
2. `access/src/AccessGatewayFactory.cpp` 中 `build_gateway_submit_pipeline()` 在 `runtime_dispatch_backend` 缺失时返回 `nullptr`，这说明 gateway submit pipeline 的 production 前提已经在工厂层显式存在。
3. 同一 `main.cpp` 在 `gateway->init()` 失败时输出 `production submit pipeline unavailable` 并退出，这表明缺 backend 本应是 fail-closed，而不是 fallback 到 mock path。
4. `apps/gateway/src/main.cpp` 仍把 health readiness 直接绑定到 `gateway->is_ready()`；在 production backend owner 未冻结前，这个 ready 还不足以证明 binary submit path 已闭合。

这些信号证明：当前缺口不是 `AccessGatewayFactory` 不知道如何 fail-closed，而是 gateway binary 还没有被冻结的 production backend owner 与 config 输入规则。

## 4. production composition 规则

### 4.1 gateway binary owner 规则

gateway binary 的 production composition root 固定由 `apps/gateway/src/main.cpp` 或其 app-local helper 拥有，且只负责以下装配动作：

1. 读取 install layout、profile/config 输入和 gateway entry 配置。
2. 构造或连接 runtime-facing app-local bridge / facade。
3. 生成 `GatewayAccessPipelineOptions` 并注入真实 `runtime_dispatch_backend`。
4. 调用 `create_gateway_access_gateway()` 获取 `IAccessGateway` facade。
5. 把 `accepted`、`bridge-reachable`、`health-ready` 投影到 HTTP health surface。

gateway binary 不得把这些 owner 责任下放给：

1. `AccessGatewayFactory` 的隐藏 fallback。
2. tests/fixtures 中的 mock lambda。
3. protocol adapter、health handler 或 publish path 的局部布尔值。

### 4.2 production backend 来源

`GatewayAccessPipelineOptions::runtime_dispatch_backend` 在 production path 中只允许来自以下来源之一：

1. gateway main 自身创建的 runtime facade / bridge 对象。
2. app-local runtime composition helper 返回的真实 dispatch callable。
3. 与 `BinaryEntrypointReadinessV1` 对齐、可投影 `accepted` / `bridge-reachable` / `health-ready` 的 runtime binary bridge。

以下来源一律不构成 production backend：

1. 仅用于 unit/integration fixture 的 lambda fake。
2. 返回固定成功结果的 mock pipeline。
3. 仅证明 ping/liveness 的空实现或占位 backend。

### 4.3 profile / config 输入规则

gateway binary 的 production path 允许消费的输入固定为：

1. install layout 派生的 profiles / assets / config 根路径。
2. gateway entry 自身的 bind/listen/security 配置。
3. runtime policy snapshot 或等价 profile 投影视图。
4. access bootstrap / auth / admission / publish 所需的 production config view。

不允许作为 production backend owner 的输入来源：

1. tests/fixtures 专用的 fake config view。
2. 仅为 mock pipeline 服务的 profile 开关。
3. 隐式环境变量或硬编码分支，导致 gateway 在未声明 mock/test profile 的情况下退回 fake runtime bridge。

## 5. fail-closed 与 health 规则

1. `runtime_dispatch_backend` 缺失时，gateway binary 必须在 init 前或 init 时 fail-closed；不得以空 pipeline 继续启动 HTTP submit 主链。
2. `gateway->init()` 返回失败时，binary 必须输出明确的 startup failure cause，例如 `production submit pipeline unavailable` 或等价错误语义。
3. `/health/ready` 只有在 `AccessGateway` ready 且 production `runtime_dispatch_backend` 已装配时才允许返回 ready。
4. `gateway->is_ready()` 只能表达 `AccessGateway` facade 的 readiness；若 production backend owner 尚未满足，不得单独外推为 gateway binary production ready。
5. production path 的 fail-closed 不得被 `mock pipeline`、`ping-only health` 或手工 `set_ready(true)` 覆盖。

## 6. mock/test profile 边界

1. mock pipeline、fake runtime bridge、fixture adapter registry 只允许存在于测试代码、fixture helper 或显式 test profile 中。
2. 测试替身必须在命名、文件位置或 setup helper 中显式可见，不能伪装成 app binary 默认行为。
3. `apps/gateway/src/main.cpp` 不得因为“方便本地 smoke”而内建无声明的 mock fallback。
4. focused integration 中的 mock backend 只能证明 `Gate-INT-08` 的局部 access 语义，不自动构成 `Gate-INT-10` 的 app-binary / release 证据。

## 7. 与相邻 SSOT 的关系

1. `AccessUnaryProductionPathV1` 定义 access v1 production ingress 的共享边界；本文件只冻结 gateway binary 如何装配并证明该 shared 边界。
2. `BinaryEntrypointReadinessV1` 定义 `accepted`、`bridge-reachable`、`health-ready` 等 entrypoint 术语；本文件在 gateway binary 上承接这些术语的来源和 fail-closed 规则。
3. `RuntimeAppCompositionV1` 负责 live / stub runtime dependency composition；本文件只要求 gateway binary 不得在 production path 依赖未声明的 mock backend。

## 8. Design -> Build 映射

| 设计决策 | 后续 Build 任务 | 代码目标 | 测试目标 | 验收命令 |
|---|---|---|---|---|
| gateway binary 必须装配真实 `runtime_dispatch_backend` | `INTFIX-TODO-006` | `apps/gateway/src/main.cpp`、必要时 app-local runtime helper、`access/src/AccessGatewayFactory.cpp` | `GatewayAccessSubmitCompositionTest`、`GatewayBinaryUnarySmokeTest` | `Build_CMakeTools(buildTargets=["dasall_gateway","dasall_access_gateway_submit_composition_test","dasall_access_gateway_binary_unary_smoke_integration_test"])` |
| gateway 缺 backend 必须 fail-closed，且 health 不能虚报 ready | `INTFIX-TODO-009`、`INTFIX-TODO-011` | `tests/integration/access/GatewayBinaryUnarySmokeTest.cpp`、startup diagnostics tests | `GatewayBinaryUnarySmokeTest`、`GatewayStartupDiagnosticsTest` | `Build_CMakeTools(buildTargets=["dasall_access_gateway_binary_unary_smoke_integration_test","dasall_access_gateway_startup_diagnostics_test"])` |

## 9. 验证锚点

```bash
rg -n "GatewayBinaryProductionPath|runtime_dispatch_backend|mock pipeline|production submit pipeline" \
  docs/ssot/GatewayBinaryProductionPathV1.md \
  docs/architecture/DASALL_access子系统详细设计.md \
  apps/gateway/src/main.cpp \
  access/src/AccessGatewayFactory.cpp
```

## 10. 结论

1. gateway binary 的 production backend owner 是 app composition root，不是工厂 fallback、health handler 或测试替身。
2. `runtime_dispatch_backend` 缺失必须 fail-closed，并阻止 gateway binary 把 ready 假象暴露到 `/health/ready` 或 release gate。
3. mock/test profile 证据以后只能停留在显式测试接缝，不能再与 production submit pipeline 结论混写。