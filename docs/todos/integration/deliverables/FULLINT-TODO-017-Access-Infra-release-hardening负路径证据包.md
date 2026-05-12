# FULLINT-TODO-017 Access / Infra release hardening 负路径证据包

日期：2026-05-12
来源任务：FULLINT-TODO-017
范围：policy backend unavailable、diagnostics denied、audit required、listen/bind fail-closed；Gate-INT-05 / Gate-INT-08 正式收口；用户态 installed package 真实 fail-closed 取证

## 1. Phase -1 任务确认

本轮只推进 `FULLINT-TODO-017`。

可执行性判定：PASS with same-round blocker fix。

1. 前置 `FULLINT-TODO-006` 与 `FULLINT-TODO-009` 已完成：Access focused ingress gate 与 startup diagnostics stage 覆盖已存在真实代码落点，但 `FULLINT-TODO-017` 需要把更广的 release hardening 负路径收口到正式 gate，而不是停留在零散 focused test。
2. 本轮最初代码考古结论不是“功能未实现”，而是“已有负路径实现和测试没有全部进入 `dasall_gate_int_05` / `dasall_gate_int_08` 的 discoverability / acceptance 面”。缺口集中在四类现有 owner：
   - `AccessPolicyGate` 的 policy backend unavailable。
   - daemon diagnostics deny 与 publish failure audit。
   - infra diagnostics deny / audit-required / retained snapshot。
   - daemon/gateway startup diagnostics 的 listen/bind fail-closed。
3. 同轮验证阻塞在第一次 `Build_CMakeTools(buildTargets=["dasall_gate_int_05","dasall_gate_int_08"])` 时暴露：一旦把 startup diagnostics tests 接入 `gate-int-05`，当前宿主机因为存在 installed package assets，daemon/gateway binary 会优先解析 packaged install layout，并在 `runtime-dependency-composition` 阶段因 `/var/lib/dasall/memory` 权限不足提前失败，导致无法命中我们真正要验的 `listener-bind` / `listen` 分支。
4. 同轮最小解阻策略是：不改 Access / Diagnostics 业务语义，只为 install layout 增加 `DASALL_STATE_ROOT` 绝对路径覆盖缝隙，并让 startup diagnostics tests 把 state root 固定到临时目录，从而在普通用户环境下稳定触发真实 socket / port 资源冲突。
5. 用户要求本轮不能只依赖既有单测/集测或文档完成状态，因此本交付物同时记录三类真实证据：
   - 当前代码的 `Build_CMakeTools` gate 验收结果。
   - 当前系统已安装 `dasall` / `dasall-daemon` 的用户态 fail-closed probe。
   - 不依赖测试框架的当前构建产物 daemon/gateway 直接二进制负路径 probe。

## 2. 研究输入

### 2.1 本地证据

| 输入 | 本轮采用方式 |
|---|---|
| `docs/todos/integration/DASALL_全量业务链集成验证专项TODO-2026-05-11.md` | 锁定 `FULLINT-TODO-017` 的任务边界、前置任务、验收命令与完成判定。 |
| `docs/architecture/DASALL_access子系统详细设计.md` | 回链 Access security 边界：Access 只做接入与发布，授权默认 fail-closed，不用 liveness 代替安全结论。 |
| `docs/architecture/DASALL_infra_diagnostics模块详细设计.md` | 回链 diagnostics 边界：diagnostics 只输出证据与导出，不越权做恢复裁定；命令白名单、导出与审计必须显式可观测。 |
| `docs/todos/integration/deliverables/FULLINT-TODO-009-startup-diagnostics-stage覆盖.md` | 继承 startup diagnostics 的 forced-stage 与真实资源冲突 fixture，不重复扩展 stage owner，只把 listen/bind 负路径纳入正式 gate。 |
| `access/src/AccessPolicyGate.cpp`、`access/src/AccessGatewayFactory.cpp` | 确认 policy backend unavailable、daemon diagnostics deny、publish failure audit 的真实行为 owner 在 Access core / pipeline，而不是文档层。 |
| `infra/src/diagnostics/DiagnosticsServiceFacade.cpp` | 确认 diagnostics command deny、retained snapshot、remote export audit-required 的真实行为 owner 在 diagnostics facade。 |
| `tests/integration/access/*.cpp`、`tests/integration/infra/*.cpp` | 确认已有 focused tests 已覆盖目标负路径，只是未完整纳入 `gate-int-05` / `gate-int-08` discoverability / acceptance。 |
| installed command surface：`command -v dasall dasall-daemon dasall_gateway`、`dasall --help`、`dasall diag --help` | 确认当前系统仅有 installed `dasall` 与 `dasall-daemon`，无 installed `dasall_gateway`；因此 gateway `listen` 负路径必须以当前构建产物 binary probe 记录，不能误写为 installed-package L4。 |

