# CAP-TODO-041 shared-contract header 兼容评审与落位决策

日期：2026-04-10
任务：CAP-TODO-041
状态：D Gate PASS / 评审结论：当前直接迁移 No-Go，future Phase-Go

## 1. 本地证据

1. [docs/architecture/DASALL_capability_services子系统详细设计.md](../../../architecture/DASALL_capability_services子系统详细设计.md) 的 6.6 与 7.2 已冻结 shared-contract header 的边界：`IExecutionService` / `IDataService` 的 shared-contract 工作在 CAP-TODO-033 只完成 admission baseline，不在当轮直接把接口头迁入 `contracts/include`；物理落位必须由 CAP-TODO-041 单独给出兼容结论。
2. [docs/architecture/DASALL_Engineering_Blueprint.md](../../../architecture/DASALL_Engineering_Blueprint.md) 当前仍把 `services/include/` 作为 Capability Services 的公共接口槽位；蓝图的关键接口文件分布没有把 `IExecutionService.h`、`IDataService.h` 或 `ServiceTypes.h` 列到 `contracts/include/`。
3. [services/CMakeLists.txt](../../../../services/CMakeLists.txt) 已把 `include/ServiceTypes.h`、`include/IExecutionService.h`、`include/IDataService.h` 注册为 `dasall_services` 的 `PUBLIC` `HEADERS` file set，并以 `services/include` 作为 `BASE_DIRS`。这说明 `services/include` 不只是目录现状，而是已经进入 build graph 的公开 include 合同。
4. [tests/unit/services/ServiceHeaderLayoutTest.cpp](../../../../tests/unit/services/ServiceHeaderLayoutTest.cpp) 直接从 include 根包含 `IExecutionService.h`、`IDataService.h` 与 `ServiceTypes.h`，并锁定关键签名与 `deadline_ms` 类型。若在不保留兼容包装头的前提下直接搬移文件位置，该测试会首先回归。
5. 当前仓库对三份头文件的真实消费面主要集中在 services 内部与 tests： [services/src/ServiceFacade.h](../../../../services/src/ServiceFacade.h) 直接包含两个接口头，`ServiceTypes.h` 被 services 子域和 `tests/mocks/include/CapabilityServicesLoopbackFixture.h` 消费；本轮检索未发现 `tools/**` 下已有直接 include 这三份头的实现侧证据。
6. [contracts/include/boundary/InterfaceCatalog.h](../../../../contracts/include/boundary/InterfaceCatalog.h) 与 [contracts/include/boundary/InterfaceAdmissionGuards.h](../../../../contracts/include/boundary/InterfaceAdmissionGuards.h) 已把 services pair 的 shared-contract 语义固化为 `ReviewReady` / `Admit`，但这些规则只约束 catalog metadata 与 admission 结论，不要求接口头必须物理位于 `contracts/include`。
7. 当前 `contracts/include/` 顶层只有 `agent/`、`boundary/`、`checkpoint/`、`context/`、`error/`、`event/`、`llm/`、`memory/`、`observation/`、`prompt/`、`task/`、`tool/` 等目录，没有现成的 `services/` 分类；若现在强行迁移，不只是挪文件，还会一并引入新的 contracts taxonomy 决策。

## 2. 外部参考

1. Google AIP-180 明确指出：现有组件在同一主版本内不应直接移动到新的文件，因为这会改变 import/include 位置，构成 source compatibility break。该规则虽然针对 API/IDL，但对当前 C++ 头文件迁移同样适用。参考：https://google.aip.dev/180
2. CMake `target_sources(FILE_SET HEADERS)` 文档说明：`PUBLIC HEADERS` file set 的 `BASE_DIRS` 会追加到目标及其依赖者的 include properties。既然 `services/include` 已通过 `PUBLIC HEADERS` 进入 `dasall_services` 的构建接口，就不能把它当成“尚未承诺”的临时路径。参考：https://cmake.org/cmake/help/latest/command/target_sources.html

## 3. 兼容矩阵

| 消费面 | 当前证据 | 直接迁移到 `contracts/include` 的影响 | 评审结论 |
|---|---|---|---|
| services 组合根与内部源码 | [services/src/ServiceFacade.h](../../../../services/src/ServiceFacade.h) 直接 include `IDataService.h` / `IExecutionService.h`；多条 services 内部链路直接 include `ServiceTypes.h` | 需要同时改写 include 路径、public header file set 与源码依赖面 | 当前不支持直接迁移 |
| public header 布局门 | [tests/unit/services/ServiceHeaderLayoutTest.cpp](../../../../tests/unit/services/ServiceHeaderLayoutTest.cpp) 显式验证三份头文件可经 `services/include` 根可达 | 不保留兼容包装头就会立即破坏 source/include 兼容与单测门禁 | 当前不支持直接迁移 |
| tests/mocks 与 integration fixture | `ServiceTypes.h` 被 loopback fixture 和若干 tests-side 组件直接消费 | fixture 与 mock include 路径会同步漂移，增加无收益的 tests-side 改动面 | 只能在兼容窗口内分阶段迁移 |
| 未来 tools/runtime 消费者 | 设计要求 tools 未来依赖三份公共头，但当前仓库尚无真实 `tools/**` include 证据 | 即刻迁移没有现实收益，反而提前冻结 taxonomy 与路径 | 等待真实消费者出现后再评估 |
| shared-contract catalog 语义 | InterfaceCatalog / InterfaceAdmission 已把 services pair 固化为 `ReviewReady` / `Admit` | admission baseline 不依赖物理路径，不需要靠挪头文件证明 shared-contract 成立 | 保持现状即可 |

