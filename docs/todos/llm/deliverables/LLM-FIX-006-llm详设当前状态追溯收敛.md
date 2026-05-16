# LLM-FIX-006 llm 详设当前状态追溯收敛

状态：Done
日期：2026-05-17
来源 TODO：[docs/todos/DASALL_子系统查漏补缺专项记录.md](../../DASALL_%E5%AD%90%E7%B3%BB%E7%BB%9F%E6%9F%A5%E6%BC%8F%E8%A1%A5%E7%BC%BA%E4%B8%93%E9%A1%B9%E8%AE%B0%E5%BD%95.md)、[docs/todos/llm/DASALL_llm子系统专项TODO.md](../DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E4%B8%93%E9%A1%B9TODO.md)
任务类型：详设当前状态与历史 baseline 差异回写

## 1. 任务边界

1. 本任务只修正 [docs/architecture/DASALL_llm子系统详细设计.md](../../../architecture/DASALL_llm%E5%AD%90%E7%B3%BB%E7%BB%9F%E8%AF%A6%E7%BB%86%E8%AE%BE%E8%AE%A1.md) 中已经明显落后的“当前状态”表述，并同步专项 TODO、总账与 worklog 的追溯链。
2. 本任务不新增生产代码，不改写 ADR-006 / 007 / 008 结论，也不把 module-local supporting objects 推进到 shared contracts。
3. 总账中“回写 LLM 当前状态与历史 baseline 差异”实际对应 `LLM-FIX-006`；`LLM-FIX-005` 仍保留给 llm/source boundary 回归防线，本轮不并入。

## 2. 收敛内容

1. 元数据与依据
   - 将详设最近修订时间更新到 2026-05-17。
   - 在“主要依据”中补入 `LLM-TODO-038`、`LLM-TODO-043`、`LLM-FIX-004` 与 BC-07 / BC-16 相关 SSOT，形成从详设到专项 TODO / deliverable / 集成证据的直接追溯入口。
2. 1.4 现有工程信号
   - 把“只编译 `placeholder.cpp`、无 unit/integration、Prompt/Provider loader 未落地”的旧表述降级为历史 baseline。
   - 显式改写为当前真实状态：`llm/include` / `llm/src`、Prompt 三段、ModelRouter、LLMManager、Prompt / Provider repositories、production factory、observability bridges、streaming lifecycle 与 focused tests 已落地；shared admission 继续保持 No-Go。
3. 3.1 / 3.2 / 3.3 / 3.4 当前状态与缺口
   - 将“缺接口、缺 adapter、缺 unit/integration、缺 route/fallback/observability”的旧 gap 改写为当前真实差距：`LLM-FIX-005` 的边界回归防线、historical L5 与 current rerun 的证据分层，以及 L6 soak 尚未完成。
   - 明确 D1-D9 当前已按 module-local owner 口径闭合，D10 已在 llm 内部完成 streaming lifecycle 收口，但 shared `StreamHandle` admission 仍然是 No-Go。
4. 专项 TODO / 总账 / worklog 同步
   - 在专项 TODO 中新增 `17.25 LLM-FIX-006`，并把当前状态更新收敛为“module-local implementation ready / shared admission No-Go / 历史 L5 与当前 rerun 不混写”。
   - 在总账中把 `LLM-FIX-006` 标记为 Done，同时保留 `LLM-FIX-005` 为剩余 owner。
   - 在 worklog 中新增独立记录，避免后续继续引用 placeholder-only baseline 作为当前事实。

## 3. 验证证据

1. `rg -n '当前只编译 src/placeholder.cpp|tests/unit/llm/CMakeLists.txt 仍为占位|tests/integration/llm 目录尚不存在|当前没有 LLMManager、ModelRouter、Prompt 三段或 adapter 实现' docs/architecture/DASALL_llm子系统详细设计.md; test $? -eq 1`
   - 结果：无命中；详设当前状态段落里最直接的 placeholder baseline 表述已移除。
2. `rg -n 'LLM-FIX-006|历史 baseline|shared admission No-Go|历史 authoritative qemu L5|current rerun|边界回归防线' docs/architecture/DASALL_llm子系统详细设计.md docs/todos/llm/DASALL_llm子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md docs/todos/DASALL_子系统查漏补缺专项记录.md`
   - 结果：命中详设、专项 TODO、worklog 与总账，确认 `LLM-FIX-006`、historical L5 / current rerun 分层、shared admission No-Go 与“剩余 owner 是 005”已形成跨文档追溯链。

## 4. 完成判定

LLM-FIX-006 已完成。

1. 新读者不会再从详设误判 llm 仍停留在 placeholder-only、空测试目录或“无 loader / 无 adapter / 无 route”阶段。
2. llm 当前态与历史 baseline 的时间点已经拆开：module-local implementation ready、shared admission No-Go、historical L5 与 current rerun / L6 soak 分层保持清晰。
3. 本轮完成后，llm owner 剩余开放项重新收敛为 `LLM-FIX-005` 的边界回归防线，而不是“当前状态文档仍严重落后”。