# DMD-TODO-040 daemon 文档与证据复验收敛

状态：Done
日期：2026-05-02
来源 TODO：docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md

## 1. 任务边界

1. 本任务只清理 daemon 专项的过期文档口径、陈旧 ping smoke 资产与 Gate 复验证据，不引入新的 runtime/product 语义。
2. 本任务以前置完成的 `DMD-TODO-036` ~ `039` 为基础，只负责把默认 endpoint、socket mode/stale restart、entry/reload 真接线的最终状态回写到文档和测试拓扑。
3. 本任务不重做 024/028/035 的实现，只删除 legacy send-only ping smoke，并以 focused regression 证明当前权威测试拓扑保持稳定。

## 2. 根因与设计结论

### 2.1 根因

1. 2026-05-02 评审后，专项文档仍保留 readiness 消费缺口与 reopen hardening 的旧口径，容易把已经完成的整改状态继续描述成未收口。
2. 仓库里还留着未注册的 legacy send-only ping smoke 文件；它不再代表权威测试拓扑，却会继续制造“仓库里还有旧 smoke”的假象。
3. `Gate-DMD-05` / `07` / `09` 的首轮基线证据已经存在，但整改完成后的复验证据还没有集中回写到专项 TODO / deliverable / worklog。

### 2.2 本轮结论

1. 过期字面口径必须从 daemon 专项文档中移除，避免 review snapshot 与当前实现状态混淆。
2. legacy send-only ping smoke 必须直接删除，而不是继续作为未注册文件留在仓库中。
3. `Gate-DMD-10` 需要以“负向 grep + focused ctest”作为 close-ready 证据，同时显式说明 `Gate-DMD-05` / `07` / `09` 已按整改后结果复验。

## 3. Design -> Build 映射

| 设计结论 | Build 落点 | 验收信号 |
|---|---|---|
| 过期文档口径需要清除 | `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`、`DASALL-daemon本地控制面专项评审报告_2026-05-02.md`、`deliverables/DMD-TODO-024-daemon-ping-integration收敛.md` | negative grep 不再命中过期短语 |
| legacy send-only ping smoke 需要退出仓库 | `tests/integration/access` 中的旧未注册 ping smoke 文件 | 文件已删除，且现有 `DaemonPingIntegrationTest` 继续通过 |
| close-ready 结论需要补 Gate-DMD-10 | `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`、本交付物、`docs/worklog/DASALL_开发执行记录.md` | Gate-DMD-10 PASS，且 `Gate-DMD-05` / `07` / `09` 复验证据可追溯 |

## 4. 落盘结果

1. 删除 `tests/integration/access` 中旧的未注册 send-only ping smoke 文件，彻底移除历史遗留入口。
2. 更新 `docs/todos/daemon/DASALL-daemon本地控制面专项评审报告_2026-05-02.md`：
   - 把当时的过期文档/测试问题改写为历史性描述；
   - 去除对已删除 legacy 文件的直接路径引用。
3. 更新 `docs/todos/daemon/deliverables/DMD-TODO-024-daemon-ping-integration收敛.md`，把旧 smoke 描述统一为 legacy send-only ping smoke，避免继续保留已删除文件名。
4. 更新 `docs/todos/daemon/DASALL-daemon本地控制面专项TODO.md`：
   - 将当前结论切换为 `DMD-TODO-001` ~ `040` 全部完成、专项恢复 close-ready；
   - 将 `DMD-TODO-040` 更新为 Done；
   - 在 §9.4 补齐 `Gate-DMD-10` PASS；
   - 在 §11 将执行结论从 reopen hardening 更新为整改完成后的 close-ready 口径。
5. 更新 `tests/unit/apps/daemon/CMakeLists.txt`，为 `apps/daemon` unit-test 拓扑补齐 `access/include`，保证共享 daemon public 头在 focused build 中可见。
6. 更新 `docs/worklog/DASALL_开发执行记录.md`，新增 040 的复验证据记录。

## 5. Validation

1. `rg -n "当前 CLI 尚未消费 readiness 响[应]|CliDaemonPingIntegrationTe(st)|专项状态从 fully-close[d]" docs/deploy/daemon docs/todos/daemon tests/integration/access`
   - 结果：无匹配，说明过期字面口径与 legacy 文件名已从专项文档/测试拓扑中清除。
2. `RunCtest_CMakeTools(tests=["DaemonPingIntegrationTest","CliDaemonCommandParserTest","DaemonConfigReloadTest","DaemonProfileCompatibilityTest"])`
   - 结果：通过，4/4 测试通过；`DartConfiguration.tcl` 缺失提示仍为仓库既有工具链噪声，不影响返回码与 focused 结论。

## 6. Gate 回写结论

1. `Gate-DMD-05` 复验通过：`DaemonPingIntegrationTest` 与 `CliDaemonCommandParserTest` 继续证明 CLI 读取真实 daemon response，且 ping/readiness 语义未回退到 send-only smoke。
2. `Gate-DMD-07` 复验通过：`DaemonConfigReloadTest` 与 `DaemonProfileCompatibilityTest` 继续证明 hot-reload allowlist 与 profile 兼容性未因文档/证据清理回退。
3. `Gate-DMD-09` 复验通过：专项 TODO、deliverable 与 worklog 现在已同步包含 040 的最终证据与 close-ready 结论。
4. `Gate-DMD-10` 通过：默认 endpoint、真实 socket mode/stale restart、entry/reload 真接线与文档/测试复验已经同时完成。

## 7. 完成判定

DMD-TODO-040 已完成。判定依据：

1. daemon 专项文档、评审快照和历史 deliverable 中不再残留过期字面口径。
2. legacy send-only ping smoke 已从仓库删除，不再制造未注册旧测试拓扑的假象。
3. `apps/daemon` unit-test 拓扑已补齐共享 daemon public 头的 include 可见性，不再因 `DaemonEndpointDefaults.h` 触发假性构建失败。
4. `Gate-DMD-05` / `Gate-DMD-07` / `Gate-DMD-09` / `Gate-DMD-10` 的 focused 复验证据已回写到 TODO、deliverable 与 worklog。
5. daemon 本地控制面专项已恢复为 close-ready 结论；剩余仅为仓库级工具链噪声与 v2 范围外能力，不再是本专项 reopen 项。