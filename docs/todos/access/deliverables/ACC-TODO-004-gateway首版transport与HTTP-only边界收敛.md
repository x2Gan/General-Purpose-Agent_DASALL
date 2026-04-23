# ACC-TODO-004 gateway 首版 transport 与 HTTP-only 边界收敛

日期：2026-04-23  
任务：ACC-TODO-004  
状态：D Gate PASS

## 1. 本地证据

1. [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 将 ACC-TODO-004 定义为冻结 gateway 首版 transport、HTTP-only 边界与线程模型，用于解阻 ACC-BLK-004，并直接前置 ACC-TODO-026 / 028。
2. [apps/gateway/CMakeLists.txt](/home/gangan/DASALL/apps/gateway/CMakeLists.txt) 在本轮前只链接 `dasall_access`、`dasall_runtime`、`dasall_contracts`、`dasall_infra` 四个内部目标，没有任何现成 HTTP/WS/MQTT transport 依赖锚点，说明 v1 若不先收口 transport，就会在实现期临时拉依赖并放大返工面。
3. [third_party/README.md](/home/gangan/DASALL/third_party/README.md) 已冻结三方引入策略为 submodule / local cache / FetchContent 三段式，适合后续在实现阶段接入单一、低引入成本的 HTTP transport，而不是先引入完整事件循环框架。
4. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 已明确 CLI/HTTP adapter 首版只承诺 unary + async receipt，而 `WS/MQTT/StreamGateway` 在 6.14.9 仍处于延后 Gate；这意味着 004 的正确目标不是“支持更多协议”，而是把 gateway v1 明确缩成 HTTP-only。
5. [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.20.2 已要求 `/health/*` 使用独立 listener，不经过 Admission pipeline；transport 选型必须与这条独立 health listener 规则兼容。

## 2. 外部参考

1. `cpp-httplib` 将自己定义为“single-file header-only cross platform HTTP/HTTPS library”，并明确使用 blocking socket I/O、只实现 HTTP/1.1。这与 DASALL gateway v1 的最小需求高度一致：只做 request/response、accepted async receipt 与 health listener，不在首版引入 HTTP/2/3、泛化事件循环或 WS/MQTT 主路径。
   - 参考：<https://github.com/yhirose/cpp-httplib>

## 3. 设计结论

1. gateway v1 transport 固定采用 `cpp-httplib`，只使用其 HTTP/1.1 request/response 子集；不在 004 阶段引入 libuv、Boost.Beast/Asio 或自研最小 HTTP parser。
2. gateway 首版能力边界固定为：HTTP unary submit、accepted async receipt query/cancel、`/health/live|ready|startup`、统一安全头/CORS gate；不承诺 SSE、chunked stream、WebSocket、MQTT 或任何订阅态协议。
3. 线程模型固定为 `1` 个 listen/accept 线程 + bounded worker task queue；每个 HTTP 请求在 worker 中完整经过 Admission pipeline，不保留 connection-scoped Access 业务状态。
4. 业务 listener 与 health listener 复用同一 HTTP transport 家族但独立绑定；health routes 不共享 Admission / async receipt 业务路由，也不暴露内部细节。
5. `apps/gateway/CMakeLists.txt` 在本轮只记录该选型与边界说明；真正的第三方拉取、编译选项和 target 链接接线留给 ACC-TODO-026 实现阶段。

## 4. 边界 / 职责

| 对象 | 边界与职责 | 不允许事项 |
|---|---|---|
| `cpp-httplib` transport seam | 仅提供 HTTP/1.1 listener、request parsing、response writing 和 bounded task queue 能力 | 在 004 阶段把其扩展成 WS/MQTT/streaming 主路径 |
| `HttpProtocolAdapter` | 把 HTTP request 归一化为 `InboundPacket`，再把 `PublishEnvelope` 映射回 HTTP response | 直接做 Admission / Runtime 调用；承诺 WS/SSE/stream lifecycle |
| `HealthProbeHandler` | 提供 `/health/*` 二值探针，独立于 Admission 主链 | 复用业务 listener 路由或泄露内部状态细节 |
| `apps/gateway/CMakeLists.txt` | 记录 v1 transport family 和 scope，不提前引入未冻结依赖 | 在设计轮就接入 WS/MQTT 或重量级事件循环库 |

## 5. 数据 / 接口说明

### 5.1 transport 候选对比

| 候选 | 结论 | 取舍理由 |
|---|---|---|
| `cpp-httplib` | 采纳 | header-only、直接提供 HTTP/1.1 server/client 与 task queue，最符合 v1 HTTP-only 范围 |
| `libuv` | 不采纳 | 只有事件循环与 socket 抽象，仍需额外 HTTP parser / route 层，超出 004 最小范围 |
| `Boost.Beast/Asio` | 不采纳 | 仓库当前无 Boost 依赖，且天然覆盖 WebSocket 能力，容易把 004/005 边界重新混写 |
| 自研最小 HTTP | 不采纳 | parser、安全头、keep-alive、错误映射维护成本过高，不适合作为 v1 最小交付 |

### 5.2 v1 HTTP-only surface

1. 业务面只承诺 unary request/response 与 accepted async receipt；HTTP 仍只是 Access core 的远程壳层，不形成新的中心控制面。
2. `HttpProtocolAdapter` 只处理 HTTP request snapshot、peer facts、headers/body 限制和 response mapping，不处理流式订阅或连接级业务状态。
3. keep-alive 只作为 transport 优化；Admission、ownership proof、receipt query 语义全部继续归 Access core。

### 5.3 并发 / listener 模型

1. business listener：单 listen/accept 线程接入，worker task queue 执行业务请求。
2. health listener：独立绑定，固定只读二值路由，不进入 Admission pipeline。
3. WS/MQTT listener：v1 不绑定、不启动、不出现在 ready 结论中。

## 6. 流程 / 时序

1. gateway 进程启动：装配 `cpp-httplib` HTTP/1.1 transport -> 绑定 business listener -> 独立绑定 health listener -> 将请求回调接到 `HttpProtocolAdapter`。
2. unary 请求：HTTP request -> `HttpProtocolAdapter::decode_http_request()` -> `IAccessGateway::submit()` -> `ResultPublisher` -> `HttpProtocolAdapter::encode_http_response()`。
3. accepted async：submit 返回 receipt 后，HTTP query/cancel 继续沿同一 unary request/response 路径进入 `AsyncTaskRegistry` / query handler，不引入长连接订阅。
4. health：`/health/*` 直接进入 `HealthProbeHandler`，不经过认证、授权、Admission、RuntimeBridge。
5. 任何 WS/MQTT route、upgrade、subscription 或 streaming 语义在 v1 一律返回 disabled/not ready，并继续留在 005/Phase A5 的延后范围内。

## 7. 文件范围

1. 设计真值源更新在 [docs/architecture/DASALL_access子系统详细设计.md](/home/gangan/DASALL/docs/architecture/DASALL_access子系统详细设计.md) 的 6.11、6.14.8、6.20.2、11、12。
2. gateway 方案说明锚点更新在 [apps/gateway/CMakeLists.txt](/home/gangan/DASALL/apps/gateway/CMakeLists.txt)。
3. 本任务交付物落于 [docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md](/home/gangan/DASALL/docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md)。
4. TODO / blocker / 证据回写落于 [docs/todos/access/DASALL_access子系统专项TODO.md](/home/gangan/DASALL/docs/todos/access/DASALL_access子系统专项TODO.md) 与 [docs/worklog/DASALL_开发执行记录.md](/home/gangan/DASALL/docs/worklog/DASALL_开发执行记录.md)。

## 8. Design -> Build 映射

| Design 项 | 后续 Build 落点 |
|---|---|
| `cpp-httplib` HTTP-only transport seam | `apps/gateway/CMakeLists.txt`、third_party 引入接线 |
| `HttpProtocolAdapter` 的 HTTP unary 范围 | `apps/gateway/src/HttpProtocolAdapter.cpp`、`tests/unit/access/HttpProtocolAdapterTest.cpp` |
| 独立 health listener | `apps/gateway/src/HealthProbeHandler.cpp`、`tests/integration/access/AccessHealthProbeIntegrationTest.cpp` |
| bounded worker queue 与 listener 边界 | `apps/gateway/src/main.cpp`、`tests/integration/access/AccessGatewaySmokeIntegrationTest.cpp` |

## 9. Build 三件套

1. 代码目标：仅更新 [apps/gateway/CMakeLists.txt](/home/gangan/DASALL/apps/gateway/CMakeLists.txt) 的方案说明，不接入真实 transport 依赖；具体实现接线留给 ACC-TODO-026。
2. 测试目标：通过 architecture / TODO / deliverable / CMake 一致性检索，确认 `cpp-httplib`、HTTP/1.1 unary、accepted async receipt、独立 health listener、WS/MQTT 延后与 bounded worker 模型形成唯一口径。
3. 验收命令：
   - `rg -n "cpp-httplib|HTTP/1.1|accepted async receipt|health listener|WebSocket|MQTT|bounded worker" docs/architecture/DASALL_access子系统详细设计.md docs/todos/access/DASALL_access子系统专项TODO.md docs/todos/access/deliverables/ACC-TODO-004-gateway首版transport与HTTP-only边界收敛.md apps/gateway/CMakeLists.txt`

## 10. 风险与回退

1. 如果后续实现阶段重新引入 WS/MQTT、streaming route、重量级事件循环依赖，或把 health/business listener 混写到同一业务路由面，会直接破坏本轮冻结的 HTTP-only 边界；必须回退到 `cpp-httplib` HTTP/1.1 unary + accepted async receipt + 独立 health listener。
2. 本任务只冻结 transport 选型与边界，不等价于 `HttpProtocolAdapter`、`HealthProbeHandler`、gateway main 已实现；ACC-TODO-026 / 028 仍需用 unit / integration 证明真实 listener、CORS/安全头和 async receipt 路径符合该设计。