### 2.2 设计回链结论

1. Access 侧 negative path 的 owner 继续是 `AccessPolicyGate`、Access pipeline、daemon/gateway entrypoint；本轮不扩展 Runtime / Cognition / Memory 边界。
2. diagnostics 侧 negative path 的 owner 继续是 `DiagnosticsServiceFacade` 与其审计 / retained snapshot 约束；本轮不新增 contracts，不让 diagnostics 越权做恢复判断。
3. startup diagnostics 的 listen/bind 证据属于 build-tree app-binary / direct binary 层，不外推为 installed-package L4，更不外推 release runner / qemu L5。

## 3. Design 原子项

| 原子项 | 设计目标 | 输入依据 | 完成判定 | 风险与回退 |
|---|---|---|---|---|
| D1 | 把现有 Access / Infra hardening tests 收口进正式 gate | `tests/CMakeLists.txt`、`tests/integration/access/CMakeLists.txt`、TODO 验收命令 | `gate-int-05` / `gate-int-08` 可直接构建并运行目标负路径，不再只是标签空跑 | 若 discoverability 缺测试名或 custom target 未依赖对应可执行体，则 gate 继续视为未闭合 |
| D2 | 让 startup diagnostics 在普通用户环境下稳定命中 listen/bind | 第一次 gate 验证失败日志、`InstallLayout` packaged root 选择逻辑 | daemon/gateway startup diagnostics 不再先被 `/var/lib/dasall/memory` 权限提前打断 | 若需要扩 CLI 参数或改生产行为，则超出本轮最小范围，转 blocker |
| D3 | 记录真实用户态 installed-package fail-closed 结果 | installed `dasall` / `dasall-daemon` 命令面 | `ping` / `readiness` / `diag` 的普通用户探针返回真实拒绝原因，而不是假定服务可用 | 若当前机器无 installed package，则只能记录环境缺失，不宣称 L4 |
| D4 | 用直接 binary probe 固化 daemon `listener-bind` 与 gateway `listen` | 当前构建产物二进制、真实 socket / TCP 资源冲突 | stderr 中出现目标 `stage` / `error_code` / `detail`，且 return code 为失败 | 若 daemon socket parent 位于 `/tmp`，会先命中 config-validation；需显式使用私有 0700 目录 |

## 4. Design -> Build 映射

| Design 决策 | Build / 验证落点 | 通过条件 |
|---|---|---|
| D1：gate 收口 | `tests/integration/access/CMakeLists.txt`、`tests/CMakeLists.txt` | `DaemonStartupDiagnosticsTest`、`GatewayStartupDiagnosticsTest` 进入 `gate-int-05`；`AccessPublishFailureAuditTest`、`DaemonDiagDenyIntegrationTest` 进入 `gate-int-08`；`gate-int-08` discoverability expected list 同步闭合 |
| D2：startup diagnostics user-mode unblock | `infra/src/config/InstallLayout.cpp`、`tests/integration/access/DaemonStartupDiagnosticsTest.cpp`、`tests/integration/access/GatewayStartupDiagnosticsTest.cpp` | 只有当环境变量 `DASALL_STATE_ROOT` 给出绝对路径时才覆盖 packaged state root；startup diagnostics tests 固定临时 state root 后能稳定命中 `listener-bind` / `listen` |
| D3：installed-package user-mode probes | 当前系统 `/usr/bin/dasall`、`/usr/sbin/dasall-daemon` | `ping` / `readiness` / `diag` 显式给出 transport / permission denied 结果，不把普通用户拒绝误写成不可复现 |
| D4：direct binary probes | `build/vscode-linux-ninja/apps/daemon/dasall-daemon`、`build/vscode-linux-ninja/apps/gateway/dasall_gateway` | daemon 返回 `stage=listener-bind` / `DAEMON_E_LISTENER_BIND_FAILED`；gateway 返回 `stage=listen` / `GATEWAY_E_LISTEN_FAILED` |

