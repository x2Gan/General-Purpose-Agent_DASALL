# TOOL-FIX-006 MCP v1 transport stdio-only 口径收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-006`。
2. 本轮目标：选择 B 路径，不扩写 schema、不补 SSE / streamable-HTTP 实现，而是在 tools owner 设计、profiles owner 设计、runtime policy 资产和静态守卫中把 MCP v1 支持范围明确收口为 stdio-only。
3. 完成判定：MCP v1 的对外口径与 concrete transport 实现一致；`tools_mcp` 不再被误读为 transport selector；owner 文档不再把“接口预留”写成“泛化兼容已就绪”；存在可执行的静态 wording guard。

## 2. 本地证据

1. `tools/include/mcp/IMCPTransport.h` 仍保留 `stdio`、`sse`、`streamable_http` 三个 transport kind，说明接口层为 future transport 预留扩展位。
2. `tools/src/mcp/MCPAdapter.cpp` 的默认依赖与 transport 选择逻辑只为 `stdio` 返回 `StdioMCPTransport`；非 stdio 路径 fail-closed，说明当前 concrete transport 只有 stdio。
3. `docs/architecture/DASALL_profiles模块详细设计.md` 已冻结 `schema_version: 1` 的 tools 消费面，只允许 `tools_mcp`、`tool_visibility_rules`、`capability_cache_policy.*`、`timeout_policy.*` 等治理键，不存在 transport selector。
4. `profiles/desktop_full/runtime_policy.yaml`、`profiles/cloud_full/runtime_policy.yaml`、`profiles/edge_balanced/runtime_policy.yaml`、`profiles/edge_minimal/runtime_policy.yaml`、`profiles/factory_test/runtime_policy.yaml` 都只有 `tools_mcp` 开关和 `timeout_policy.mcp.*` / `capability_cache_policy.*`，没有任何 transport 字段。
5. 因此本轮真实缺口不是“代码忘了实现 transport switch”，而是“owner 文档和配置资产必须明确：v1 只能宣称 stdio，不得把未来扩展位外推成当前 rollout 范围”。

## 3. 设计结论

### 3.1 v1 transport 边界

1. MCP v1 的 concrete transport 只支持 stdio。
2. `IMCPTransport`、`MCPTransportKind`、`MCPServerSpec.transport_kind` 中保留 `sse` / `streamable_http`，只表示接口扩展位，不表示 v1 已具备对应 transport 能力。
3. `MCPAdapter` 命中非 stdio transport 时必须 fail-closed，并把 evidence 返回上游；这属于当前正确行为，不是待修的 v1 bug。

### 3.2 配置 owner 边界

1. `schema_version: 1` 不新增 transport selector。
2. `tools_mcp` 继续只表示 MCP 治理通道启停，不表示 transport 选择。
3. `timeout_policy.mcp.*`、`capability_cache_policy.*` 继续只表示预算、缓存和退避，不重解释为 transport family 开关。
4. 若 future 需要表达 SSE / streamable-HTTP，必须新增字段或升级 schema，而不是复用 `tools_mcp` 或现有 timeout/cache 键偷带新语义。

### 3.3 文档与 rollout 边界

1. tools owner 文档必须明确写出“v1 stdio-only / non-stdio reserved”。
2. profiles owner 文档必须明确写出“schema v1 不提供 transport selector”。
3. runtime policy 资产必须带注释说明：`tools_mcp` 不是 transport selector，MCP v1 transport 固定 stdio-only。
4. 静态守卫只检查 owner 文档与 profile 资产，不追溯历史 deliverable；历史文档允许保留“不得误宣称”类回顾性表述，但当前 owner 面不得重新引入“泛化兼容已就绪”或“MCP 生产就绪”类误导说法。

## 4. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | tools owner 文档必须把 v1 concrete transport 收口为 stdio-only | `docs/architecture/DASALL_tools子系统详细设计.md` |
| D2 | profiles owner 文档必须写明 `tools_mcp` 不是 transport selector | `docs/architecture/DASALL_profiles模块详细设计.md` |
| D3 | 五档 runtime policy 资产必须就地注释 stdio-only 边界 | `profiles/*/runtime_policy.yaml` |
| D4 | 需要一个可执行的 owner-scope wording guard | `scripts/ci/check_tool_mcp_v1_wording.sh` |
| D5 | 本轮结论与验收口径必须回写总账与工作日志 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/worklog/DASALL_开发执行记录.md` |

## 5. Build 三件套

1. 代码目标：更新 tools / profiles owner 设计文档与五档 runtime policy 资产注释，新增 owner-scope static wording guard。
2. 测试目标：guard 脚本必须同时验证“owner 文档存在 stdio-only 口径”和“owner 面不存在泛化兼容已就绪 / MCP 生产就绪误导表述”。
3. 验收命令：
   - `bash scripts/ci/check_tool_mcp_v1_wording.sh`
   - `bash -n scripts/ci/check_tool_mcp_v1_wording.sh`

## 6. Rollout Checklist

1. tools owner 文档明确写出 v1 只支持 stdio，且 non-stdio 命中时 fail-closed。
2. profiles owner 文档明确写出 `schema_version: 1` 不提供 transport selector。
3. 五档 runtime policy 资产都出现同一条 stdio-only 注释。
4. static wording guard 固定检查 owner 文档与 profile 资产，不依赖人工 grep 判读。
5. 本轮不扩写 `runtime_policy.yaml` schema，不新增 `transport` 键。
6. 本轮不使用 qemu / kvm；验收只依赖本地文档/脚本静态验证。

## 7. 风险与回退

1. 若把 `tools_mcp` 直接升级为 transport selector，会破坏 `schema_version: 1` 的冻结语义，并让 profiles / runtime / tools 对同一键产生双重解释；本轮不采用。
2. 若在没有 transport 实现、测试和 rollout 评审的情况下直接宣称 SSE / streamable-HTTP ready，会让产品承诺先于实现事实；本轮通过 owner 文档 + guard 避免此风险。
3. 若后续真的要开放非 stdio transport，应新建独立任务，补 concrete transport、focused tests、profile schema 变更和 rollout gate，而不是复用本轮结论外推。

## 8. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到文档、配置和 guard 文件。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 v1 transport 支持范围与口径收敛，不扩张到 observability、installed 证据或 future transport 实现。

结论：D Gate = PASS，可进入 `TOOL-FIX-006` Build 阶段并按 stdio-only 口径收口。