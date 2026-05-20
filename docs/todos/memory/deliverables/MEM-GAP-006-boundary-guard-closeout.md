# MEM-GAP-006 boundary guard closeout

来源任务：MEM-GAP-006
完成日期：2026-05-20
关联修复：MEM-FIX-005

## 1. 任务边界

1. 本轮只收口 `MEM-GAP-006`，不把 `MEM-GAP-004` 的 release boundary、`MEM-GAP-007` 的 installed maintenance 或其它更高层证据混入本轮。
2. authoritative 问题定义固定为：ADR-006 / ADR-007 / ADR-008 所要求的 Memory owner 边界，是否已由 build-tree 自动化守卫持续锁定，而不是继续依赖人工 grep 审查。
3. 若本轮复验通过，则 `MEM-GAP-006` 保持已闭合；若复验失败，才回到 boundary guard 测试或 contract 边界实现修复。

## 2. 现有本地证据

| 证据面 | 当前证据 | 对 closeout 的意义 |
|---|---|---|
| source boundary guard | `tests/unit/memory/MemoryBoundaryGuardComplianceTest.cpp` 扫描 `memory/include`、`memory/src` 与 `memory/CMakeLists.txt` 的禁区 include / link / owner 符号 | Memory 实现面越界会在 build-tree 直接失败 |
| contract boundary guard | `ContextPacketFieldContractTest` 持续锁定 `ContextPacket` 合同字段 | context 权边界不会被静默漂移到错误 owner |
| ADR owner mapping | ADR-006 / ADR-007 / ADR-008 已明确 ContextOrchestrator / RecoveryManager / AgentOrchestrator 的 owner 分工 | closeout 判断有固定架构裁决依据 |

## 3. 外部参考

1. ADR-006 / ADR-007 / ADR-008 已明确三条 owner 边界：ContextOrchestrator 留在 Memory、RecoveryManager 留在 Runtime、AgentOrchestrator 留在 Runtime 主控层。`MEM-GAP-006` 的 closeout 目标正是把这些架构裁决从“人工理解”收口为可执行的 build-tree guard。

## 4. Design -> Build 映射

| Design 目标 | Build / Test 目标 |
|---|---|
| Memory 不得反向占有 prompt / recovery / global orchestration 权限 | 测试目标：`MemoryBoundaryGuardComplianceTest` |
| `ContextPacket` 合同字段必须继续保持 owner 边界 | 测试目标：`ContextPacketFieldContractTest` |
| GAP closeout 必须以当前树 focused boundary validation 为准，不依赖 qemu / soak | 验收命令：聚焦 build + direct test binary |

## 5. D Gate

1. 范围单一：只处理 `MEM-GAP-006`。
2. 依赖方向不变：Memory 继续只掌语义上下文权，不上浮到 llm / runtime / tools / apps 的 owner 语义。
3. 本轮不修改产品代码；若验证失败，才回到 boundary guard 与 contract guard 的局部实现面。

## 6. 验证结果

1. `cmake --build build/vscode-linux-ninja --target dasall_memory_boundary_guard_compliance_unit_test dasall_contract_context_packet_field_test -j4`
	- 结果：通过；2 个 focused targets 均构建成功。
2. `./build/vscode-linux-ninja/tests/unit/memory/dasall_memory_boundary_guard_compliance_unit_test`
	- 结果：通过；退出码 `0`。
3. `./build/vscode-linux-ninja/tests/contract/dasall_contract_context_packet_field_test`
	- 结果：通过；输出 `20 passed, 0 failed`，退出码 `0`。

## 7. 完成判定

1. `MEM-GAP-006` 已关闭。
2. Memory owner 边界守卫与 `ContextPacket` 合同守卫在当前树上复验通过，ADR-006/007/008 的关键 owner 分工不再只能依赖人工 grep 审查。
3. 本结论不外推到 release-runner / installed 多轮证据；这些属于其它 gap 或更高层验证范围。