## 5. D Gate

| Gate | 判定 | 证据 |
|---|---|---|
| 范围单一 | PASS | 只处理 `FULLINT-TODO-017` 与同轮最小 blocker：gate 收口 + startup diagnostics state root 解阻。 |
| 前置依赖 | PASS | `FULLINT-TODO-006`、`FULLINT-TODO-009` 已完成。 |
| Build 三件套 | PASS | 代码目标、测试目标、验收命令与实际 package / binary probes 均在 §4 锁定。 |
| 不外推 installed-package | PASS | 当前机器无 installed `dasall_gateway`；CLI 普通用户 socket 权限结果只记录 fail-closed，不冒充 diagnostics allowlist positive path。 |

## 6. B 阶段执行结果

### 6.1 代码落点

| 文件 | 变更 | 结果 |
|---|---|---|
| `tests/integration/access/CMakeLists.txt` | `DaemonStartupDiagnosticsTest`、`GatewayStartupDiagnosticsTest` 增加 `gate-int-05;diagnostics-retained-snapshot-gate` 标签；`AccessPublishFailureAuditTest` 与 `DaemonDiagDenyIntegrationTest` 增加 `gate-int-08;access-v1-production-gate` 标签 | Access / startup focused tests 进入正式 gate 标签面 |
| `tests/CMakeLists.txt` | `dasall_gate_int_05` 新增 daemon/gateway startup diagnostics executable 依赖；`DASALL_GATE_INT_08_TEST_NAMES` 与 `DASALL_GATE_INT_08_EXECUTABLE_TARGETS` 新增 publish failure audit / daemon diag deny | `gate-int-05` 与 `gate-int-08` discoverability / acceptance 收口到 017 要求的四类负路径 |
| `infra/src/config/InstallLayout.cpp` | 新增 `DASALL_STATE_ROOT` 绝对路径覆盖；默认仍保持 packaged layout | 普通用户测试可把 state root 固定到临时目录，不影响默认 installed 行为 |
| `tests/integration/access/DaemonStartupDiagnosticsTest.cpp` | 每个 case 固定临时 `DASALL_STATE_ROOT` | daemon startup diagnostics 不再因 `/var/lib/dasall/memory` 权限提前失败 |
| `tests/integration/access/GatewayStartupDiagnosticsTest.cpp` | 每个 case 固定临时 `DASALL_STATE_ROOT` | gateway startup diagnostics 可在普通用户环境稳定命中 `listen` |
| 文档回写 | 新增本交付物；回写专项 TODO 与 worklog | 017 的 D/B gate、same-round blocker、真实运行结果有独立证据 owner |

### 6.2 focused gate 验证

1. 首次执行 `Build_CMakeTools(buildTargets=["dasall_gate_int_05","dasall_gate_int_08"])`
   - 结果：失败。
   - 根因：`DaemonStartupDiagnosticsTest` 与 `GatewayStartupDiagnosticsTest` 在当前宿主机先命中 `runtime-dependency-composition`，stderr 关键片段为：
     - `stage=runtime-dependency-composition error_code=DAEMON_E_RUNTIME_COMPOSITION_FAILED ... detail=memory state directory unavailable for daemon.local-control-plane: /var/lib/dasall/memory: Permission denied`
     - `stage=runtime-dependency-composition error_code=GATEWAY_E_RUNTIME_COMPOSITION_FAILED ... detail=memory state directory unavailable for gateway.http-unary: /var/lib/dasall/memory: Permission denied`
   - 处理：同轮最小修复 `DASALL_STATE_ROOT` 覆盖缝隙，并让 startup diagnostics tests 固定临时 state root。
