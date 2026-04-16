# TOOL-TODO-032 CapabilityCache 实现与状态转移验证

日期：2026-04-16  
任务：TOOL-TODO-032  
状态：已完成

## 1. 目标

1. 将 `ICapabilityCache` 的最小公共面落成真正可实例化的内部实现，避免 033 / 034 继续围绕 placeholder 展开。
2. 把 `fresh` / `stale` / `expired`、`last_error`、trusted snapshot 过滤等状态语义写成可自动断言的单测，而不是依赖后续 MCP integration 才暴露问题。
3. 保持 tools 对 profile 的消费仍然是 projection-only：CapabilityCache 只接受 `expire_after_ms`、`stale_read_allowed` 等已有策略结果，不新增新的 profile schema。

## 2. 实现落点

1. 新增 `tools/src/mcp/CapabilityCache.h` 与 `tools/src/mcp/CapabilityCache.cpp`，定义 `dasall::tools::mcp::CapabilityCache`：
   - public 面继续实现 `ICapabilityCache::snapshot()` 与 `update()`；
   - internal 面补 `invalidate()`、`mark_failed()`、`list_trusted()`；
   - 读路径以 `last_refresh_at_ms + expire_after_ms` 动态计算 `freshness`；
   - 写路径采用 snapshot-and-swap 发布 `CapabilityCacheState`，与 `ToolRegistry` 的并发模型保持一致。
2. `update()` 的职责收敛为：
   - 为成功刷新写入当前 `last_refresh_at_ms`；
   - 清空 `last_error`；
   - 将 `freshness` 归一为 `fresh`；
   - 当新的 snapshot 未显式携带 `trust_marker` 时，继承既有 trusted 标记，避免后续 stale fallback 因 refresh payload 缺字段而失效。
3. `mark_failed()` 的职责收敛为：
   - 对已有成功快照保留 entries / trust marker，仅写入新的 `last_error`；
   - 在 TTL 未到期前返回 `stale`，TTL 到期后返回 `expired`；
   - 对从未成功刷新过的 server 也保留一份错误态 snapshot，但不伪造 capability entries。
4. `list_trusted()` 只返回具备 `trust_marker` 的 snapshot，并按 `stale_read_allowed` 过滤 stale trusted snapshot；这样后续 `RouteSelector` / `CapabilityDiscovery` 不需要再复制 trusted + stale policy 判断。

## 3. 测试覆盖

1. 新增 `tests/unit/tools/CapabilityCacheTest.cpp`，覆盖以下行为：
   - `update()` 会把成功写入归一为 fresh，并清空历史 `last_error`；
   - `mark_failed()` 会在 TTL 内将快照转为 stale，TTL 外转为 expired；
   - 失败后重新 `update()` 会恢复 fresh，并继承旧的 `trust_marker`；
   - 对未知 server 的 `mark_failed()` 只保留错误态 metadata，不伪造 capability entries；
   - `list_trusted()` 与 `invalidate()` 会共同遵守 trusted / stale policy。
2. 更新 `tests/unit/tools/CMakeLists.txt`，注册 `dasall_capability_cache_unit_test`。
3. 更新 `tests/unit/CMakeLists.txt`，把 `dasall_capability_cache_unit_test` 加入 `dasall_unit_tests` 聚合，避免 CTest discover 到测试名却没有对应可执行文件。

## 4. 验证

1. 构建：
   - `Build_CMakeTools` targets: `dasall_tools`, `dasall_unit_tests`
2. 定向测试：
   - `RunCtest_CMakeTools` tests: `CapabilityCacheTest`
3. 结果：
   - `Build_CMakeTools` 返回成功，`dasall_tools` 与 `dasall_unit_tests` 均可完成构建；
   - `CapabilityCacheTest` 通过；
   - `RunCtest_CMakeTools` 仍打印历史 `DartConfiguration.tcl` 噪声，但不影响测试通过结论。

## 5. 设计影响

1. 033 可以直接复用 `CapabilityCache` 的 `mark_failed()` / `list_trusted()` 语义，不必再在 `IMCPAdapter` 或 `MCPLane` 内部临时拼 freshness 状态机。
2. 034 可以把 discovery 的失败退避聚焦在 refresh scheduler，而不是把 TTL / stale policy 重新编码一遍。
3. 当前实现仍然没有把 `CapabilitySnapshot`、`CapabilityCacheState` 等对象升格到 contracts，继续满足 `TOOL-TC023` 的 module-local 约束。

## 6. 风险与后续

1. 当前 CapabilityCache 只实现内存态 snapshot-and-swap，不涉及持久化 backend；这与详设“预留切换点但不预设必须落 memory”一致。
2. `failure_backoff_ms` 仍由 034 的 `CapabilityDiscovery` 负责消费，032 不提前在 cache 中引入调度语义，避免职责漂移。
3. generic MCP 仍不可宣称 Ready；CapabilityCache 只是 Phase 4 的其中一个内部组件，仍需 033 / 034 / 035 补齐 adapter、transport、launcher 与 integration gate。