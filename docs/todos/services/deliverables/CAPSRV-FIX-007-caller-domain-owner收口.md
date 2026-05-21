# CAPSRV-FIX-007 caller-domain owner 收口

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `CAPSRV-GAP-007` / `CAPSRV-FIX-007`。
2. 本轮目标：选择并落地 B 方案，明确 caller-domain admission owner 固定在 Tools / Access PolicyGate；Capability Services 不扩展 `ServiceCallContext` / `AdapterRouter` 的 caller-domain 输入，也不再保留未消费的 `caller_domain_allowlist` 派生字段。
3. 完成判定：services 侧不再出现 dangling caller-domain allowlist owner；Tools / Access 继续承担 caller-domain admission；新增边界测试证明 services 不拥有 caller-domain admission，ToolPolicyGate 仍 fail-closed 拦截缺失或越权 domain；本轮不使用 qemu / kvm。

## 2. 本地证据

1. `services/include/ServiceTypes.h` 中 `ServiceCallContext` 只携带 request、session、trace、tool_call、goal、budget、deadline 等调用相关事实，没有 caller-domain 字段。
2. `tools/src/bridge/ToolServiceBridge.cpp` 的 `ToolServiceBridge::build_context()` 构造 `ServiceCallContext` 时没有向 services 透传 caller-domain。
3. `services/src/adapters/AdapterRouter.h` 中 `ServicePolicyView` 曾保留 `caller_domain_allowlist`，但 `AdapterRouteRequest` 与 `AdapterRouter::select_adapter()` 没有 caller-domain 输入面，导致该字段无 owner、无消费路径。
4. `services/src/ops/ServiceConfigAdapter.cpp` 曾把 `execution_policy.allowed_tool_domains` 派生到 `ServicePolicyView.caller_domain_allowlist`，但 services router、lane 与 public ABI 均不消费该字段。
5. `tools/src/policy/ToolPolicyGate.cpp` 已在 `check_allowed_domain()` 和 `check_visibility()` 中使用 `ToolAdmissionRequest::caller_domain` 与 `ToolPolicyView::allowed_tool_domains` 做 fail-closed 准入。
6. `tools/src/ToolManager.cpp` 已把归一化后的 requested domain 写入 `ToolAdmissionRequest::caller_domain`，policy denial 发生在执行与 services bridge 之前。
7. `access/src/AccessPolicyGate.cpp` 已构造 Access policy context，并在进入受控操作前通过 policy evaluator 做上游准入裁定。
8. `docs/architecture/DASALL_capability_services子系统详细设计.md` 仍残留 “Service recheck caller_domain / decision_ref / proof” 口径，与当前代码实际不一致；本轮必须同步修正文档，避免继续暗示 services 拥有第二个 caller-domain owner。

## 3. 外部参考

1. Microsoft Azure Architecture Center 的 Gateway Routing pattern 建议把 gateway 放在一组应用或服务前面，客户端只需要知道单一入口；该模式还建议在后端服务前限制公开访问，让服务只能经 gateway 或私有网络访问。这支撑 DASALL 当前把 caller-domain admission 固定在 Tools / Access 入口侧，而不是在 services 内部重复实现 allowlist。
2. Microservices.io 的 API Gateway pattern 把 API Gateway 定义为所有客户端的 single entry point，并指出 gateway 可以实现安全校验，例如验证客户端是否有权执行请求。这与 DASALL 的 ToolPolicyGate / AccessPolicyGate owner 模型一致：caller-domain 归入口策略层解释，services 只处理已经被上游允许的执行或数据请求。

## 4. 设计结论

### 4.1 根因收口

1. `CAPSRV-GAP-007` 的根因不是 services 缺少一个简单 if 判断，而是 owner 漂移：`caller_domain_allowlist` 字段存在于 services internal policy view，但 services 没有 caller-domain 输入，也没有可靠的上游 decision/proof 绑定事实可 recheck。
2. 若选择 A，必须扩展 `ServiceCallContext`、`ToolServiceBridge`、`AdapterRouteRequest` 与 Router 语义，还要解释 raw caller domain、requested execution domain、ToolPolicyGate decision 的绑定关系；这会扩大 public ABI 与跨层耦合，不符合当前最小原子任务。
3. 当前 DASALL 代码已经把 caller-domain admission 放在 Tools / Access 上游，services 更适合作为受控后端组件，保留 route trust、availability、fallback envelope、budget/deadline 等本地 invariant，而不重复拥有 caller-domain allowlist。

### 4.2 本轮决定

1. 固定选择 B：caller-domain owner = Tools / Access PolicyGate。
2. 删除 `ServicePolicyView::caller_domain_allowlist` 与 `ServiceConfigAdapter` 中的对应派生。
3. 不扩展 `ServiceCallContext`，不向 `AdapterRouter` 新增 caller-domain 输入，不新增 decision_ref / confirmation proof 公共字段。
4. 修正 capability services 详细设计，把 caller-domain allowlist 从 services recheck 口径改为上游 owner 口径。
5. 新增 services boundary compliance test，防止 caller-domain allowlist 再次以未消费字段回到 services。
6. 扩展 Tools policy 负例，证明越权 domain 在进入执行 / services bridge 前 fail-closed。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | services 不拥有 caller-domain admission owner | 删除 `ServicePolicyView::caller_domain_allowlist` 与 `ServiceConfigAdapter` 派生 |
| D2 | `ServiceCallContext` 不扩展 caller-domain 字段 | 通过 boundary compliance test 扫描 `services/include/ServiceTypes.h` 与 services router/ops 目录 |
| D3 | ToolPolicyGate 继续 fail-closed 拦截缺失或越权 domain | 扩展 `ToolPolicyGateTest` / `ToolManagerFailurePathTest` 负例 |
| D4 | 详细设计必须与代码 owner 对齐 | 更新 `docs/architecture/DASALL_capability_services子系统详细设计.md` 的 caller-domain / proof recheck 口径 |
| D5 | 总账和工作日志必须能追溯本轮 B 方案 | 更新总账、本文档与 `docs/worklog/DASALL_开发执行记录.md` |