2. 再次执行 `Build_CMakeTools(buildTargets=["dasall_gate_int_05","dasall_gate_int_08"])`
   - 结果：通过。
   - `gate-int-05`：`InfraDiagnosticsSmokeTest`、`InfraDiagnosticsIntegrationTest`、`DaemonStartupDiagnosticsTest`、`GatewayStartupDiagnosticsTest` 全部 passed。
   - `gate-int-08`：discoverability / acceptance passed，expected test list 包含 `AccessPolicyBackendUnavailableIntegrationTest`、`AccessPublishFailureAuditTest`、`DaemonDiagDenyIntegrationTest` 等目标负路径入口。

### 6.3 当前系统 installed-package 用户态探针

#### 6.3.1 命令面

| 探针 | 实际结果 | 判定 |
|---|---|---|
| `command -v dasall` | `/usr/bin/dasall` | installed CLI 存在 |
| `command -v dasall-daemon` | `/usr/sbin/dasall-daemon` | installed daemon 存在 |
| `command -v dasall_gateway` | 无输出 | 当前系统无 installed gateway binary，gateway 负路径不能写成 installed-package L4 |
| `dasall --help` | 暴露 `ping/readiness/diag/config/run/status/cancel` 等子命令 | 当前用户态 control-plane probe 可直接执行 |
| `dasall diag --help` | `Usage: dasall-cli diag <health|queue|threads> ...` | diagnostics 命令面存在 |
| `dasall-daemon --help` | 返回 `stage=argument-parse error_code=DAEMON_E_ARGUMENT_PARSE_FAILED` | installed daemon 不提供独立 help surface，不影响本轮负路径 owner |

#### 6.3.2 普通用户 fail-closed 结果

| 命令 | 实际结果 | 判定 |
|---|---|---|
| `dasall ping --timeout-ms 100` | exit `3`；`connect() failed for socket path '/run/dasall/daemon.sock': Permission denied` | user-mode access fail-closed |
| `dasall readiness --json` | exit `3`；JSON `disposition="daemon_unavailable"`，`error.kind="transport"`，`reason="connect() failed for socket path '/run/dasall/daemon.sock': Permission denied"` | user-mode control-plane fail-closed |
| `dasall diag health` | exit `3`；`connect() failed for socket path '/run/dasall/daemon.sock': Permission denied` | user-mode diagnostics fail-closed |
| `dasall diag queue` | exit `3`；`connect() failed for socket path '/run/dasall/daemon.sock': Permission denied` | user-mode diagnostics fail-closed |

结论：当前普通用户未加入 daemon socket 可访问边界时，installed `dasall` / `diag` 命令会在 transport 层明确 fail-closed，而不是隐式放行或返回伪成功。这是本轮可采信的 installed-package 用户态证据；它证明了 fail-closed 和权限边界，但不等价于 allowlisted diagnostics positive path。

### 6.4 当前构建产物 direct binary probes

本轮直接运行当前构建产物，不借助 CTest wrapper，避免把 focused tests 自身当作唯一完成证据。

#### 6.4.1 daemon `listener-bind`

执行策略：

1. 创建 0700 私有临时目录。
2. 在该目录内创建并占用 `daemon.sock` Unix listener。
3. 设置 `DASALL_STATE_ROOT=<temp>/state`。
4. 运行 `build/vscode-linux-ninja/apps/daemon/dasall-daemon --socket-path <temp>/daemon.sock`。

结果：

- returncode=`1`
- stderr 包含 `stage=listener-bind`
- stderr 包含 `error_code=DAEMON_E_LISTENER_BIND_FAILED`
- stderr 包含 `active unix socket cannot be removed during bind preflight`

关键片段：

```text
[dasall-daemon] startup failure stage=listener-bind error_code=DAEMON_E_LISTENER_BIND_FAILED ... detail=active unix socket cannot be removed during bind preflight
```

说明：daemon 若把 socket 放在 `/tmp`，会先命中 `config-validation` 的 socket parent policy；因此本轮 listener-bind 证据必须使用私有 0700 目录，而不是公用 `/tmp` parent。

#### 6.4.2 gateway `listen`

执行策略：