## 4. 评审结论

1. 当前直接把 `IExecutionService`、`IDataService`、`ServiceTypes` 迁入 `contracts/include`：No-Go。原因不是 shared-contract 语义不足，而是当前 canonical include 根已经被 `services/CMakeLists.txt` 和 `ServiceHeaderLayoutTest` 固化；直接挪文件会制造无收益的 source/include 兼容破坏。
2. CAP-TODO-033 的 `ReviewReady` 与 CAP-TODO-041 的物理落位评审是两个不同层次：前者回答“这是不是合格的 shared-contract interface candidate”，后者回答“头文件今天是否应该改址到 contracts/include”。041 的结论是 shared-contract 语义成立，但 physical placement 维持现状。
3. `services/include` 继续作为当前 canonical public include 根；`contracts/include` 暂不新增 `services/` 目录，也不在本轮创建转发头或双路径导出。
4. future 只允许 Phase-Go，不允许无条件直接 Go。若后续要重新发起 shared header 升格，必须同时满足以下前提：
   - 出现至少一个真实跨模块消费者，并能证明迁入 `contracts/include` 有实际复用收益，而不是只满足目录洁癖。
   - 架构文档与工程蓝图先明确 `contracts/include/services/` 或等价 taxonomy，避免在代码任务中顺带做目录政策决策。
   - 保留 `services/include` 旧路径作为兼容包装头至少一个迁移窗口，避免 source/include break。
   - 同步更新 `ServiceHeaderLayoutTest`、`InterfaceCatalogContractTest`、`InterfaceAdmissionContractTest` 与相关 deliverable/worklog，确保 admission 结论、include 入口与测试门禁一致。
5. 因为两个接口头都依赖 `ServiceTypes.h`，future 若发生 shared header 升格，只能把三份头视为一个兼容迁移单元，而不能拆成只迁接口、不迁 supporting objects 的半迁移状态。

## 5. Design -> Build 映射

| Design 项 | Build 落点 |
|---|---|
| shared-contract header 兼容评审结论 | 本文件 |
| 保持 `services/include` 作为当前 canonical public include 根 | docs/architecture/DASALL_capability_services子系统详细设计.md、docs/todos/services/DASALL_capability_services子系统专项TODO.md |
| 记录 041 的任务选择、验证命令与后续 Phase-Go 前提 | docs/worklog/DASALL_开发执行记录.md |

## 6. Build 三件套

1. 代码目标：完成 `IExecutionService` / `IDataService` / `ServiceTypes` shared-contract header 的兼容评审，明确是否迁入 `contracts/include` 的结论、consumer 影响矩阵与 future Phase-Go 前提；本轮不移动任何生产头文件。
2. 测试目标：保持 InterfaceCatalog / InterfaceAdmission contract 基线不回退，证明 041 是 docs-only 决策任务，不改变 shared-contract admission 语义。
3. 验收命令：
   - `cmake -S . -B build-ci -G "Unix Makefiles"`
   - `cmake --build build-ci --target dasall_contract_tests`
   - `ctest --test-dir build-ci --output-on-failure -R InterfaceCatalogContractTest`
   - `ctest --test-dir build-ci --output-on-failure -R InterfaceAdmissionContractTest`
   - `ctest --test-dir build-ci --output-on-failure -L contract`

## 7. 风险与回退

1. 最大风险不是“暂时不迁”，而是把 `ReviewReady` 误读成“必须立刻迁移头文件”。041 已明确两者不是同一件事；若后续跳过 compat wrapper 与迁移窗口直接改址，将主动制造 source/include 回归。
2. 若 future 真的要发起 shared header 升格，必须先回写架构文档和蓝图里的目录分类，再落 compat wrapper；不要在代码任务里一边建 taxonomy、一边改 include、一边要求消费者同步升级。
3. 本轮不改变任何生产代码或 contract metadata；若后续评审认为需要重新打开 041，只能基于新的 consumer evidence 发起新任务，而不是回滚当前的 No-Go 结论去做无证据迁移。