# TOOL-FIX-010 `knowledge.search` / Knowledge 与 Tools 关系收敛

## 1. 任务来源与目标

1. 来源任务：`docs/todos/DASALL_子系统查漏补缺专项记录.md` 中 `TOOL-FIX-010`。
2. 本轮目标：收口 `knowledge.search` 与 Knowledge / Tools 的边界口径，明确当前是否需要把 Knowledge 检索面暴露为 tool；若当前不存在该需求，则以 owner 文档与静态守卫固定“Knowledge 是相邻子系统，而不是 Tools 实现目录”的结论。
3. 完成判定：tools owner 详设、专项总账、deliverables 索引与工作日志对同一结论保持一致，且新增静态守卫能阻止把 `knowledge/*` 误写成 tools 实现域或把 `knowledge.search` 误记为当前已落地 builtin。

## 2. 本地证据

1. `apps/runtime_support/src/RuntimeLiveDependencyComposition.cpp` 中 `compose_runtime_tool_manager()` 当前只向 `ToolRegistry` 注册 `agent.dataset`，运行时 live composition 并未注册 `knowledge.search`。
2. `tools/src/registry/BuiltinCatalog.cpp` 当前仅聚合 `builtin::terminal::build_descriptor()` 与 `builtin::dataset::build_descriptor()`；module-local builtin catalog 里也不存在 `knowledge.search`。
3. `docs/architecture/DASALL_knowledge子系统详细设计.md` 已在边界表中定义 Knowledge 与 tools 为“通过 Runtime 主链路协作”的同层模块，并明确“Knowledge 不得反调 `tools/*` 或 `services/*` 执行副作用路径”。
4. `docs/todos/DASALL_子系统查漏补缺专项记录.md` 当前已经把 `knowledge/*` 记为“非 tools 实现域”，但尚未形成 tools owner 文档与静态守卫闭环，因此评审时仍可能出现“Knowledge 完成度可替代 Tools 完成度”或“`knowledge.search` 已存在实现”两类口径漂移。

## 3. 外部参考

1. MCP Architecture overview 指出：host 负责管理 capabilities 与 lifecycle，tools 只是 server 显式暴露并经 discovery 注册的 primitive；context/resource 与 executable tools 是分层能力，不会因为底层存在某个子系统就自动变成可调用 tool。该原则支持 DASALL 当前将 Knowledge 维持为 Runtime 邻接检索子系统，只有在显式注册 `knowledge.search` builtin / MCP / skill binding 时才进入 Tools 可见面。

## 4. 设计结论

### 4.1 根因重判

1. `TOOL-FIX-010` 当前缺的不是 `knowledge.search` 实现本身，而是“Knowledge 与 Tools 的关系 owner 口径”仍只存在于专项总账，未回写到 tools 详设和静态守卫。
2. 现有代码路径表明 Runtime 同时持有 `knowledge_service` 与 `tool_manager` 两条独立门面：Knowledge 负责 retrieval/evidence，Tools 负责 governed tool execution；两者在 runtime/profile/evidence 层汇合，而不是互为实现目录。
3. 若在本轮直接新增 `knowledge.search` builtin/MCP/skill binding，需要同时改动 ToolRegistry、PolicyGate、RouteSelector、runtime live composition 与 focused tests，这已经超出“关系收口”这个原子任务的最小闭环。

### 4.2 本轮决定

1. 本轮不新增 `knowledge.search` builtin、MCP binding 或 skill binding。
2. 本轮通过 tools owner 详设、deliverable、专项总账、工作日志与 docs/CI 静态守卫，把“`knowledge/*` 不是 tools 实现域”固定为 authoritative 结论。
3. 若未来确有将 Knowledge 检索面暴露为 tool 的需求，必须作为独立任务显式落地以下链路：`ToolRegistry` / `ToolPolicyGate` / `ToolRouteSelector` 注册、runtime-facing stable facade、focused integration test，以及相应 profile / rollout gate。

