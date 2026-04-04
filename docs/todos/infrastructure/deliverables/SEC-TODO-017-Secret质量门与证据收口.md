# SEC-TODO-017 Secret 质量门与证据收口

日期：2026-04-04
任务：SEC-TODO-017
状态：已完成

## 1. 输入依据

1. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 已将 SEC-TODO-017 定义为“回写 secret 质量门与交付证据”，要求给出门禁结论、阻塞变化与回退证据。
2. SEC-TODO-015 已完成 unit/contract discoverability，SEC-TODO-016 已完成 integration discoverability，因此 secret 当前可执行测试入口已覆盖三层。
3. SEC-BLK-003 仍未解阻，因此本轮既要回写 PASS gate，也要保留当前仍 BLOCKED 的 KMS 后续入口，不能伪造“全范围已完成”。

## 2. 研究学习结果

### 2.1 本地证据

1. `ctest --test-dir build-ci -N -L secret` 当前可发现 20 个测试，覆盖 13 个 unit、5 个 contract 与 2 个 integration 测试。
2. `ctest --test-dir build-ci --output-on-failure -L secret` 当前可一次性执行上述 20 个 secret 标签测试，天然适合作为 secret 子域统一质量门。
3. secret TODO 的 9.1 基线说明此前仍停留在“017 待回写”的中间态，尚未将 `ctest -L secret` 正式收口为当前 gate 基线。

### 2.2 可落地启发

1. 017 最稳妥的做法不是重复拆分 unit/contract/integration 命令，而是把 015/016 已经铺好的 `secret` 标签直接收口成统一 gate。
2. PASS gate、残余 BLOCKED 项和回退证据必须并列表达，否则后续继续推进 KMS 真实接入时容易误以为 secret 全域已经闭环。

## 3. Design 原子清单

| D 子项 | 设计目标 | 输入依据 | 产出 | 完成判定 |
|---|---|---|---|---|
| D1 | 收口 secret 当前 gate 基线 | TODO 9.1；015/016 完成状态 | 更新后的 TODO 9.1 | 基线命令可直接覆盖 secret 三层测试 |
| D2 | 收口 PASS gate 结论 | Gate 表；当前测试结果 | TODO 9.4 gate 结论表 | 每个 gate 都有明确 PASS/BLOCKED 结论 |
| D3 | 收口 blocker 变化与回退证据 | blocker 表；010/012/016 测试结果 | TODO 9.5 摘要表 | blocker 与 rollback/failure evidence 可追溯 |
| D4 | 收口后续推进入口 | blocker 表；11.5 | deliverable / worklog / TODO 下一步 | 后续入口明确转向 SEC-BLK-003 或新 v2 范围 |

## 4. D Gate 结论

### 4.1 Design -> Build 映射

| Design 结论 | Build 落地 |
|---|---|
| secret 三层测试统一走 `ctest -L secret` | 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md 9.1 |
| PASS gate 与 blocker/rollback 结论要显式表格化 | 更新同文件 9.4 与 9.5 |
| 当前轮收口结果要进入交付件与执行记录 | 新增本交付件，并更新 docs/worklog/DASALL_开发执行记录.md |

### 4.2 Build 三件套

1. 代码目标：更新 TODO、deliverable、worklog，并将 secret 当前 gate 基线收口为 `ctest -L secret`。
2. 测试目标：执行全量 unit/contract/integration gate，并追加 `ctest -L secret` 验证 secret 标签矩阵。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_integration_tests`
   - `ctest --test-dir build-ci -N`
   - `ctest --test-dir build-ci --output-on-failure -L unit`
   - `ctest --test-dir build-ci --output-on-failure -L contract`
   - `ctest --test-dir build-ci --output-on-failure -L integration`
   - `ctest --test-dir build-ci -N -L secret`
   - `ctest --test-dir build-ci --output-on-failure -L secret`

### 4.3 D Gate

结论：PASS。

理由：

1. 当前 secret 可执行测试范围已完整覆盖 unit/contract/integration 三层，并能通过 `secret` 标签统一聚合。
2. 残余未解阻项已明确收缩到 `SEC-BLK-003` 的 KMS 真实接入前置条件，不存在伪装完成的证据空档。

## 5. Build 落地结果

1. 更新 docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md，将 SEC-TODO-017 标记为 Completed，并补齐 9.1 secret 专项 gate 基线、9.4 gate 执行结论表、9.5 blocker/rollback 摘要与新的下一步建议。
2. 新增 docs/todos/infrastructure/deliverables/SEC-TODO-017-Secret质量门与证据收口.md，收口本轮 gate 结果、blocker 变化与回退证据。
3. 更新 docs/worklog/DASALL_开发执行记录.md，新增本轮质量门收口记录。

## 6. 验证结果

1. `cmake -S . -B build-ci -G "Unix Makefiles"`：通过。
2. `cmake --build build-ci --target dasall_unit_tests dasall_contract_tests dasall_integration_tests`：通过。
3. `ctest --test-dir build-ci -N`：通过。
4. `ctest --test-dir build-ci --output-on-failure -L unit`：119/119 通过；标签摘要中 `secret=13`。
5. `ctest --test-dir build-ci --output-on-failure -L contract`：133/133 通过；标签摘要中 `secret=5`。
6. `ctest --test-dir build-ci --output-on-failure -L integration`：13/13 通过；标签摘要中 `secret=2`。
7. `ctest --test-dir build-ci -N -L secret`：发现 20 个测试。
8. `ctest --test-dir build-ci --output-on-failure -L secret`：20/20 通过；标签摘要为 `unit=13`、`contract=5`、`integration=2`。

## 7. 结论

1. SEC-TODO-017 已将 secret 当前轮从“任务逐个完成”推进到“拥有统一 secret gate 基线、8 个 gate 结论表和 blocker/rollback 摘要”的可追溯收口状态。
2. 当前 secret 组件专项 TODO 中 001~017 已全部完成；后续若继续推进，应转向 SEC-BLK-003 的 KMS 解阻或另起 KMS/profile 扩展范围的新原子任务。