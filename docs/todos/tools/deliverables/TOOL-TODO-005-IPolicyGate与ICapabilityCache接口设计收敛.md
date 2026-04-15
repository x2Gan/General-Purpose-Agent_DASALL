# TOOL-TODO-005 IPolicyGate 与 ICapabilityCache 接口设计收敛

日期：2026-04-15  
任务：TOOL-TODO-005  
状态：D Gate PASS

## 1. 本地证据

1. docs/architecture/DASALL_tools子系统详细设计.md 6.6 已给出 `IPolicyGate::evaluate()` 与 `ICapabilityCache::snapshot()/update()` 的建议签名，且明确这些接口属于 tools/include 下的模块公共 ABI。
2. 同一设计文档 6.5.1、6.5.3、6.12.1、6.12.3 明确 `ToolAdmissionDecision`、`ToolPolicyView`、`CapabilitySnapshot` 等 supporting object 必须保持 module-local，不得推进 shared contracts。
3. docs/todos/tools/DASALL_tools子系统专项TODO.md 中 TOOL-TC006 与 TOOL-TODO-005 的验收条件一致要求：接口最小面必须满足 fail-closed / snapshot-only 约束，但不能把 ToolPolicyView / CapabilitySnapshot 升格到 contracts 共享 ABI。
4. 同一设计文档 6.12.1 说明 PolicyGate 要围绕 required scopes、caller domain、safe mode、confirmation facts 产出 deny-oriented 决策；6.12.3 说明 CapabilityCache 只保存 server capability snapshot、fresh/stale/expired 元数据和最近错误，不直接承担路由或策略裁定。
5. 当前仓库尚无 `ToolPolicyView`、`ToolAdmissionDecision`、`CapabilitySnapshot` 的独立头文件任务，因此 005 的最小可执行路径是在接口头内同时冻结这些 module-local supporting type，而不是把它们写入 contracts。

## 2. 外部参考

1. OWASP Authorization Cheat Sheet 明确建议“deny by default”“validate the permissions on every request”以及 failed authorization checks 必须安全退出。这支持本任务把 PolicyGate 公共接口设计为 fail-closed：默认 deny、显式返回 reason code，并把 policy view 与 admission request 作为每次调用都需要重新校验的输入。

## 3. Design 结论

1. IPolicyGate 首版冻结为最小 fail-closed 接口：消费 `ToolAdmissionRequest` 与 `ToolPolicyView`，返回 `ToolAdmissionDecision`；默认 admission effect 为 `deny`，直到实现层明确允许。
2. ToolPolicyView 只保留准入所需的最小投影视图：`effective_profile_id`、safe mode、high-risk confirmation、audit level、allowed domains、visibility rules；不在 005 中引入 timeout、lane budget 或 profile 解析细节。
3. ToolAdmissionRequest 只保留 admission 所需的最小事实：tool name、required scopes、caller domain、high-risk 标记、confirmation presence、route proof；不把 ToolIR、RecoveryDecision 或 runtime 主控状态塞进公共 ABI。
4. ICapabilityCache 首版冻结为 snapshot-only 接口：以 server_id 查询 `std::optional<CapabilitySnapshot>`，并通过 `update()` 写入新的 snapshot；不暴露 invalidate / persist / route-selection API。
5. CapabilitySnapshot 只保留 server_id、capability entries、freshness、last_refresh_at、last_error、trust marker 这组 cache 事实；它继续是 tools module-local supporting object，而不是 shared contract。

## 4. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| 冻结 fail-closed policy gate 接口 | tools/include/IPolicyGate.h |
| 冻结 snapshot-only capability cache 接口 | tools/include/ICapabilityCache.h |
| 增加 policy/cache surface 证据 | tests/unit/tools/ToolPolicyCapabilitySurfaceTest.cpp |

## 5. Build 三件套

1. 代码目标：把 tools/include/IPolicyGate.h、tools/include/ICapabilityCache.h 从壳文件替换为真实接口签名与最小 module-local supporting type。
2. 测试目标：新增未接线的 compile-only surface 源文件 tests/unit/tools/ToolPolicyCapabilitySurfaceTest.cpp，通过方法指针类型断言与样例初始化锁定 policy/cache 公共面，同时不提前侵入 TOOL-TODO-008 的 unit 拓扑接线 owner。
3. 验收命令：
   - `c++ -std=c++20 -I/home/gangan/DASALL/tools/include -x c++ -fsyntax-only /home/gangan/DASALL/tests/unit/tools/ToolPolicyCapabilitySurfaceTest.cpp`
   - Build_CMakeTools 构建 `dasall_tools`、`dasall_unit_tests`、`dasall_contract_tests`
   - `dasall_unit_tests` 与 `dasall_contract_tests` 目标构建期间自动执行当前 unit / contract 集合，并以构建期 gate 结果作为本轮验收依据

## 6. 风险与回退

1. 若后续 ToolConfigAdapter 需要更丰富的 `ToolPolicyView` 字段，应在保持当前接口兼容的前提下追加字段，不应回头把已有字段迁入 shared contracts。
2. 若后续 CapabilityCache 需要独立持久化或 invalidate 语义，应在实现层或新接口中扩展，不应破坏 005 的 snapshot-only 公共面。
3. 若后续评审坚持这些 supporting type 必须回到 internal-only，则需要同步重构 6.6 的接口签名；在此之前，005 以“module-local 但非 shared”作为当前最小可编译 ABI 折中。