## 6. Build 三件套

1. 代码目标：删除 services 内未消费的 caller-domain allowlist 字段与派生；保持 `ServiceCallContext` / `AdapterRouter` ABI 不扩张；保留 Tools / Access PolicyGate 作为 caller-domain admission owner。
2. 测试目标：新增 services boundary compliance test，扩展 Tools policy / manager 负例，证明 caller-domain 约束在上游 fail-closed，而 services 不承诺第二套 owner。
3. 验收命令：
   - `cmake --build build/vscode-linux-ninja --target dasall_service_config_adapter_unit_test dasall_service_caller_domain_boundary_guard_compliance_unit_test dasall_tool_policy_gate_unit_test dasall_tool_manager_failure_path_unit_test`
   - `./build/vscode-linux-ninja/tests/unit/services/ops/dasall_service_config_adapter_unit_test && echo PASS`
   - `./build/vscode-linux-ninja/tests/unit/services/dasall_service_caller_domain_boundary_guard_compliance_unit_test && echo PASS`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_tool_policy_gate_unit_test && echo ToolPolicyGate-PASS`
   - `./build/vscode-linux-ninja/tests/unit/tools/dasall_tool_manager_failure_path_unit_test && echo ToolManagerFailurePath-PASS`
   - `rg -n "caller_domain_allowlist" services/src services/include; test $? -eq 1`
   - `rg -n "allowed_tool_domains" services/src services/include; test $? -eq 1`
   - `rg -n "caller_domain" services/include/ServiceTypes.h services/src/adapters services/src/ops; test $? -eq 1`
   - `rg -n "allowed_tool_domains|caller_domain" tools/src/policy/ToolPolicyGate.cpp tools/src/ToolManager.cpp access/src/AccessPolicyGate.cpp`
   - `rg -n "caller-domain owner|不派生到 ServicePolicyView|不消费 caller-domain" docs/architecture/DASALL_capability_services子系统详细设计.md docs/todos/DASALL_子系统查漏补缺专项记录.md docs/todos/services/DASALL_capability_services子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`

## 7. Build 原子清单

| B 项 | 代码目标 | 测试目标 | 验收命令 | 风险与回退 |
|---|---|---|---|---|
| B1 | 删除 services `caller_domain_allowlist` 字段与派生 | `ServiceConfigAdapterTest` 继续通过，证明剩余 policy derivation 未漂移 | `dasall_service_config_adapter_unit_test` | 若暴露真实消费点，停止并重新评估是否必须转 A |
| B2 | 新增 services caller-domain boundary guard | 扫描 services public/context/router/ops，禁止 dangling caller-domain owner 回流 | `dasall_service_caller_domain_boundary_guard_compliance_unit_test` | 若扫描误伤注释或测试文件，收紧扫描路径 |
| B3 | 扩展 Tools policy / manager 负例 | caller domain 缺失、越权 domain、上游 manager denial 均 fail-closed | `dasall_tool_policy_gate_unit_test`、`dasall_tool_manager_failure_path_unit_test` | 若 ToolManager 语义已变化，只修正测试到当前上游 owner，不改 services ABI |
| B4 | 同步架构 / TODO / worklog | 当前 authoritative 文档不再声明 services-owned caller-domain recheck | `rg -n "caller-domain owner|不派生到 ServicePolicyView|不消费 caller-domain" ...` | 若历史交付物仍需保留旧描述，只增加 superseded 注记 |

## 8. Build 合规预案

1. 代码注释：新增 boundary compliance test 会保留少量扫描意图注释；删除字段与派生不需要额外注释。
2. 正负例：正例为 ToolPolicyGate 对允许 domain 放行；负例为缺失 domain、越权 domain、ToolManager 上游 denial，外加 services boundary guard 的 forbidden-token 负向扫描。
3. 测试发现性：新增 services unit test target 必须注册到 `tests/unit/services/CMakeLists.txt`，并通过 direct binary 验证。
4. TODO/工作日志：总账 `CAPSRV-GAP-007` / `CAPSRV-FIX-007` 与本 worklog 必须回写验收证据。
5. 提交隔离：只 stage 本轮代码、测试、架构文档、deliverable、TODO 与 worklog 文件。

## 9. 风险与回退

1. B 方案不证明未来任意非 Tools / Access 直接调用 services 都能被 caller-domain 二次拦截；它明确禁止把这类直接调用当作当前 supported path。
2. 若未来 services 升级为独立暴露的共享平台服务，应新开任务选择 A 或 hybrid，并同步设计 caller-domain、decision_ref、proof 绑定契约。
3. 本轮不使用 qemu / kvm，也不外推 installed / release / soak 证据；这些仍属于 `CAPSRV-GAP-008`。

## 10. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 services 字段删除、boundary compliance test、Tools policy 负例与文档回写。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 `CAPSRV-FIX-007` 的 caller-domain owner 收口，不扩张到 installed / release / soak 证据。

结论：D Gate = PASS；允许进入 `CAPSRV-FIX-007` Build 阶段。