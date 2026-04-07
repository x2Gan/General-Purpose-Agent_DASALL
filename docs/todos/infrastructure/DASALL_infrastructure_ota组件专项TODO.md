# DASALL infrastructure 子系统 ota 组件专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：infra/ota

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_infra_OTA模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts/
12. docs/todos/infrastructure/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/infrastructure/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/infrastructure/DASALL_infrastructure_tracing组件专项TODO.md
15. docs/todos/infrastructure/DASALL_infrastructure_metrics组件专项TODO.md
16. docs/todos/infrastructure/DASALL_infrastructure_config组件专项TODO.md
17. docs/todos/infrastructure/DASALL_infrastructure_secret组件专项TODO.md
18. docs/todos/infrastructure/DASALL_infrastructure_health组件专项TODO.md
19. docs/todos/infrastructure/DASALL_infrastructure_watchdog组件专项TODO.md
20. 代码与构建现状：infra/CMakeLists.txt、infra/include/、infra/src/ota/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、tests/contract/CMakeLists.txt、build-ci/build.ninja

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 infrastructure/ota 组件边界扩张到无关模块。
3. 讨论类事项不作为 Done-ready Build 任务。
4. 每项任务必须包含代码目标、测试目标、验收命令。
5. 设计证据不足处先列 Blocked 与补设计前置项，不伪造实现任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 提供 OTA 最小闭环：预检、验签、安装、启动确认、失败回滚。
2. 支持 slot_bound 与 repo_bound 工件分层升级，满足 A/B 或等价冗余切换约束。
3. 输出可回放升级证据链：UpgradeOutcome、InstallEvidence、RollbackEvidence、审计事件。
4. 支持 profile 可裁剪实现，不改变上层调用语义。

### 2.2 范围边界

纳入范围：

1. ota 接口、数据结构、错误语义、主异常流程、配置模型。
2. ota 的 CMake 接线、unit/contract/integration/failure 测试入口规划。
3. 质量门、阻塞项、风险与回退策略。

不纳入范围：

1. 云端仓库与镜像生产流水线实现。
2. runtime 对失败的全局 replan 或 abort_safe 裁定。
3. contracts 共享对象重定义。
4. Uptane 全量双仓库元数据链实现（仅保留演进方向）。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的影响 |
|---|---|---|---|---|
| OTA-TC001 | OTA 设计 1.1/6.8；架构 8.9/9.3 | Must | OTA 必须支持预检、验签、安装、确认、回滚闭环 | 任务必须覆盖对象、接口、流程、异常路径 |
| OTA-TC002 | 架构 3.7；蓝图 4.2 | Must | infra 不反向依赖业务实现 | 代码目标限定在 infra/tests/docs/cmake |
| OTA-TC003 | 蓝图 4.3 | Must | 跨模块通过 contracts 冻结接口 | 错误映射与标识语义仅复用现有 contracts |
| OTA-TC004 | ADR-005 | Must | 不得用 ota 反向改写主架构与 contracts 结论 | 证据不足项必须 Blocked |
| OTA-TC005 | ADR-006 | Must-Not | ota 不参与上下文装配与 Prompt 渲染 | 禁止生成 cognition/llm 语义任务 |
| OTA-TC006 | ADR-007 | Must-Not | ota 不做失败语义裁定 | 仅执行本地回滚并返回结果 |
| OTA-TC007 | ADR-008 | Must | ota 不拥有全局调度权 | 任务不得引入主控状态机推进逻辑 |
| OTA-TC008 | OTA 设计 2.2；contracts 冻结约束 | Must-Not | slot/bootloader/签名实现细节不进入 contracts | 私有对象留在 infra/ota 域 |
| OTA-TC009 | 编码规范 3.6 | Must | 高风险失败必须可观测，不得吞错 | 异常任务必须绑定日志/指标/审计 |
| OTA-TC010 | 编码规范 3.7 | Should | 公共接口应配套测试 | 接口任务需绑定 unit/contract |
| OTA-TC011 | OTA 设计 6.10；蓝图 5.1 | Must | profile 只裁剪能力，不绕过审计与主控链路 | 配置任务必须带覆盖策略 |
| OTA-TC012 | 工程落地指引 阶段 C | Must | infra 底座任务必须可验证 | 执行顺序先边界冻结再链路实现 |
| OTA-TC013 | OTA 设计 12.1 | Must | 存在未决设计项（token 持久化、签名算法、confirm 成功准则等） | 必须单列补设计前置与阻塞门 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| infra/CMakeLists.txt | 已接入 core/audit/plugin/tracing 等真实源码 | ota 公共接口已落盘，但 ota 具体实现尚未入构建图 |
| infra/include/ | 已形成“根目录共享契约 + 组件目录公共接口”布局，ota/ 子目录已落盘接口与对象 | ota public headers 已冻结，后续差距集中在升级/回滚实现闭环 |
| infra/src/ota/ | 目录存在但无源文件 | ota 实现主链未落盘 |
| tests/CMakeLists.txt | 已接入 mocks/unit/contract/integration 并提供 dasall_integration_tests 聚合入口 | integration/failure 门禁拓扑已可承载 OTA 用例，后续只需补 OTA 具体集成/故障测试 |
| build-ci/build.ninja | 有 dasall_infra/dasall_unit_tests/dasall_contract_tests 目标 | 可用统一构建与标签测试命令 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成并执行 L3/L2 混合专项 TODO。  
当前最小可执行粒度：函数/接口/数据结构级（L3 为主，局部 L2，受阻项降为 L0 Blocked）。

