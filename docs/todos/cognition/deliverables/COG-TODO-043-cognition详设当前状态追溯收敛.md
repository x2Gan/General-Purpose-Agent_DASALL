# COG-TODO-043 cognition 详设当前状态追溯收敛

状态：Done
日期：2026-05-15
来源 TODO：docs/todos/cognition/DASALL_cognition子系统专项TODO.md
任务类型：详设当前状态与证据链同步

## 1. 任务边界

1. 本任务只修正 `docs/architecture/DASALL_cognition子系统详细设计.md` 中已经明显落后的“当前状态”表述，并把 039 ~ 042 的证据链入口补齐。
2. 本任务不改写 ADR-006 / 007 / 008 结论，不新增生产代码，也不把 module-local 支撑类型外推到 shared contracts。
3. 本任务只解决“新读者会被详设误导”的问题；warning hygiene 仍由 COG-TODO-044 单独收口。

## 2. 收敛内容

1. 元数据与主要依据
   - 将详设最近修订时间更新到 2026-05-15。
   - 在“主要依据”中补入 COG-TODO-039 ~ 042 deliverables，形成从详设到专项 TODO / deliverable 的直接追溯入口。
2. 1.2 当前实现状态
   - 把“仅有静态库占位和空测试目录”改为当前真实状态：`cognition/include` 已具备 Runtime-facing 公共接口面，Facade、五段组件、LLM bridge、validator、telemetry 已落盘，unit / integration 拓扑已形成。
   - 显式声明 `placeholder.cpp` 仅为早期 bootstrap 痕迹，不再代表当前工程状态。
3. 3.1 现状-目标差距表
   - 将“模块代码骨架 / 公共接口面 / 阶段组件 / 单元测试 / 集成测试 / 测试 mocks”六行从早期 baseline 改写为当前状态，并把剩余 gap 收口为 repo-wide gate blocker、044 warning hygiene 与 shared-contract 边界守护。
   - 明确 `RuntimeCognitionLoopSmokeTest` 已经穿过 cognition 主链，不再把 runtime smoke 描述为“绕过 cognition”的当前事实。
4. 3.2 关键差距结论
   - 把“当前核心缺口是接口面与支撑类型缺失”改为当前真实缺口：Gate-COG-12 仍被 non-cognition blocker 与 043 / 044 文档、warning hygiene 收口项卡住。

## 3. 验证证据

1. `rg -n "placeholder.cpp|空测试目录|绕过 cognition|Gate-COG-12|COG-TODO-04[0-3]" docs/architecture/DASALL_cognition子系统详细设计.md docs/todos/cognition/DASALL_cognition子系统专项TODO.md docs/worklog/DASALL_开发执行记录.md`
   - 结果：详设中的 `placeholder.cpp`、空测试目录与 runtime smoke 旧表述已退回为“历史 baseline / 证据引用”语境；`Gate-COG-12` 与 `COG-TODO-040 ~ 043` 已形成跨文档追溯入口。

## 4. 完成判定

COG-TODO-043 已完成。

1. 新读者不会再从详设误判 cognition 仍停留在 placeholder-only、空测试目录或 runtime smoke 绕过 cognition 的早期状态。
2. 039 ~ 042 的交付证据已经能从详设直接追溯到专项 TODO / deliverables。
3. 当前文档剩余开放项已经收敛到 COG-TODO-044 的 warning hygiene，而不是“详设与代码现实完全脱节”。