1. 创建独立临时 state root。
2. 先占用 TCP 端口 `9999`。
3. 设置 `DASALL_STATE_ROOT=<temp>/state`。
4. 运行 `build/vscode-linux-ninja/apps/gateway/dasall_gateway --port 9999`。

结果：

- stderr 包含 `stage=listen`
- stderr 包含 `error_code=GATEWAY_E_LISTEN_FAILED`
- stderr 包含 `detail=listen failed on 0.0.0.0:9999`

关键片段：

```text
[dasall_gateway] startup failure stage=listen error_code=GATEWAY_E_LISTEN_FAILED ... detail=listen failed on 0.0.0.0:9999
```

### 6.5 017 范围闭合映射

| 017 目标负路径 | 当前完成证据 | 结论 |
|---|---|---|
| policy backend unavailable | `gate-int-08` discoverability / acceptance 已包含 `AccessPolicyBackendUnavailableIntegrationTest` | 已进入正式 Access hardening gate |
| diagnostics denied | `gate-int-08` 已包含 `DaemonDiagDenyIntegrationTest`；普通用户 installed `dasall diag health/queue` 返回 transport permission denied | Access focused diagnostics deny 已 gate 化；installed 用户态 fail-closed 已有真实输出 |
| audit required | `gate-int-05` 中 `InfraDiagnosticsIntegrationTest` 保留 diagnostics audit-required / remote export 断言；`gate-int-08` 中 `AccessPublishFailureAuditTest` 保留 publish failure audit fields | diagnostics / publish audit-required 语义已进入正式 gate |
| listen/bind fail-closed | `gate-int-05` 中 `DaemonStartupDiagnosticsTest` / `GatewayStartupDiagnosticsTest` 已通过；direct binary probe 直接命中 `listener-bind` / `listen` | startup hardening 负路径已正式收口，不再只留在 release-preflight 附属面 |

## 7. Build 合规复核

| 检查项 | 结果 |
|---|---|
| 代码目标 | PASS：本轮只改 gate 注册、discoverability expected list、install layout state root 覆盖与 startup diagnostics tests 的环境注入。 |
| 业务语义边界 | PASS：未修改 `AccessPolicyGate`、`DiagnosticsServiceFacade`、daemon/gateway startup failure 语义 owner；`DASALL_STATE_ROOT` 仅在显式设置且为绝对路径时生效。 |
| 正负例覆盖 | PASS：正例不是本轮目标；负例已覆盖 policy backend unavailable、diagnostics denied、audit required、listen/bind fail-closed，并补充用户态 installed-package 与 direct binary probe。 |
| 不依赖旧结论 | PASS：本轮完成判定以当轮 `Build_CMakeTools`、installed package 命令输出和 direct binary probe 为准，不采信旧文档/旧测试绿灯本身。 |
| 无关改动隔离 | PASS：未扩改 Runtime / Knowledge / Services 逻辑；build 生成物与其他脏文件不属于本轮提交范围。 |

## 8. Gate 判定与残余边界

Gate 判定：`FULLINT-TODO-017` Done。

已闭合：

1. `gate-int-05` 与 `gate-int-08` 已正式覆盖 017 指定的 Access / Infra release hardening 负路径 owner，不再只是零散 focused slices。
2. startup diagnostics 在当前普通用户环境下已具备稳定的 `listener-bind` / `listen` 复现路径，不再被 `/var/lib/dasall/memory` 权限提前打断。
3. 当前系统 installed `dasall` / `dasall-daemon` 的普通用户命令面已给出真实 fail-closed 结果，可证明 socket 权限边界没有被 liveness 掩盖。

残余边界：

1. 当前系统没有 installed `dasall_gateway`；gateway `listen` 负路径证据属于当前 build-tree direct binary 层，而不是 installed-package L4。
2. 普通用户 `dasall diag` 当前记录的是 transport permission denied fail-closed，不是 daemon allowlist positive path；若要提升为 rootful / allowlisted installed diagnostics 证据，应另起 package / ops 任务，不得拿本轮结果外推。
3. release runner / qemu authoritative hardening 结论仍归 `FULLINT-TODO-019`；本轮不把 L2 build-tree + user-mode local installed 证据写成 L5 production release-ready。