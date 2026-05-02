# DASALL daemon 本地控制面专项评审报告（2026-05-02）

状态：Changes Requested

评审对象：daemon 专项 TODO、daemon 详细设计、当前代码实现、focused gate / smoke 交付物。

评审方式：

1. 方案评审：检查详细设计、专项 TODO、交付物口径是否一致。
2. 代码评审：检查 `apps/daemon`、`apps/cli`、`platform/linux`、部署文档与近邻测试是否与设计和 TODO 结论一致。
3. 证据复核：复用 2026-05-02 已落盘的 focused build/test/smoke 结果，不在本轮重复执行全量构建。

说明：本报告汇总的是“本次评估”的最终快照。`DMD-TODO-001` ~ `DMD-TODO-035` 的首轮基线并未被推翻，但它们不足以继续支撑“专项 fully-closed”结论；当前应以 `DMD-TODO-036` ~ `DMD-TODO-040` 的回归整改链为后续执行主线。

## 1. 执行摘要

### 1.1 总体结论

1. 方案层面：daemon 详细设计、ADR 边界和专项 TODO 的主体方向是一致的，daemon 仍被限定为本地 Access owner，而不是第二 Runtime 主控；这一点在 [详细设计约束段](../../architecture/DASALL_daemon本地控制面详细设计.md#L102-L110) 与 [专项 TODO 当前结论](DASALL_daemon本地控制面专项TODO.md#L1-L6) 上是一致的。
2. 代码层面：当前实现已具备真实 `AF_UNIX` / `SOCK_SEQPACKET` transport、focused ping/readiness 闭环和 direct-bind 部署样例，但 entry config/profile/reload 真接线、CLI 默认 endpoint、一致的 socket 权限面和文档/测试口径仍未完成闭环。
3. 交付层面：`Gate-DMD-05`、`Gate-DMD-07`、`Gate-DMD-09` 的历史 focused evidence 已存在，但它们代表的是“首轮收敛基线”，不能继续外推为当前专项已 fully closed；这也是专项 TODO 已追加 `DMD-TODO-036` ~ `040` 并引入 `Gate-DMD-10` 的根因。

### 1.2 合并结论

结论为 Changes Requested。

当前不建议把 daemon 专项继续表述为“`DMD-TODO-001` ~ `DMD-TODO-035` 已全部完成并可直接合并收口”。更准确的状态是：

1. `001` ~ `035` 形成了可复用的第一轮实现与验证基线。
2. 本次评估确认仍存在 4 个必须关闭的回归整改面。
3. 这些整改面已经被专项 TODO 正式吸收为 `DMD-TODO-036` ~ `DMD-TODO-040`，后续应以 `Gate-DMD-10` 为最终复验门。

## 2. 证据矩阵

| 证据面 | 位置 | 结论用途 |
|---|---|---|
| 方案边界与四层配置模型 | [docs/architecture/DASALL_daemon本地控制面详细设计.md:367](../../architecture/DASALL_daemon本地控制面详细设计.md#L367)；[docs/architecture/DASALL_daemon本地控制面详细设计.md:469](../../architecture/DASALL_daemon本地控制面详细设计.md#L469) | 证明 daemon 应服从 `defaults -> profile -> deployment_override -> runtime_override` 的配置加载链 |
| daemon 默认 socket 配置 | [apps/daemon/src/DaemonConfig.h:50](../../../apps/daemon/src/DaemonConfig.h#L50) | 证明 daemon 当前默认值为 `/tmp/dasall/control.sock` |
| CLI 默认 endpoint | [apps/cli/src/main.cpp:38](../../../apps/cli/src/main.cpp#L38) | 证明 CLI 当前硬编码连接 `/tmp/dasall-daemon-control.sock` |
| daemon entry 运行时硬编码 | [apps/daemon/src/main.cpp:181-185](../../../apps/daemon/src/main.cpp#L181-L185)；[apps/daemon/src/main.cpp:207](../../../apps/daemon/src/main.cpp#L207)；[apps/daemon/src/main.cpp:255](../../../apps/daemon/src/main.cpp#L255) | 证明 profile/readiness 元数据和 reload snapshot 仍是硬编码或重复使用初始值 |
| socket policy 要求 | [apps/daemon/src/DaemonSocketPolicy.cpp:183](../../../apps/daemon/src/DaemonSocketPolicy.cpp#L183) | 证明 policy 目标 socket mode 为 `0600` |
| 真实 listener 实现 | [platform/src/linux/UnixIpcProvider.cpp:229](../../../platform/src/linux/UnixIpcProvider.cpp#L229)；[platform/src/linux/UnixIpcProvider.cpp:243](../../../platform/src/linux/UnixIpcProvider.cpp#L243) | 证明实际 listen 路径执行了 `bind()` / `listen()`，但当前可见路径中没有把 socket mode 主动收敛到 `0600` |
| 部署文档口径 | [docs/deploy/daemon/README.md:48](../../deploy/daemon/README.md#L48) | 证明部署文档仍保留“CLI 尚未消费 readiness”表述 |
| 陈旧测试文件 | [tests/integration/access/CliDaemonPingIntegrationTest.cpp:11](../../../tests/integration/access/CliDaemonPingIntegrationTest.cpp#L11)；[tests/integration/access/CliDaemonPingIntegrationTest.cpp:20](../../../tests/integration/access/CliDaemonPingIntegrationTest.cpp#L20) | 证明仓库仍保留旧 socket path、旧 bool 风格 send-only smoke 文件 |
| historical gate baseline | [docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md:22](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L22)；[docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md:24](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L24)；[docs/todos/daemon/deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md:26](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L26) | 证明 `Gate-DMD-05/07/09` 的第一轮 focused evidence 已落盘 |
| real smoke baseline | [docs/todos/daemon/deliverables/DMD-TODO-035-daemon部署与supervisor交付契约.md:70-81](deliverables/DMD-TODO-035-daemon部署与supervisor交付契约.md#L70-L81) | 证明 validate-only、daemon unavailable、custom socket path start、Python UDS ping/readiness、graceful stop 均已有落盘 smoke 证据 |

## 3. 方案评审结论

### 3.1 一致项

1. daemon 的权责边界没有漂移。设计文档明确要求 daemon 只做本地控制面与 access owner，不得形成第二主循环或第二恢复裁定点；当前专项 TODO 仍沿用这一边界，没有把 daemon 扩张成新的主控平面，见 [详细设计约束段](../../architecture/DASALL_daemon本地控制面详细设计.md#L102-L110)。
2. “首轮收敛基线 + 回归整改”的结构现在是正确的。专项 TODO 已把 `DMD-TODO-036` ~ `DMD-TODO-040` 接到 6.5，并新增 `Gate-DMD-10`，这与本次评估发现的缺口是一一对应的，见 [专项 TODO 当前结论](DASALL_daemon本地控制面专项TODO.md#L1-L6)。
3. gate / deliverable 的使用方式是正确的。`DMD-TODO-028` 已明确 historical gate 证据只能作为 focused baseline，不能再把 send-only smoke 或聚合噪声误写成“已交付事实”，见 [DMD-TODO-028 评审结论](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L49)。

### 3.2 不一致项

1. 设计要求 daemon 完全服从 ConfigCenter 四层模型，但真实 `main.cpp` 仍未把该模型接到入口组合根，见 [四层模型要求](../../architecture/DASALL_daemon本地控制面详细设计.md#L469) 与 [main.cpp entry 硬编码段](../../../apps/daemon/src/main.cpp#L181-L185)。
2. 设计与 policy 要求 socket 权限面受控，但当前代码只声明了 `0600` 目标，没有在真实 bind 路径上形成可见的权限收敛动作，见 [DaemonSocketPolicy.cpp:183](../../../apps/daemon/src/DaemonSocketPolicy.cpp#L183) 与 [UnixIpcProvider.cpp:229-243](../../../platform/src/linux/UnixIpcProvider.cpp#L229-L243)。
3. 部署文档仍保留“CLI 尚未消费 readiness”说法，但 current CLI 已经具备 `readiness` 命令和 response parser；文档与代码不再一致，见 [apps/cli/src/main.cpp](../../../apps/cli/src/main.cpp#L47-L64) 与 [docs/deploy/daemon/README.md:48](../../deploy/daemon/README.md#L48)。

## 4. 代码评审发现

### 4.1 高优先级：daemon 入口尚未接入真实 profile/config/reload 链

结论：当前 `apps/daemon/src/main.cpp` 仍带有明显的 entry-level hardcode，导致真实二进制入口尚未等价实现设计文档要求的四层配置治理。

证据：

1. 设计要求 daemon “完全服从 ConfigCenter 四层模型：defaults、profile、deployment_override、runtime_override”，见 [详细设计:469](../../architecture/DASALL_daemon本地控制面详细设计.md#L469)。
2. 当前入口仍直接写死 `daemon_profile_id = "daemon.direct_bind.v1"`、`daemon_listener_ready = true`、`daemon_gateway_ready = true`、`daemon_bridge_reachable = true`，见 [main.cpp:181-185](../../../apps/daemon/src/main.cpp#L181-L185)。
3. `DaemonBootstrap::build()` 仍接收硬编码的 `.effective_profile_id = "daemon.direct_bind.v1"`，见 [main.cpp:207](../../../apps/daemon/src/main.cpp#L207)。
4. 收到 reload signal 时，reloader 直接重复 `apply_reload_snapshot(parsed.config)`，没有 fresh snapshot source，见 [main.cpp:255](../../../apps/daemon/src/main.cpp#L255)。

影响：

1. 当前 binary entry 仍不能代表 profile/config helper 已经具备的能力。
2. `Gate-DMD-07` 的 historical PASS 只能证明 allowlist helper 有效，不能证明真实进程入口已具备相同性质。
3. 若继续把专项口径写成 fully closed，会把 helper 级能力误写成 binary 级能力。

整改映射：`DMD-TODO-038`、`DMD-TODO-039`。

### 4.2 高优先级：CLI 默认 endpoint 与 daemon 默认 endpoint 仍然漂移

结论：daemon 和 CLI 当前使用的是两条不同的默认 socket path，这会直接破坏“默认值即可互通”的本地控制面契约。

证据：

1. daemon 默认值是 `"/tmp/dasall/control.sock"`，见 [DaemonConfig.h:50](../../../apps/daemon/src/DaemonConfig.h#L50)。
2. CLI 默认连接值是 `"/tmp/dasall-daemon-control.sock"`，见 [apps/cli/src/main.cpp:38](../../../apps/cli/src/main.cpp#L38)。
3. 专项 TODO 已把这一问题吸收到 `DMD-TODO-036`，说明当前仓库也已承认该契约尚未收口，见 [专项 TODO 当前结论](DASALL_daemon本地控制面专项TODO.md#L1-L6)。

影响：

1. CLI 默认路径不能直接验证 daemon 默认配置。
2. 运维 smoke 很容易被迫依赖“显式自定义 socket path”或“旧兼容路径”，导致默认契约长期失真。
3. 文档、CLI 和 daemon 三方口径不一致时，后续部署验收容易出现假阳性。

整改映射：`DMD-TODO-036`。

### 4.3 高优先级：socket policy 已声明 0600，但真实 bind 路径仍缺最终权限收敛证据

结论：policy 与真实 transport 之间仍存在一段未闭合的实现差距。

证据：

1. daemon socket policy 明确要求 `required_socket_mode = 0600U`，见 [DaemonSocketPolicy.cpp:183](../../../apps/daemon/src/DaemonSocketPolicy.cpp#L183)。
2. 真实 `UnixIpcProvider::listen()` 已执行 `bind()` 和 `listen()`，见 [UnixIpcProvider.cpp:229](../../../platform/src/linux/UnixIpcProvider.cpp#L229) 与 [UnixIpcProvider.cpp:243](../../../platform/src/linux/UnixIpcProvider.cpp#L243)。
3. 在当前可见 listen 路径中，没有在 `bind()` 之后通过 `chmod()` / `fchmod()` 等动作把 socket 文件权限显式收敛到 `0600`。

影响：

1. stale socket cleanup 与 restart-safe bind 的安全前提仍可能和真实文件权限不一致。
2. 部署方即便满足了父目录要求，仍无法从代码上直接确认最终 socket mode 与 policy 一致。
3. `DMD-TODO-032` 的 policy / validator 收敛，尚未自动等价为真实 listener 文件权限已经正确。

整改映射：`DMD-TODO-037`。

### 4.4 中优先级：部署文档与陈旧测试仍会制造“已交付”假象

结论：当前文档与测试拓扑仍残留旧口径，容易让评审者误以为 send-only smoke 或旧 socket path 仍代表权威行为。

证据：

1. 部署 README 仍写着“当前 CLI 尚未消费 readiness 响应”，见 [docs/deploy/daemon/README.md:48](../../deploy/daemon/README.md#L48)。
2. 仓库中仍保留旧的 [CliDaemonPingIntegrationTest.cpp](../../../tests/integration/access/CliDaemonPingIntegrationTest.cpp#L11-L31)，其中继续使用 `"/tmp/dasall-daemon-control.sock"`，见 [line 20](../../../tests/integration/access/CliDaemonPingIntegrationTest.cpp#L20)。
3. 对 `**/CMakeLists.txt` 检索 `CliDaemonPingIntegrationTest` 的结果为 0 matches，说明该文件当前不在构建注册路径中，属于“留在仓库里但不再代表权威测试拓扑”的陈旧资产。
4. `DMD-TODO-028` 已明确“不得再把历史 send-only smoke 写成 daemon 已交付事实”，见 [DMD-TODO-028:49](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L49)。

影响：

1. 评审和运维可能同时被旧文档与旧测试误导。
2. 任何“README 能跑 / 仓库里有测试文件”式结论都可能失真。
3. 若不清理，专项 TODO 的 reopen hardening 口径会再次漂移。

整改映射：`DMD-TODO-040`。

## 5. focused 验证与命令结果汇总

### 5.1 可确认的正向基线

1. `Gate-DMD-05` 已有 focused 证据，证明 ping/readiness/diag 语义分离以及 CLI response parser 的第一轮收敛，见 [DMD-TODO-028:22](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L22)。
2. `Gate-DMD-07` 已有 focused 证据，证明 graceful shutdown、failure injection、profile compatibility 与 hot-reload allowlist helper 的第一轮闭环，见 [DMD-TODO-028:24](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L24)。
3. `Gate-DMD-09` 已有证据，证明 TODO、deliverable 和 worklog 曾完成一次统一回写，见 [DMD-TODO-028:26](deliverables/DMD-TODO-028-daemon专项Gate与交付证据收敛.md#L26)。
4. 部署 smoke 已有落盘结果：validate-only 正常、daemon unavailable fail-closed、custom socket path 启动成功、原始 Python UDS ping/readiness 正常、`SIGTERM` graceful stop 正常，见 [DMD-TODO-035:70-81](deliverables/DMD-TODO-035-daemon部署与supervisor交付契约.md#L70-L81)。

### 5.2 本轮报告对命令结果的使用边界

1. 本轮没有重复执行全量 build/test/smoke，只复核了仓库中已经落盘的 focused evidence 与当前源码状态。
2. 因此，本报告不推翻历史 PASS，而是把这些 PASS 明确降格为“首轮收敛基线”。
3. 后续真正的关闭条件不是重复宣告 `Gate-DMD-05/07/09` 历史 PASS，而是完成 `DMD-TODO-036` ~ `040` 后，重跑 `Gate-DMD-05`、`Gate-DMD-07`、`Gate-DMD-09` 并补写 `Gate-DMD-10`。

## 6. 与专项 TODO 的一致性判断

当前专项 TODO 已与本次评估结论基本一致，判断如下：

1. 一致：顶部状态已从 fully-closed 调整为 reopen hardening，见 [专项 TODO 当前结论](DASALL_daemon本地控制面专项TODO.md#L1-L6)。
2. 一致：`DMD-TODO-036` ~ `DMD-TODO-040` 覆盖了 endpoint、socket mode/stale restart、profile/config entry、reload snapshot、文档/证据复验五个整改面。
3. 一致：`Gate-DMD-10` 已成为新的最终复验门，用来阻止“只凭历史 PASS 继续宣告专项完成”。
4. 仍需执行：在 `036` ~ `040` 完成前，不应恢复任何“专项 fully closed / 已可合并收口”的表述。

## 7. 最终结论

### 7.1 评审结论

本次评审的最终结论为 Changes Requested。

理由不是 daemon 主体方向错误，而是当前仓库仍存在“首轮实现基线已具备，但最后一段运行契约没有真正闭合”的问题。该问题集中在：

1. 真实入口未接入 profile/config/reload 的最终链路。
2. CLI 默认 endpoint 与 daemon 默认 endpoint 不一致。
3. socket policy 与真实 bind 后权限之间仍缺闭环证据。
4. 文档和陈旧测试仍会制造“已交付”假象。

### 7.2 关闭条件

只有在以下条件同时满足后，才建议把 daemon 专项重新表述为 close-ready：

1. `DMD-TODO-036` ~ `DMD-TODO-040` 全部完成。
2. `Gate-DMD-10` 补齐并 PASS。
3. `Gate-DMD-05`、`Gate-DMD-07`、`Gate-DMD-09` 完成针对整改后的 focused 复验回写。
4. 部署文档、专项 TODO、deliverables 与测试拓扑不再残留旧 socket path、send-only smoke、helper 级能力误写为 binary 级能力的口径。

### 7.3 建议执行顺序

1. 先做 `DMD-TODO-036`、`DMD-TODO-037`，收敛默认 endpoint 与真实 socket 权限面。
2. 再做 `DMD-TODO-038`、`DMD-TODO-039`，把 entry config/profile/reload 真正接到 binary 入口。
3. 最后做 `DMD-TODO-040`，清理文档/陈旧测试并统一回写 `Gate-DMD-10` 证据。