证据：

1. 有明确接口清单：IOTAManager、IOTAPackageVerifier、IInstallExecutor、IBootControlAdapter、IOTAHealthProbe（OTA 设计 6.7）。
2. 有核心对象字段：UpgradePlan、PackageDescriptor、ArtifactDescriptor、VerifiedPackageManifest、RollbackToken、InstallEvidence、UpgradeOutcome、OTAStatusSnapshot（OTA 设计 6.5）。
3. 有主流程与异常流程：正常 apply 时序、validate_only 时序、五类异常与恢复动作（OTA 设计 6.8/6.9）。
4. 有错误语义与配置约束：错误码建议清单、配置键与默认策略（OTA 设计 6.7/6.10）。
5. 有落盘建议与测试门：目录、Design->Build 映射、测试矩阵与 Gate（OTA 设计 7/8/9）。
6. 仍有阻塞：rollback token 持久化策略、签名算法首版选择、confirm 成功准则、tests integration 顶层接线（OTA 设计 12.1 + 当前代码现状）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| IOTAManager | OTA 设计 6.7/6.8 | L3 | 方法语义、前置条件、状态查询语义 | query_status 状态持久化窗口细节 | 直接拆接口冻结任务 |
| IOTAPackageVerifier | OTA 设计 6.7/6.9 | L3 | 包校验与工件校验语义 | 签名算法首版选择未决 | 直接拆接口任务 + 签名补设计前置 |
| IInstallExecutor | OTA 设计 6.7/6.8 | L3 | stage/activate/revert 职责明确 | stage 目录规范细节 | 直接拆接口与实现骨架任务 |
| IBootControlAdapter | OTA 设计 6.7/6.8 | L3 | 4 个核心动作明确 | 不同平台实现边界细节 | 直接拆接口任务 |
| OTATypes 核心对象组 | OTA 设计 6.5/6.6 | L3 | 字段清单与 contracts 对齐关系明确 | token 存储位置未决 | 直接拆数据结构任务 + 补设计前置 |
| OTAPrecheckService | OTA 设计 6.2/6.3/6.8 | L3 | 输入输出与阻断条件明确 | 策略阈值来源细节 | 直接拆实现骨架任务 |
| PackageVerifier | OTA 设计 6.2/6.8/6.9 | L3 | 验签、hash、release_counter 语义明确 | trust anchor 接口细节 | 直接拆实现任务 + 补设计前置 |
| ArtifactCompatibilityEvaluator | OTA 设计 6.2/6.8 | L3 | compatibility 约束与失败路径明确 | dependency_refs 冲突规则细节 | 直接拆实现任务 |
| SlotSwitchCoordinator | OTA 设计 6.2/6.8/6.9 | L3 | inactive slot 选择、切换语义明确 | boot adapter 平台差异 | 直接拆实现任务 |
| BootConfirmationMonitor | OTA 设计 6.2/6.8/6.9 | L2 | confirm 窗口与超时失败明确 | confirm 成功判据未冻结 | 先补设计再进实现 |
| RollbackController | OTA 设计 6.2/6.9 | L3 | rollback 路径和失败可观测约束明确 | token 持久化细节未冻结 | 直接拆实现任务，持久化先 Blocked |
| OTAAuditBridge/OTAHealthProbe | OTA 设计 6.2/6.11 | L3 | 审计字段与健康信号清单明确 | 审计 fallback 优先级细节 | 直接拆桥接任务 |
| tests/integration/infra/ota | OTA 设计 8.1/9.1 | L0 | 测试层与关键用例明确 | tests 顶层 integration 已接线，缺口转为 OTA 具体集成用例尚未落盘 | 直接拆 OTA integration/failure 任务 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 冻结 OTA 私有对象模型 | 6.5/6.6 | 数据结构 | OTA-TODO-001 | 先冻结字段与边界，解除实现分歧 |
| 建立 IOTAManager 对外稳定入口 | 6.7 | 接口 | OTA-TODO-002 | 固定 precheck/apply/rollback/query_status 语义 |
| 建立 verifier/executor/boot adapter 接口面 | 6.7 | 接口 | OTA-TODO-003、004、005 | 先固化抽象边界，避免直连平台细节 |
| 建立预检闭环 | 6.2/6.8 | 生命周期与初始化 | OTA-TODO-006 | 把 health/resource/policy 前置阻断显式化 |
| 建立验签与兼容性闭环 | 6.2/6.8/6.9 | 适配器/桥接 | OTA-TODO-007、008 | 验签与兼容性拆分，降低任务耦合 |
| 建立安装与切换闭环 | 6.2/6.8 | 流程 | OTA-TODO-009、010 | stage 与 switch 分离，单目标推进 |
| 建立确认与回滚闭环 | 6.2/6.9 | 异常与错误处理 | OTA-TODO-011、012 | confirm 与 rollback 单独验收 |
| 建立审计与健康出口 | 6.11 | 测试/门禁 | OTA-TODO-013、014 | 高风险动作必须审计，健康状态可观测 |
| ota 入构建图与测试发现性 | 7/8/9 + 代码现状 | 注册点/门禁 | OTA-TODO-015、016、017 | 先 build/unit/contract，再 integration |
| 未决设计项收敛 | 12.1 | 文档/交付证据回写 | OTA-TODO-018、019、020、021 | 把缺口显式前置，避免伪造实现任务 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 说明 |
|---|---|---|
| 接口定义类任务 | 是 | OTA-TODO-002~005 |
| 数据结构定义类任务 | 是 | OTA-TODO-001 |
| 生命周期与初始化类任务 | 是 | OTA-TODO-006 |
| 适配器/桥接类任务 | 是 | OTA-TODO-007~008、013~014 |
| 异常与错误处理类任务 | 是 | OTA-TODO-011~012 |
| 配置与 Profile 裁剪类任务 | 是 | OTA-TODO-021 |
| 测试与门禁类任务 | 是 | OTA-TODO-016~017 |
| 文档/交付证据回写类任务 | 是 | OTA-TODO-018~020 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OTA-TODO-001 | Done | 定义 OTATypes 核心对象头文件组 | OTA 设计 6.5/6.6 | 6.5 核心对象表 | L3 | infra/include/ota/OTATypes.h | UpgradePlan/PackageDescriptor/ArtifactDescriptor/VerifiedPackageManifest/RollbackToken/InstallEvidence/UpgradeOutcome/OTAStatusSnapshot | unit：对象字段完整性；contract：仅复用 ResultCode/ErrorInfo/ID 语义 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra && cmake --build build-ci --target dasall_contract_tests && ctest --test-dir build-ci --output-on-failure -L contract | 无 | 无 | 无 | 头文件、字段说明、contract 测试记录；2026-04-01 已落盘 infra/include/ota/OTATypes.h、tests/unit/infra/OTATypesTest.cpp、tests/contract/smoke/OTATypeBoundaryContractTest.cpp，并更新 infra/tests CMake 注册；已通过 cmake --build build-ci --target dasall_ota_types_unit_test dasall_contract_ota_type_boundary_test 与 ctest --test-dir build-ci --output-on-failure -R "OTATypesCompileTest|OTATypeBoundaryTest" | 仅当对象字段与 6.5 一致且 contract 测试未发现越权字段时完成 |
| OTA-TODO-002 | Done | 定义 IOTAManager 接口头文件 | OTA 设计 6.7 | 6.7 IOTAManager | L3 | infra/include/ota/IOTAManager.h | precheck/apply/rollback/query_status | unit：接口可编译；contract：返回对象不扩写 contracts | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | OTA-TODO-001 | 无 | 无 | 接口头文件、编译记录；2026-04-01 已落盘 infra/include/ota/IOTAManager.h、tests/unit/infra/IOTAManagerInterfaceTest.cpp、tests/contract/smoke/OTAManagerInterfaceBoundaryContractTest.cpp，并更新 infra/tests CMake 注册；已通过 cmake --build build-ci --target dasall_ota_manager_interface_unit_test dasall_contract_ota_manager_interface_boundary_test 与 ctest --test-dir build-ci --output-on-failure -R "OTAInterfaceCompileTest|OTAManagerInterfaceBoundaryContractTest" | 仅当方法集合、输入输出与 6.7 一致且可编译时完成 |
| OTA-TODO-003 | Done | 定义 IOTAPackageVerifier 接口头文件 | OTA 设计 6.7/6.9 | 6.7 IOTAPackageVerifier | L3 | infra/include/ota/IOTAPackageVerifier.h | verify_package/verify_artifact | unit：接口编译；failure：签名失败路径可表达 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | OTA-TODO-001 | 无（2026-04-01 已由 OTA-TODO-019 解阻） | 无 | 接口头文件、阻塞记录或编译记录；2026-04-01 已落盘 infra/include/ota/IOTAPackageVerifier.h、tests/unit/infra/OTAPackageVerifierInterfaceTest.cpp、tests/contract/smoke/OTAPackageVerifierInterfaceBoundaryContractTest.cpp，并更新 infra/tests CMake 注册；已通过 cmake --build build-ci --target dasall_ota_package_verifier_interface_unit_test dasall_contract_ota_package_verifier_interface_boundary_test 与 ctest --test-dir build-ci --output-on-failure -R "OTAPackageVerifierInterfaceTest|OTAPackageVerifierInterfaceBoundaryContractTest" | 仅当接口签名稳定且签名依赖边界已说明时完成 |
| OTA-TODO-004 | Done | 定义 IInstallExecutor 接口头文件 | OTA 设计 6.7/6.8 | 6.7 IInstallExecutor | L3 | infra/include/ota/IInstallExecutor.h | stage_artifact/activate_plan/revert_install | unit：接口可编译；unit：InstallEvidence 绑定关系校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | OTA-TODO-001 | 无 | 无 | 接口头文件、编译记录；2026-04-01 已落盘 infra/include/ota/IInstallExecutor.h、tests/unit/infra/InstallExecutorInterfaceTest.cpp、tests/contract/smoke/InstallExecutorInterfaceBoundaryContractTest.cpp，并更新 infra/tests CMake 注册；已通过 cmake --build build-ci --target dasall_install_executor_interface_unit_test dasall_contract_install_executor_interface_boundary_test 与 ctest --test-dir build-ci --output-on-failure -R "InstallExecutorInterfaceTest|InstallExecutorInterfaceBoundaryContractTest" | 仅当安装与回退动作被明确解耦且接口可编译时完成 |
| OTA-TODO-005 | Done | 定义 IBootControlAdapter 接口头文件 | OTA 设计 6.7/6.8 | 6.7 IBootControlAdapter | L3 | infra/include/ota/IBootControlAdapter.h | get_active_target/set_next_boot/mark_boot_success/mark_boot_failed | unit：接口可编译；integration：可被 mock 适配器替换 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | OTA-TODO-001 | 无 | 无 | 接口头文件、编译记录；2026-04-01 已落盘 infra/include/ota/IBootControlAdapter.h、tests/unit/infra/BootControlAdapterInterfaceTest.cpp、tests/contract/smoke/BootControlAdapterInterfaceBoundaryContractTest.cpp，并更新 infra/tests CMake 注册；已通过 cmake --build build-ci --target dasall_boot_control_adapter_interface_unit_test dasall_contract_boot_control_adapter_interface_boundary_test 与 ctest --test-dir build-ci --output-on-failure -R "BootControlAdapterInterfaceTest|BootControlAdapterInterfaceBoundaryContractTest"；mock 可替换性通过 BootControlAdapterInterfaceTest 中基类引用驱动流程验证 | 仅当 4 个动作完整且不依赖具体 bootloader 头文件时完成 |
| OTA-TODO-006 | Done | 实现 OTAPrecheckService 骨架 | OTA 设计 6.2/6.3/6.8/6.10 | 6.2 OTAPrecheckService；6.8 预检失败路径 | L3 | infra/src/ota/OTAPrecheckService.cpp | precheck(plan) | unit：health/resource/policy 任一 hard-fail 阻断 apply | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-001、002 | 无 | 无 | 骨架实现、unit 测试、deliverable；2026-04-07 已落盘 infra/src/ota/OTAPrecheckService.h、infra/src/ota/OTAPrecheckService.cpp、tests/unit/infra/ota/OTAPrecheckServiceTest.cpp、docs/todos/infrastructure/deliverables/OTA-TODO-006-OTAPrecheckService骨架收敛.md，并更新 infra/tests CMake 接线；为恢复本任务的 `dasall_unit_tests` 聚合验收，同轮最小修复 tests/unit/infra/DiagnosticsSnapshotExportTest.cpp 中过期的 `CommandExecutionResult::success(...)` 调用签名；已通过 `cmake -S . -B build-ci`、`cmake --build build-ci --target dasall_infra dasall_ota_precheck_service_unit_test`、`ctest --test-dir build-ci -N -R "OTAPrecheckServiceTest"`、`ctest --test-dir build-ci --output-on-failure -R "OTAPrecheckServiceTest"`、`cmake --build build-ci --target dasall_unit_tests` 与 `ctest --test-dir build-ci --output-on-failure -L unit` | 仅当 precheck 失败无副作用且结果二值可判定时完成 |
| OTA-TODO-007 | Done | 实现 PackageVerifier 骨架 | OTA 设计 6.2/6.8/6.9 | 6.2 PackageVerifier；6.9 验签失败处理 | L3 | infra/src/ota/PackageVerifier.cpp | verify_package/verify_artifact | unit：签名失败/hash 失败/release_counter 回退拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-003、006 | 无（2026-04-01 已由 OTA-TODO-019 解阻） | 无 | 骨架实现、unit 测试、deliverable；2026-04-07 已落盘 infra/src/ota/PackageVerifier.h、infra/src/ota/PackageVerifier.cpp、tests/unit/infra/ota/PackageVerifierTest.cpp、docs/todos/infrastructure/deliverables/OTA-TODO-007-PackageVerifier骨架收敛.md，并更新 infra/tests CMake 接线；已通过 `cmake --build build-ci --target dasall_infra dasall_ota_package_verifier_unit_test`、`ctest --test-dir build-ci -N -R "OTAPackageVerifierTest"`、`ctest --test-dir build-ci --output-on-failure -R "OTAPackageVerifierTest"`、`cmake --build build-ci --target dasall_unit_tests` 与 `ctest --test-dir build-ci --output-on-failure -L unit` | 仅当三类失败路径均返回可观测错误且不进入安装时完成 |
| OTA-TODO-008 | Not Started | 实现 ArtifactCompatibilityEvaluator 骨架 | OTA 设计 6.2/6.8 | 6.2 ArtifactCompatibilityEvaluator | L3 | infra/src/ota/ArtifactCompatibilityEvaluator.cpp | evaluate(verified_manifest, capability, profile) | unit：hardware_selector/profile/dependency_refs 冲突拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-001、007 | 无 | 无 | 骨架实现、unit 测试 | 仅当兼容性失败可阻断安装且返回原因可追踪时完成 |
| OTA-TODO-009 | Not Started | 实现 InstallExecutor 骨架 | OTA 设计 6.2/6.8 | 6.2 InstallExecutor；6.8 安装失败处理 | L3 | infra/src/ota/InstallExecutor.cpp | stage_artifact/activate_plan/revert_install | unit：repo_bound 与 slot_bound 分支可区分；failure：写入失败可清理 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-004、008 | 无 | 无 | 骨架实现、unit 测试 | 仅当安装失败能触发清理路径且产出 InstallEvidence 时完成 |
| OTA-TODO-010 | Not Started | 实现 SlotSwitchCoordinator 骨架 | OTA 设计 6.2/6.8/6.9 | 6.2 SlotSwitchCoordinator | L3 | infra/src/ota/SlotSwitchCoordinator.cpp | build_slot_plan/select_inactive_slot/set_next_boot | unit：仅允许 inactive slot；failure：slot 不可用拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-005、009 | 无 | 无 | 骨架实现、unit 测试 | 仅当 inactive slot 约束可验证且切换前生成 rollback token 时完成 |
| OTA-TODO-011 | Blocked | 实现 BootConfirmationMonitor 骨架 | OTA 设计 6.2/6.8/12.1 | 6.2 BootConfirmationMonitor；12.1 问题 5 | L2 | infra/src/ota/BootConfirmationMonitor.cpp | await_confirm/handle_timeout/evaluate_self_check | unit：confirm 成功与超时失败；failure：未确认默认失败 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-005、010 | OTA-BLK-03 | 冻结 confirm 成功判据（health ready + 进程心跳/版本报告） | 阻塞记录或骨架实现与测试 | 仅当成功判据冻结后从 Blocked 转 Not Started |
| OTA-TODO-012 | Not Started | 实现 RollbackController 骨架 | OTA 设计 6.2/6.9 | 6.2 RollbackController；6.9 回滚失败处理 | L3 | infra/src/ota/RollbackController.cpp | rollback(token)/restore_boot_target/recover_repo_pointer | unit：rollback 成功/失败双路径；failure：rollback_fail 独立可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-009、010 | OTA-BLK-01 | 冻结 token 持久化位置与过期规则 | 骨架实现、unit 测试或阻塞记录 | 仅当 rollback 结果与证据可二值判定且失败路径可观测时完成 |
| OTA-TODO-013 | Not Started | 实现 OTAAuditBridge 骨架 | OTA 设计 6.2/6.11 | 6.2 OTAAuditBridge；6.11 审计字段 | L3 | infra/src/ota/OTAAuditBridge.cpp | write_precheck_audit/write_apply_audit/write_rollback_audit | unit：高风险动作强制审计；failure：审计写失败可见 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-001、012 | 无 | 无 | 骨架实现、unit 测试 | 仅当 ota.apply/ota.rollback 路径均产出审计且字段完整时完成 |
| OTA-TODO-014 | Not Started | 实现 OTAHealthProbe 骨架 | OTA 设计 6.2/6.11 | 6.2 OTAHealthProbe；6.11 健康信号 | L3 | infra/src/ota/OTAHealthProbe.cpp | probe(backlog,last_failure,pending_confirm) | unit：degraded 条件可判定；unit：pending_confirm 计数准确 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | OTA-TODO-001、011、012 | 无 | 无 | 骨架实现、unit 测试 | 仅当关键健康信号可查询且与失败路径一致时完成 |
| OTA-TODO-015 | Not Started | 接线 ota 到 infra CMake 构建入口 | OTA 设计 7/8.1；代码现状 | 8.1 文件落盘建议 | L2 | infra/CMakeLists.txt、infra/src/ota/ | ota 源文件纳入 dasall_infra | build：dasall_infra 可编译；test：后续 unit 目标可链接 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_infra | OTA-TODO-001~014（可分批） | 无 | 无 | CMake 改动、构建记录 | 仅当 placeholder 不再是唯一源码入口且 ota 文件入图时完成 |
| OTA-TODO-016 | Not Started | 注册 ota 的 unit 与 contract 测试入口 | OTA 设计 9.1/9.2；规范 3.7 | 9.1 测试矩阵；9.2 gate | L2 | tests/unit/CMakeLists.txt、tests/unit/infra/ota/、tests/contract/CMakeLists.txt | unit：precheck/verify/install/switch/rollback；contract：边界与错误映射 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests dasall_contract_tests && ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -L contract | OTA-TODO-015 | 无 | 无 | 测试注册改动、执行记录 | 仅当新增 ota 测试可被 ctest -N 发现并在 unit/contract 标签下执行时完成 |
| OTA-TODO-017 | Not Started | 注册 ota integration/failure 测试入口 | OTA 设计 9.1/9.2；代码现状 | 9.1 Integration/Failure 覆盖 | L0 | tests/CMakeLists.txt、tests/integration/infra/ota/、tests/stress/ | integration：apply->confirm->success；failure：verify_fail/confirm_timeout/rollback_fail | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | OTA-TODO-015、016 | 无（2026-03-30 已由 INF-BLK-06 integration 顶层拓扑校准解阻） | 无；待 OTA-TODO-015、016 完成后落盘具体 integration/failure 用例 | 测试注册改动或阻塞记录 | 仅当 tests 顶层接线完成并可发现 ota integration 后可解阻 |
| OTA-TODO-018 | Not Started | 补齐 rollback token 生命周期与持久化设计 | OTA 设计 12.1 问题 3/4；11.1 阻塞管理 | 12.1 未决问题；11.1 阻塞管理 | L0 | docs/architecture/DASALL_infra_OTA模块详细设计.md | RollbackToken 存储位置、过期策略、重启恢复规则 | process test：设计评审门；contract：对象边界不越权 | rg -n "RollbackToken|rollback token|expires_at|持久化" docs/architecture/DASALL_infra_OTA模块详细设计.md | 无 | 无 | 评审通过并回链到 OTA-TODO-012 | 设计补丁、评审记录、回链记录 | 仅当 token 生命周期表具备字段、状态、过期与恢复语义时完成 |
| OTA-TODO-019 | Done | 补齐签名算法与 trust anchor 接口设计 | OTA 设计 12.1 问题 4；6.10 配置键 | 12.1 未决问题；6.10 signature_algorithm | L0 | docs/architecture/DASALL_infra_OTA模块详细设计.md、docs/architecture/DASALL_infrastructure子系统详细设计.md | signature_algorithm 允许集、trust anchor 读取接口与错误映射 | process test：安全评审门；unit 入口：verifier 依赖接口稳定性 | rg -n "signature_algorithm|trust anchor|验签|PackageVerifier" docs/architecture/DASALL_infra_OTA模块详细设计.md docs/architecture/DASALL_infrastructure子系统详细设计.md | 无 | 无 | 安全评审通过并回链到 OTA-TODO-003、007 | 设计补丁、评审记录、回链记录；2026-04-01 已在 OTA 详细设计冻结 `ed25519` / `ecdsa-p256-sha256` 允许集、`ITrustAnchorProvider.load_active_anchor(...)` 只读接口与 `INF_E_OTA_VERIFY_FAIL` outward 映射，并在 infra 子系统设计补齐 SecretManager trust anchor 读取职责；已同步解阻 OTA-TODO-003、007 与 OTA-BLK-02 | 仅当算法选择与 anchor 接口边界冻结且不进入 contracts 时完成 |
| OTA-TODO-020 | Not Started | 补齐 boot confirm 成功判据设计 | OTA 设计 12.1 问题 5；6.9 confirm 语义 | 12.1 未决问题；6.9 启动确认失败 | L0 | docs/architecture/DASALL_infra_OTA模块详细设计.md | confirm 成功条件、超时处理、watchdog/health 联动条件 | process test：门禁评审；failure：超时默认失败规则可验证 | rg -n "confirm|启动确认|BootConfirmationMonitor|timeout" docs/architecture/DASALL_infra_OTA模块详细设计.md | 无 | 无 | 评审通过并回链到 OTA-TODO-011 | 设计补丁、评审记录、回链记录 | 仅当成功判据与失败兜底均可二值判定时完成 |
| OTA-TODO-021 | Not Started | 补齐 ota profile 键命名与覆盖优先级收敛 | OTA 设计 6.10；蓝图 5.1 | 6.10 配置表 | L2 | docs/architecture/DASALL_infra_OTA模块详细设计.md、profiles/*/ | infra.ota.* 键命名与默认/Profile/部署覆盖次序 | process test：profile 评审门；unit：配置键一致性校验入口 | rg -n "infra\.ota\.|Profile|覆盖层级" docs/architecture/DASALL_infra_OTA模块详细设计.md profiles/** | OTA-TODO-018~020（建议） | 无 | 配置评审通过并回链到 OTA-TODO-006/011 | 配置键名清单、评审记录、回链记录 | 仅当配置键与覆盖次序冻结并可被实现引用时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 边界冻结 | OTA-TODO-001~005 | 可并行 | 先稳定对象和接口边界 |
| B 核心链路骨架 | OTA-TODO-006~010、012 | 串行优先：006->007->008->009->010；012 可在 009/010 后推进 | 先 precheck/verify，再 install/switch，再 rollback |
| C 观测与健康出口 | OTA-TODO-013~014 | 可并行 | 审计和健康桥接可独立落地 |
| D 构建与测试接线 | OTA-TODO-015~016 | 串行建议：先 015 后 016 | 先入构建图，再测发现性 |
| E 受阻项解锁 | OTA-TODO-011、017 | 串行 | 先确认判据，再做 integration 注册 |
| F 补设计收敛 | OTA-TODO-018~021 | 可并行（建议先 018/019/020，再 021） | 作为阻塞解消输入，反哺实现任务 |

### 7.2 必过门禁表

| Gate ID | 门禁项 | 触发时机 | 通过标准 | 不通过后动作 |
|---|---|---|---|---|
| OTA-GATE-01 | 对象与接口冻结门 | 进入实现任务前 | OTA-TODO-001~005 完成且可编译 | 退回边界任务 |
| OTA-GATE-02 | 预检与验签门 | 执行安装相关任务前 | precheck/verify 失败可阻断且无副作用 | 修复 006/007 |
| OTA-GATE-03 | 回滚可观测门 | 提交前 | rollback_success/rollback_fail 均可观测并有证据 | 修复 012/013/014 |
| OTA-GATE-04 | 构建门 | 任务 015 后 | cmake --build build-ci --target dasall_infra 通过 | 修复 CMake 接线 |
| OTA-GATE-05 | unit/contract 发现性门 | 任务 016 后 | ctest -N 可发现 ota 新增测试并可按标签执行 | 修复 tests 注册 |
| OTA-GATE-06 | integration 准入门 | 任务 017 前 | tests 顶层 integration 接线与标签规范已冻结，且 ota 组件 integration/failure 用例已落盘 | 未通过前补齐 ota 组件 integration/failure 用例与注册 |
| OTA-GATE-07 | breaking 评审门 | 任意边界变更前 | 评审结论包含风险、迁移窗口、回退策略 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 |
|---|---|---|---|---|
| OTA-BLK-01 | rollback token 生命周期与持久化位置未冻结 | OTA-TODO-012 | 明确 token 存储位置、过期策略、重启恢复规则 | 完成 OTA-TODO-018 |
| OTA-BLK-02 | 已解阻（2026-04-01）：signature_algorithm 允许集与 trust anchor 读取接口已冻结，并约束 outward 映射为 INF_E_OTA_VERIFY_FAIL | OTA-TODO-003、007 | 无；后续按冻结边界实现 verifier 接口与骨架 | 证据回链到 OTA-TODO-019 与 docs/architecture/DASALL_infra_OTA模块详细设计.md、docs/architecture/DASALL_infrastructure子系统详细设计.md |
| OTA-BLK-03 | boot confirm 成功判据未冻结 | OTA-TODO-011 | 明确 success 判据与超时失败默认规则 | 完成 OTA-TODO-020 |
| OTA-BLK-04 | 已解阻（2026-03-30）：tests 顶层 integration 拓扑与聚合 gate 依赖已补齐；ota integration/failure 是否可执行改由组件自身落盘负责 | OTA-TODO-017 | 无；后续仅需按组件落盘 integration/failure 用例 | 证据回链到 infra 专项 TODO 的 INF-BLK-06 校准记录，以及 tests/CMakeLists.txt、tests/integration/CMakeLists.txt |
| OTA-BLK-05 | profile 键命名与覆盖优先级未最终收敛 | OTA-TODO-006、011（部分） | infra.ota.* 键命名冻结且跨 profile 一致 | 完成 OTA-TODO-021 |

## 9. 验收与质量门

### 9.1 验收命令基线

| 用途 | 命令 |
|---|---|
| 配置构建目录 | cmake -S . -B build-ci -G Ninja |
| 构建 infra | cmake --build build-ci --target dasall_infra |
| 构建 unit/contract 聚合目标 | cmake --build build-ci --target dasall_unit_tests dasall_contract_tests |
| 检查测试发现性 | ctest --test-dir build-ci -N |
| 执行 unit 标签 | ctest --test-dir build-ci --output-on-failure -L unit |
| 执行 contract 标签 | ctest --test-dir build-ci --output-on-failure -L contract |

说明：

1. integration/failure 当前不列为必过基线，原因是 OTA-TODO-017 尚未落盘具体 integration/failure 用例；顶层 integration 拓扑已于 2026-03-30 解阻。
2. 所有 Build-ready 任务均绑定至少 1 条构建命令与 1 条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而不是只列任务标题：是。  
2. 是否明确当前最细可达到粒度等级：是（L3/L2，受阻项 L0）。  
3. 是否所有任务都满足代码目标 + 测试目标 + 验收命令：是。  
4. 是否所有 Blocked 项都带有明确证据和解阻条件：是。  
5. 是否所有任务都具备可二值判定完成标准：是。  
6. 是否避免跨子系统范围扩张：是。  
7. 若要求函数/数据结构级，是否真正落到对象：是。  

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 回退策略 |
|---|---|---|---|---|
| 边界越权风险 | High | ota 直接介入 runtime 恢复裁定或调度 | 出现 replan/abort_safe 裁定逻辑 | 回退到只输出 UpgradeOutcome 与 RecoveryHint 事实 |
| 验签旁路风险 | High | verify 失败仍进入 install | verify_fail 后仍写入目标位点 | 强制在 coordinator 中短路并清理 staging |
| 回滚不可恢复风险 | High | token 生命周期未定义或过期规则不清晰 | rollback_fail 增高且 evidence 缺失 | 冻结 apply 通道，仅保留 precheck/query_status |
| confirm 误判风险 | High | 成功判据过宽，超时默认放行 | confirm_timeout 未触发失败 | 未冻结判据前维持 OTA-TODO-011 Blocked |
| 测试发现性缺失 | Medium | 新增用例未被 ctest 发现 | ctest -N 不显示 ota 测试 | 回退到 tests 注册改动，禁止宣告 gate 完成 |
| profile 漂移风险 | Medium | 不同 profile 键名和默认值不一致 | 同一计划在 profile 行为不一致 | 回退到默认策略，关闭运行时覆盖 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合），但 integration 与部分实现任务需等待阻塞解消。  
2. 原因：
- OTA 详细设计已给出接口清单、对象字段、主异常流程、错误语义与落盘建议。
- 设计中已包含 Design->Build 映射、测试矩阵和门禁建议。
- 当前代码虽为空实现，但构建目标与 unit/contract 聚合目标已存在，可承接增量落地。
- 仍存在 12.1 未决项与 tests integration 顶层接线缺口，必须前置补齐。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构（受阻项为组件级补设计）。  
4. 未达到全量函数级的缺口：token 持久化、签名算法与 anchor 接口、confirm 成功判据、integration 顶层接线。  
5. 下一步建议：
- 先执行 OTA-TODO-001~016，完成对象/接口/核心骨架与 unit/contract 门禁；
- 并行完成 OTA-TODO-018~021 消除设计阻塞；
- 解阻后再推进 OTA-TODO-011 与 OTA-TODO-017，进入完整集成验证阶段。