### 4.3 边界与不外推项

1. 本轮不把 Knowledge retrieval facade 包装成 tool，也不改动 `IKnowledgeService`、`ToolManager` 或 runtime composition 的产品实现。
2. 本轮不把 contract tests 中出现的示例名 `knowledge_search` 误判为 production capability；这些测试只验证共享对象字段语义，不代表 builtin 已落地。
3. 本轮禁止使用 qemu / kvm；验收只依赖本地文档回写与静态守卫。

## 5. Design -> Build 映射

| D 项 | 设计结论 | Build 落点 |
|---|---|---|
| D1 | tools owner 详设必须显式写出 Knowledge 只通过 Runtime / profile / evidence 与 Tools 间接协作 | `docs/architecture/DASALL_tools子系统详细设计.md` |
| D2 | 需要 docs/CI 守卫固定“当前未落地 `knowledge.search` tool，`knowledge/*` 不得出现 tools 实现耦合” | `scripts/ci/check_tools_knowledge_boundary.sh`、`scripts/ci/static_check.sh` |
| D3 | 专项总账、deliverable 索引与工作日志必须回写同一口径，避免只在一处收口 | `docs/todos/DASALL_子系统查漏补缺专项记录.md`、`docs/todos/tools/deliverables/DELIVERABLES-INDEX.md`、`docs/worklog/DASALL_开发执行记录.md` |
| D4 | 未来若要真正引入 `knowledge.search`，必须以独立任务追加 runtime-facing binding 与 focused tests，而不是绕过 owner 文档直接在 `knowledge/*` 下补实现 | 后续独立任务，不在本轮范围 |

## 6. Build 三件套

1. 代码目标：更新 tools owner 详设、专项总账、deliverables 索引和工作日志，并新增 `check_tools_knowledge_boundary.sh` 静态守卫后接入 `static_check.sh`。
2. 测试目标：以静态守卫验证 owner 文档口径和源码边界一致；继续保留 `RuntimeKnowledgeEvidenceIntegrationTest` 作为 Knowledge 主链 evidence 的既有 authority，但不把它误写成 `knowledge.search` tool 证据。
3. 验收命令：
   - `bash -n scripts/ci/check_tools_knowledge_boundary.sh && bash scripts/ci/check_tools_knowledge_boundary.sh`
   - `rg -n "IToolManager|tools::" knowledge --glob '*.{h,hpp,hh,c,cc,cpp,cxx}' ; test $? -eq 1`

## 7. Rollout Checklist

1. tools owner 详设必须出现 `knowledge/*` 非 tools 实现域、二者只通过 Runtime / profile / evidence 间接协作的明确表述。
2. static check 必须能在文档口径或源码边界回退时 fail-closed。
3. 专项总账、deliverable、索引与工作日志必须四处一致，不得一处写“已收口”、另一处仍暗示 `knowledge.search` 已落地。
4. 本轮不得把文档守卫结果外推为新增 tool 功能已可用。

## 8. 风险与回退

1. 若跳过 owner 文档而直接扩写 `knowledge.search`，会在没有 runtime-facing binding 设计和 focused tests 的前提下引入额外行为面，超出本轮任务粒度。
2. 若只在专项总账中写结论、不加静态守卫，后续评审仍可能把 `knowledge/*` 当成 tools 详设实现落点，造成口径反复。
3. 若未来确需落地 `knowledge.search`，本轮新增的 guard 需要由后续独立任务显式更新；这属于有意识的 rollout，而不是当前口径的缺陷。

## 9. D Gate

1. 设计产物已落盘。
2. Design -> Build 映射已明确到 owner 文档、静态守卫、专项总账、索引与工作日志。
3. Build 三件套已锁定，且不依赖 qemu / kvm。
4. 范围保持在 Knowledge / Tools 关系收口，不扩张到新增 tool 实现。

结论：D Gate = PASS，可进入 `TOOL-FIX-010` Build 阶段并按文档 + guard 路径完成收口。