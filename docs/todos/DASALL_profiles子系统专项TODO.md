# DASALL profiles 子系统专项 TODO

最近更新时间：2026-03-25  
阶段：Detailed Design -> Special TODO  
适用范围：profiles/

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/DASALL_profiles模块详细设计.md
2. docs/architecture/DASALL_infrastructure子系统详细设计.md
3. docs/architecture/DASSALL_Agent_architecture.md
4. docs/architecture/DASALL_Engineering_Blueprint.md
5. docs/adr/ADR-005-architecture-review-baseline.md
6. docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md
7. docs/adr/ADR-007-reflection-engine-vs-recovery-manager.md
8. docs/adr/ADR-008-agent-orchestrator-vs-multi-agent-coordinator.md
9. docs/plans/DASALL_工程落地实现步骤指引.md
10. docs/development/DASALL_工程协作与编码规范.md
11. docs/todos/contracts-freeze/
12. docs/todos/DASALL_infrastructure子系统专项TODO.md
13. docs/todos/DASALL_infrastructure_logging组件专项TODO.md
14. docs/todos/DASALL_infrastructure_tracing组件专项TODO.md
15. docs/todos/DASALL_infrastructure_metrics组件专项TODO.md
16. docs/todos/DASALL_infrastructure_config组件专项TODO.md
17. docs/todos/DASALL_infrastructure_secret组件专项TODO.md
18. docs/todos/DASALL_infrastructure_health组件专项TODO.md
19. docs/todos/DASALL_infrastructure_watchdog组件专项TODO.md
20. docs/todos/DASALL_infrastructure_ota组件专项TODO.md
21. docs/todos/DASALL_platform_linux组件专项TODO.md
22. 当前代码与构建现状：CMakeLists.txt、profiles/*、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、build-ci/

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 profiles 子系统边界扩张到无关模块。
3. 讨论类事项不作为 Done-ready Build 任务。
4. 每项任务必须包含代码目标、测试目标、验收命令。
5. 设计证据不足处先列 Blocked 与补设计前置项，不伪造实现任务。

## 2. 子系统目标与范围

### 2.1 子系统目标

1. 将 profiles 从“资产占位”落地为“Build 裁剪 + Runtime 治理”双平面机制。
2. 提供统一 ProfileCatalog、BuildProfileResolver、RuntimePolicyProvider、Overlay/Validator/LKG 能力，防止 CMake 与 YAML 双源漂移。
3. 维持同一主流程跨档位复用，通过配置、注册表、依赖注入实现差异，不在主流程散落平台分支。
4. 为 runtime、llm、tools、memory、knowledge、infra 提供可验证且可审计的策略快照输入。

### 2.2 范围边界

纳入范围：

1. profiles 资产结构、接口、核心对象、校验流程、回退流程。
2. Build 与 Runtime 双平面的接线点、CMake 注册点、测试注册点。
3. 失败拒绝、LKG 回退、可观测事件的门禁化验证。

不纳入范围：

1. runtime 主状态机裁定与恢复执行逻辑。
2. llm prompt 具体渲染细节与 memory 语义检索细节。
3. contracts 共享对象字段扩写。
4. 各业务子系统内部算法实现。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的直接影响 |
|---|---|---|---|---|
| PRF-TC001 | profiles 详细设计 2.1 | Must | profiles 必须同时覆盖 Build 裁剪与 Runtime 治理两层 | 任务必须同时覆盖 build 接线与 runtime 策略快照 |
| PRF-TC002 | profiles 详细设计 2.1/6.9 | Must | 每个 profile 至少冻结 target_platform、enabled_modules、runtime_budget、model_route、execution_policy、ops_policy 等策略域 | 必须有配置模型与 schema 任务 |
| PRF-TC003 | 架构 7.5；蓝图 3.13 | Must | Profile 差异通过配置、注册表、依赖注入落地，不得在主流程散落平台分支 | 必须有 Validator 与模块矩阵一致性任务 |
| PRF-TC004 | profiles 详细设计 2.1；ADR-006 | Must-Not | profiles 不接管 ContextPacket 组装与 Prompt 渲染 | 不生成 context/prompt 装配实现任务 |
| PRF-TC005 | profiles 详细设计 2.1；ADR-007 | Must-Not | profiles 不持有失败语义解释权或恢复执行权 | 错误处理任务只做到策略阈值与拒绝语义 |
| PRF-TC006 | profiles 详细设计 2.1；ADR-008 | Must-Not | profiles 不产生第二调度中心，仅启停 multi_agent 能力与预算 | 不生成 orchestrator/coordinator 调度任务 |
| PRF-TC007 | 蓝图 4.2/4.3 | Must | 跨模块调用通过 contracts 接口，禁止跨模块实现依赖闭环 | 代码目标限定在 profiles/tests/cmake/docs |
| PRF-TC008 | ADR-005 | Must | contracts 与关键边界冻结前，不能以 profiles 反向改写系统边界 | 设计缺口必须 Blocked，不伪造实现 |
| PRF-TC009 | profiles 详细设计 6.5/6.6 | Must-Not | Build 开关、YAML 结构、adapter 选择属于 profiles 私有，不进入 contracts | 任务中不得新增 contracts 共享对象 |
| PRF-TC010 | profiles 详细设计 2.1；contracts freeze 约束 | Must | 兼容优先，schema 演进优先新增字段 | breaking 风险任务必须评审门禁 |
| PRF-TC011 | profiles 详细设计 6.9；infra 详细设计 2/6 | Must | profiles 必须作为 infra ConfigCenter 的 Profile 层输入，不得旁路配置体系 | 必须拆出 OverlayComposer 与对齐校验任务 |
| PRF-TC012 | 工程规范 3.6 | Must | 激活、校验、回退失败必须可观测，不可吞错 | 必须拆出 Telemetry 与失败注入测试任务 |
| PRF-TC013 | 工程规范 3.7 | Should | 新增公共接口应同步增加 unit/contract/integration 测试 | 每任务必须含测试目标与命令 |
| PRF-TC014 | 工程落地计划 阶段 M | Must | profiles 为后置收敛阶段，但当前可先完成可复用骨架 | 任务需分 Build-ready 与 Blocked 两层 |
| PRF-TC015 | 代码现状（CMakeLists.txt、profiles/*） | Must | 目前仅有 profile.cmake + runtime_policy.yaml 占位，且根 CMake 未纳入 profiles 代码模块 | 必须先补接线与发现性，再推进实现 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| CMakeLists.txt | 未 add_subdirectory(profiles) | profiles 代码模块尚未纳入构建图 |
| profiles/*/profile.cmake | 仅 set(DASALL_PROFILE_NAME "<id>") | Build 侧仅有档位名占位 |
| profiles/*/runtime_policy.yaml | 仅 profile/enabled_modules/runtime_budget 三个占位字段 | Runtime 策略域严重缺失 |
| profiles/ | 无 CMakeLists.txt，无 include/src | ProfileCatalog/Resolver/Provider 未落盘 |
| tests/CMakeLists.txt | 仅 mocks/unit/contract，无 integration 顶层接线 | profiles integration 门禁不可执行 |
| tests/unit/CMakeLists.txt | 无 profiles 子目录 | profiles 单测注册点缺失 |
| build-ci/ | 已有可复用构建目录与 ctest 入口 | 可作为统一验收命令基线 |

## 4. 粒度可行性评估

### 4.1 总体结论

结论：可直接生成 L3/L2 混合专项 TODO，但需显式保留 L0 Blocked 前置任务。  
当前最小可执行粒度：函数/接口/数据结构级（以接口与对象为主，少量流程任务为 L2）。

证据：

1. 已有核心接口与方法语义：IProfileCatalog、IBuildProfileResolver、IRuntimePolicyProvider、IProfileCompatibilityValidator、ILastKnownGoodStore。
2. 已有核心对象字段：ProfileDescriptor、BuildProfileManifest、RuntimePolicySnapshot、ValidationReport、ProfileActivationRecord。
3. 已有主流程与异常流程：Build 解析、Runtime 激活、运行时覆盖刷新、拒绝与回退原则。
4. 已有错误码域：PRF_E_PROFILE_NOT_FOUND 等 8 个私有错误码。
5. 已有文件落盘建议、测试矩阵、质量门与阻塞表。
6. 当前阻塞项集中在工程接线与跨子系统输入冻结，而非接口语义缺失。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| ProfileDescriptor | 6.5 | L3 | 字段、唯一性、schema 约束明确 | support_level 枚举取值表未落盘 | 直接拆数据结构任务 |
| BuildProfileManifest | 6.5/6.7 | L3 | 模块/适配器/标签语义明确 | 与 tests 标签绑定规则需补充 | 直接拆数据结构任务 |
| RuntimePolicySnapshot | 6.5/6.7/6.9 | L3 | 核心策略域与不可变语义明确 | generation 与原子替换并发语义细节待补 | 直接拆数据结构任务 |
| ValidationReport | 6.5/6.6 | L3 | blocking/warning 分层明确 | dependency_gaps 编码规范待补 | 直接拆数据结构任务 |
| ProfileActivationRecord | 6.5/6.8/6.10 | L3 | 激活/回退审计字段明确 | source 枚举与 activation_mode 枚举待补 | 直接拆数据结构任务 |
| IProfileCatalog | 6.6 | L3 | list_profiles/get_profile 语义与错误语义明确 | 目录扫描失败分类细节待补 | 直接拆接口任务 |
| IBuildProfileResolver | 6.6/6.7 | L3 | resolve_build_manifest 语义明确 | request 对象字段未独立定义 | 先定义 request 再实现 |
| IRuntimePolicyProvider | 6.6/6.7/6.8 | L3 | load_snapshot/activate_snapshot 语义明确 | overlay 来源对象未冻结 | 先接口冻结后实现 |
| IProfileCompatibilityValidator | 6.6 | L3 | validate 输入输出语义明确 | environment 事实对象未冻结 | 先接口冻结后实现 |
| ILastKnownGoodStore | 6.6/6.8 | L3 | save/load 语义与门禁明确 | 存储介质策略未定 | 先接口 + file/memory 最小实现 |
| ProfileOverlayComposer | 6.2/6.3/6.7/6.9 | L2 | 覆盖顺序明确 | infra/config 覆盖输入接口未冻结 | 先补前置设计 |
| ProfileTelemetryAdapter | 6.2/6.10 | L2 | 日志/指标/trace/audit 事件清单明确 | infra observability 适配接口签名未冻结 | 先接口 + stub 事件 |
| runtime_policy.yaml 五档位资产 | 3.1/6.9/8.3 | L2 | 资产路径与最小占位结构存在 | schema_version 与完整策略域缺失 | 直接拆资产冻结任务 |
| profiles 模块 CMake 接线 | 8.1 + 代码现状 | L0 | 设计给出路径建议 | 仓库无 profiles/CMakeLists 且根 CMake 未纳入 | 先补接线，再推进实现 |
| tests unit/integration 注册点 | 8.1/9.1 + 代码现状 | L0 | 测试矩阵明确 | tests 顶层无 integration，unit 无 profiles | 先补测试拓扑，再推进集成验证 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 建立统一 ProfileCatalog | 6.2/6.6/7 | 接口定义 | PRF-TODO-001、006 | 先冻结目录发现接口，再落最小实现 |
| Build 与 Runtime 共享模块矩阵 | 6.3/6.6/6.7/7 | 数据结构 + 流程 | PRF-TODO-002、007、011 | 先冻结 manifest，再实现 resolver 和一致性校验 |
| Runtime 不可变策略快照 | 6.5/6.6/6.7 | 数据结构 + 生命周期 | PRF-TODO-003、008 | 先冻结 snapshot，再落 provider |
| 覆盖合并与拒绝策略 | 6.3/6.7/6.9 | 适配器/桥接 | PRF-TODO-009、PRF-BLK-02 | 依赖 infra/config 覆盖输入冻结 |
| 兼容校验与错误域 | 6.6/6.8/9 | 异常与错误处理 | PRF-TODO-004、010、011 | 先错误域与校验报告，再做拒绝门禁测试 |
| LKG 回退机制 | 6.2/6.6/6.8 | 生命周期与初始化 | PRF-TODO-005、012 | 先接口与最小存储，再做回退失败注入 |
| 五档位策略资产冻结 | 3.1/6.9/8.3 | 配置与裁剪 | PRF-TODO-013 | 直接补齐 runtime_policy.yaml 策略域 |
| Build/测试入口收敛 | 7/8.1/9 | CMake/注册点 | PRF-TODO-014、015、016 | 先 CMake 纳入 profiles，再补 unit/integration 注册 |
| 激活/拒绝/回退可观测 | 6.10/9 | 测试与门禁 | PRF-TODO-017 | 与 infra 接口对齐，先做可观测事件最小闭环 |
| 交付证据回写与 Gate 收敛 | 9.2/11.1 | 文档/交付证据 | PRF-TODO-018 | 收敛执行证据、阻塞变化与回退记录 |
| 未决问题补设计 | 11.1/12.1 | 补设计前置 | PRF-TODO-019~021 | 把缺口显式前置，禁止伪造实现任务 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | PRF-TODO-001、005、006、007、008、010 |
| 数据结构定义类任务 | 是 | PRF-TODO-002、003、004 |
| 生命周期与初始化类任务 | 是 | PRF-TODO-012 |
| 适配器/桥接类任务 | 是 | PRF-TODO-009、017 |
| 异常与错误处理类任务 | 是 | PRF-TODO-004、011、017 |
| 配置与 Profile 裁剪类任务 | 是 | PRF-TODO-013、019、021 |
| 测试与门禁类任务 | 是 | PRF-TODO-014、015、016、017 |
| 文档/交付证据回写类任务 | 是 | PRF-TODO-018 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PRF-TODO-001 | Done | 定义 IProfileCatalog 接口头文件 | 详细设计 6.6；工程规范 3.2 | 6.6 IProfileCatalog | L3 | profiles/include/dasall/profiles/IProfileCatalog.h | list_profiles(), get_profile(profile_id) | unit：接口编译与最小 mock 调用 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_profile_catalog_interface_unit_test && ctest --test-dir build-ci -R ProfileCatalogInterfaceTest --output-on-failure | 无 | 无 | 无 | IProfileCatalog.h、ProfileDescriptor.h、ProfileCatalogInterfaceTest.cpp、编译与单测记录；2026-03-27 已通过 | 仅当接口签名、错误语义注释与最小 mock 调用均通过编译和单测时完成 |
| PRF-TODO-002 | Done | 定义 BuildProfileManifest 对象头文件 | 详细设计 6.5；蓝图 5.1 | 6.5 BuildProfileManifest | L3 | profiles/include/dasall/profiles/BuildProfileManifest.h | enabled_modules, enabled_adapters, observability_level, build_tags, toolchain_hint | unit：字段完整性断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_build_profile_manifest_unit_test && ctest --test-dir build-ci -R BuildProfileManifestTest --output-on-failure | PRF-TODO-001 | 无 | 无 | BuildProfileManifest.h、BuildProfileManifestTest.cpp、编译与单测记录；2026-03-27 已通过 | 仅当字段集合、唯一性约束与工具链提示语义可二值断言且构建通过时完成 |
| PRF-TODO-003 | Done | 定义 RuntimePolicySnapshot 对象头文件 | 详细设计 6.5/6.9 | 6.5 RuntimePolicySnapshot | L3 | profiles/include/dasall/profiles/RuntimePolicySnapshot.h | generation, effective_profile_id, runtime_budget, model_profile, token_budget_policy, prompt_policy, capability_cache_policy, degrade_policy, timeout_policy, execution_policy, ops_policy | unit：不可变语义断言（只读访问） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_runtime_policy_snapshot_unit_test && ctest --test-dir build-ci -R RuntimePolicySnapshotTest --output-on-failure | PRF-TODO-001 | 无 | 无 | RuntimePolicySnapshot.h、RuntimePolicySnapshotTest.cpp、编译与单测记录；2026-03-27 已通过 | 仅当快照字段完整、getter 只读语义稳定且不可变约束可自动化断言时完成 |
| PRF-TODO-004 | Done | 定义 ValidationReport 对象与私有错误码头文件 | 详细设计 6.5/6.6；工程规范 3.6 | 6.5 ValidationReport；6.6 错误码清单 | L3 | profiles/include/dasall/profiles/ValidationReport.h；profiles/include/dasall/profiles/ProfileError.h | blocking_errors, warnings, dependency_gaps, compatibility_state；PRF_E_* | unit：错误码唯一性与分类断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_validation_report_unit_test && ctest --test-dir build-ci -R ValidationReportTest --output-on-failure | PRF-TODO-001 | 无 | 无 | ValidationReport.h、ProfileError.h、ValidationReportTest.cpp、编译与单测记录；2026-03-27 已通过 | 仅当 8 个错误码唯一、分类稳定且报告结构可区分 blocking/warning/blocked 状态时完成 |
| PRF-TODO-005 | Done | 定义 ILastKnownGoodStore 接口头文件 | 详细设计 6.6/6.8 | 6.6 ILastKnownGoodStore | L3 | profiles/include/dasall/profiles/ILastKnownGoodStore.h | save(snapshot_ref), load(profile_id) | unit：接口编译与读写占位测试 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_last_known_good_store_unit_test && ctest --test-dir build-ci -R LastKnownGoodStoreInterfaceTest --output-on-failure | PRF-TODO-003 | 无 | 无 | ILastKnownGoodStore.h、LastKnownGoodStoreInterfaceTest.cpp、编译与单测记录；2026-03-27 已通过 | 仅当接口语义、只读快照引用约束与最小读写占位路径均通过编译和单测时完成 |
| PRF-TODO-006 | Not Started | 实现 ProfileCatalog 最小资产发现 | 详细设计 6.2/6.3/6.6/8.1 | 6.2 ProfileCatalog；8.1 路径建议 | L3 | profiles/src/ProfileCatalog.cpp | list_profiles(), get_profile(profile_id) | unit：5 档位发现与 profile_id 唯一性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | PRF-TODO-001、014 | 无 | 无 | 实现代码、单测、发现性记录 | 仅当可稳定发现 5 个现有 profile 资产且错误码符合设计时完成 |
| PRF-TODO-007 | Not Started | 实现 BuildProfileResolver 最小解析流程 | 详细设计 6.2/6.6/6.7 | 6.2 BuildProfileResolver；6.7 主流程 A | L3 | profiles/src/BuildProfileResolver.cpp | resolve_build_manifest(request) | unit：合法/非法 profile 请求解析 | cmake -S . -B build-ci -G Ninja -DDASALL_PROFILE=desktop_full && cmake --build build-ci | PRF-TODO-002、006、014 | PRF-BLK-03 | 模块标识命名表冻结 | 实现代码、单测、构建记录 | 仅当 resolver 可输出稳定 manifest 且非法组合可拒绝时完成 |
| PRF-TODO-008 | Not Started | 实现 RuntimePolicyProvider 最小加载流程 | 详细设计 6.2/6.6/6.7/6.9 | 6.2 RuntimePolicyProvider；6.7 主流程 B | L3 | profiles/src/RuntimePolicyProvider.cpp | load_snapshot(request), activate_snapshot(request) | unit：基线加载成功/格式错误拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | PRF-TODO-003、013、014 | PRF-BLK-04 | YAML/schema 校验策略冻结 | 实现代码、单测、失败用例记录 | 仅当基线快照加载与显式拒绝语义可二值判定时完成 |
| PRF-TODO-009 | Blocked | 实现 ProfileOverlayComposer 合并流程 | 详细设计 6.2/6.3/6.7/6.9；infra 详细设计 | 6.2 ProfileOverlayComposer；6.7 覆盖顺序 | L2 | profiles/src/ProfileOverlayComposer.cpp | compose(base, deployment_override, runtime_override) | unit：四层覆盖顺序断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci | PRF-TODO-008 | PRF-BLK-02 | infra/config 覆盖输入接口冻结 | 代码草案或阻塞记录 | 仅当覆盖输入契约冻结后才可转 Not Started |
| PRF-TODO-010 | Not Started | 定义 IProfileCompatibilityValidator 接口并落最小实现 | 详细设计 6.2/6.6/6.7 | 6.6 IProfileCompatibilityValidator | L3 | profiles/include/dasall/profiles/ProfileCompatibilityValidator.h；profiles/src/ProfileCompatibilityValidator.cpp | validate(candidate, build_manifest, environment) | unit：blocking/warning 分层断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci | PRF-TODO-004、007、008、014 | PRF-BLK-03 | environment 输入对象冻结 | 头文件、实现、单测 | 仅当校验结果可区分 blocking/warning 且拒绝路径可判定时完成 |
| PRF-TODO-011 | Not Started | 校验 BuildManifest 与 RuntimeSnapshot 模块矩阵一致性 | 详细设计 6.7/9.1/9.6 | 6.7 主流程 A/B；9.1 测试矩阵 | L2 | tests/unit/profiles/ | 一致性校验逻辑 | unit：矩阵一致/冲突拒绝 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci --output-on-failure -L unit | PRF-TODO-007、008、010、015 | 无 | 无 | 单测与执行记录 | 仅当一致性断言与冲突拒绝均可自动化验证时完成 |
| PRF-TODO-012 | Not Started | 实现 LastKnownGoodStore 最小读写与激活回退路径 | 详细设计 6.2/6.6/6.8 | 6.2 LastKnownGoodStore；6.8 恢复路径 | L3 | profiles/src/LastKnownGoodStore.cpp；profiles/src/RuntimePolicyProvider.cpp | save(), load(), fallback_lkg 路径 | unit：回退成功/不可用两条路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | PRF-TODO-005、008、014 | PRF-BLK-05 | LKG 存储介质策略冻结 | 实现代码、失败注入测试、执行记录 | 仅当回退路径可复现且失败不静默时完成 |
| PRF-TODO-013 | Not Started | 补齐 5 档 runtime_policy.yaml 最小 schema 与策略域 | 详细设计 3.1/6.9/8.3 | 6.9 配置逻辑域；8.3 PRF-T002 | L2 | profiles/*/runtime_policy.yaml | profile_meta/enabled_modules/runtime_budget/model_profile/token_budget_policy/prompt_policy/capability_cache_policy/degrade_policy/timeout_policy/execution_policy/ops_policy | contract：schema 结构稳定性校验 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci --output-on-failure -L contract | 无 | PRF-BLK-04 | schema_version 与字段必填规则冻结 | 5 个 YAML、契约测试、校验记录 | 仅当 5 档均通过 schema 检查且字段集合一致时完成 |
| PRF-TODO-014 | Done | 新增 profiles 模块 CMake 接线 | 详细设计 8.1；代码现状 | 8.1 路径建议 | L0 | CMakeLists.txt（根）；profiles/CMakeLists.txt（新增）；profiles/src/placeholder.cpp（新增） | add_subdirectory(profiles) 与 profiles 静态库目标注册 | build：profiles 目标可被构建图识别 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci；cmake --build build-ci --target dasall_profiles | 无 | 无 | 无 | CMake 改动、构建记录；2026-03-27 已执行 configure/build 成功，dasall_profiles 目标可单独构建 | 仅当 profiles 模块进入构建图并可独立编译时完成 |
| PRF-TODO-015 | Done | 注册 profiles unit 测试目录与目标 | 详细设计 8.1/9.1；工程规范 3.7 | 9.1 单元测试范围 | L2 | tests/unit/CMakeLists.txt；tests/unit/profiles/ | unit 发现性注册 | unit：ProfileCatalog/Resolver/Provider/Validator/LKG | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N | PRF-TODO-014 | 无 | 无 | tests/unit/CMakeLists.txt、tests/unit/profiles/CMakeLists.txt、ctest -N 发现记录；2026-03-27 已发现 ProfileCatalogInterfaceTest | 仅当 ctest -N 可发现至少 1 个 profiles unit 测试并纳入 build-ci 时完成 |
| PRF-TODO-016 | Blocked | 注册 profiles integration 测试目录与目标 | 详细设计 8.1/9.1/9.4；代码现状 | 9.4 集成路径 | L0 | tests/CMakeLists.txt；tests/integration/profiles/ | integration 发现性注册 | integration：Build->Runtime 一致性冒烟 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | PRF-TODO-014 | PRF-BLK-06 | tests 顶层 integration 接线冻结 | CMake 变更或阻塞记录 | 仅当 integration 测试可被发现后解阻 |
| PRF-TODO-017 | Blocked | 实现 ProfileTelemetryAdapter 最小观测链路 | 详细设计 6.2/6.10/9.6；工程规范 3.6 | 6.10 可观测性；9.6 质量门 | L2 | profiles/src/ProfileTelemetryAdapter.cpp | activation/rejected/fallback 事件输出 | integration：激活/拒绝/回退事件可观测 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N | PRF-TODO-014、016 | PRF-BLK-07 | infra observability 接口签名冻结 | 实现代码或阻塞记录 | 仅当三类事件都有可验证输出且不吞错时完成 |
| PRF-TODO-018 | Not Started | 回写 profiles 专项 Gate 与交付证据 | 详细设计 9.6/11.1；本专项 TODO | 9.6 Gate 清单；11.1 阻塞管理 | L2 | docs/todos/DASALL_profiles子系统专项TODO.md | process test：门禁与阻塞状态一致性 | cmake -S . -B build-ci -G Ninja && ctest --test-dir build-ci -N | PRF-TODO-014~017 | 无 | 无 | 更新后的 TODO 证据段 | 仅当每个 Gate 均有通过/失败结论和命令证据时完成 |
| PRF-TODO-019 | Not Started | 补齐模块标识与适配器命名冻结表 | 详细设计 8.3 阻塞项；蓝图 5.1 | 11.1 模块标识未统一 | L0 | docs/architecture/DASALL_profiles模块详细设计.md | enabled_modules 与 enabled_adapters 命名表 | process test：评审门 | rg -n "enabled_modules|adapter|模块矩阵" docs/architecture/DASALL_profiles模块详细设计.md docs/architecture/DASALL_Engineering_Blueprint.md | 无 | 无 | 无 | 设计补丁、评审记录、回链记录 | 仅当命名表冻结并回链 PRF-TODO-007/010 时完成 |
| PRF-TODO-020 | Not Started | 补齐 overlay 输入契约与来源合法性设计 | 详细设计 6.3/6.7/11.1；infra 详细设计 | 11.1 override 接口未冻结 | L0 | docs/architecture/DASALL_profiles模块详细设计.md；docs/architecture/DASALL_infrastructure子系统详细设计.md | deployment_override/runtime_override 输入对象与合法性规则 | process test：跨文档一致性评审 | rg -n "override|Overlay|ConfigCenter|四层" docs/architecture/DASALL_profiles模块详细设计.md docs/architecture/DASALL_infrastructure子系统详细设计.md | 无 | 无 | 无 | 设计补丁、评审记录、回链记录 | 仅当 overlay 输入契约冻结并回链 PRF-TODO-009 时完成 |
| PRF-TODO-021 | Not Started | 补齐 LKG 存储介质与失效语义设计 | 详细设计 6.2/6.8/11.1 | 11.1 LKG 存储介质未定 | L0 | docs/architecture/DASALL_profiles模块详细设计.md | file/sqlite/in-memory 选型与失效策略 | process test：评审门 | rg -n "LastKnownGood|LKG|fallback" docs/architecture/DASALL_profiles模块详细设计.md | 无 | 无 | 无 | 设计补丁、评审记录、回链记录 | 仅当介质策略冻结并回链 PRF-TODO-012 时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 构建接线 | PRF-TODO-014 | 串行起步 | 未接线前其它代码任务不可执行 |
| B 接口与对象冻结 | PRF-TODO-001~005 | 可并行 | 接口与对象弱依赖，可并行收敛 |
| C 资产冻结 | PRF-TODO-013 | 串行（与 B 并行可行） | RuntimeProvider 依赖 schema 资产 |
| D 主链实现 | PRF-TODO-006~008、010、012 | 分段并行 | Catalog/Resolver/Provider/Validator/LKG 可按依赖推进 |
| E 测试门禁接线 | PRF-TODO-015、016 | 串行（016 当前 Blocked） | 先 unit 注册，再 integration 接线 |
| F 一致性与观测 | PRF-TODO-011、017 | 串行（017 当前 Blocked） | 一致性测试可先做，观测依赖 infra 接口冻结 |
| G 证据收口 | PRF-TODO-018 | 串行 | 收敛 Gate 证据与阻塞状态 |
| H 补设计解阻 | PRF-TODO-019~021 | 可并行 | 反哺 D/E/F 阶段阻塞项 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 未通过处理 |
|---|---|---|---|---|
| PRF-GATE-01 | CMake 接线门 | 所有代码任务前 | 根 CMake 纳入 profiles，profiles 目标可构建 | 停止后续实现，先修接线 |
| PRF-GATE-02 | 接口冻结门 | 进入实现前 | IProfileCatalog/IRuntimePolicyProvider/Validator/LKG 接口冻结 | 退回接口定义 |
| PRF-GATE-03 | 资产 schema 门 | RuntimeProvider 开发前 | 5 档 runtime_policy.yaml 字段齐全并通过 schema 校验 | 退回 PRF-TODO-013 |
| PRF-GATE-04 | 矩阵一致性门 | Resolver/Provider 联调前 | BuildManifest 与 RuntimeSnapshot 一致性测试通过 | 阻断合入 |
| PRF-GATE-05 | 测试发现性门 | 进入验收前 | ctest -N 可发现 profiles unit/integration 用例 | 修复 tests CMake 接线 |
| PRF-GATE-06 | 失败可观测门 | 激活/回退功能验收前 | 激活失败、override 拒绝、LKG 回退均有观测记录 | 阻断合入 |
| PRF-GATE-07 | ADR/边界门 | 任意跨边界改动前 | 不侵入 Context/Recovery/Orchestrator 边界 | 回退越界改动 |
| PRF-GATE-08 | breaking 评审门 | 公共接口签名或 schema 语义变更前 | 有评审记录、迁移窗口、回退方案 | 未评审不得推进 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| PRF-BLK-01 | 已解除：profiles 模块已纳入构建图（2026-03-27 完成 profiles/CMakeLists、新增根 CMake 接线与占位目标） | PRF-TODO-001~012 | 完成 PRF-TODO-014 | 新增 profiles/CMakeLists 并 add_subdirectory(profiles) | 保留最小空实现目标，不推进业务逻辑 |
| PRF-BLK-02 | Overlay 输入契约未与 infra/config 冻结对齐 | PRF-TODO-009 | 补齐跨文档覆盖输入契约 | 完成 PRF-TODO-020 | 初版仅支持 Profile 基线，不开放 runtime override |
| PRF-BLK-03 | enabled_modules/adapter 命名表未冻结 | PRF-TODO-007、010 | 命名表评审通过 | 完成 PRF-TODO-019 | validator 先校验冻结子集 |
| PRF-BLK-04 | runtime_policy schema_version 与字段必填规则未冻结 | PRF-TODO-008、013 | schema 规则冻结并评审通过 | 在详细设计补齐 schema 约束段 | 暂以最小字段运行，不宣称全域策略可用 |
| PRF-BLK-05 | LKG 存储介质策略未定 | PRF-TODO-012 | 介质策略与失效语义冻结 | 完成 PRF-TODO-021 | 先用进程内临时 LKG，不宣称持久化能力 |
| PRF-BLK-06 | tests 顶层未纳入 integration 子目录 | PRF-TODO-016、017 | tests/CMakeLists 集成接线完成 | 先跑 unit/contract 门禁，integration 保持 Blocked | 集成验证不作为当前必过发布门 |
| PRF-BLK-07 | ProfileTelemetryAdapter 对接 infra observability 接口签名未冻结 | PRF-TODO-017 | 与 infra logging/metrics/tracing/audit 接口对齐冻结 | 先输出标准日志事件占位并保留适配层 | 若接口未冻，观测任务保持 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

1. 构建基线：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci
2. 测试发现性基线：
   - ctest --test-dir build-ci -N
3. 单测基线：
   - ctest --test-dir build-ci --output-on-failure -L unit
4. 契约基线：
   - ctest --test-dir build-ci --output-on-failure -L contract
5. 集成基线（解阻后）：
   - ctest --test-dir build-ci --output-on-failure -L integration

说明：

1. 在 PRF-BLK-06 解阻前，integration 不作为必过基线。
2. 所有 Build-ready 任务都绑定至少 1 条构建命令与 1 条测试命令或发现性命令。

### 9.2 质量门逐项回答

1. 该专项 TODO 是否给出 Design -> TODO 映射，而不是只列任务标题：是。  
2. 该专项 TODO 是否明确当前最细可达到粒度等级：是（L3/L2，阻塞项 L0）。  
3. 是否所有任务都满足代码目标 + 测试目标 + 验收命令三件套：是。  
4. 是否所有 Blocked 项都带有明确证据和解阻条件：是。  
5. 是否所有任务都具备可二值判定完成标准：是。  
6. 是否避免跨子系统范围扩张：是。  
7. 若要求函数/数据结构级任务，是否真正落到了这些对象：是（接口方法与对象字段来自 6.5/6.6 明确定义）。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 处置策略 | 回退策略 |
|---|---|---|---|---|---|
| Build/Runtime 双源漂移 | High | resolver 与 runtime_policy 字段不一致 | 一致性测试失败 | 触发 PRF-GATE-04 阻断合入 | 回退到上一个通过矩阵版本 |
| profiles 越权侵入 runtime/cognition | High | profiles 开始承载恢复决策或上下文装配 | 边界评审失败 | 触发 ADR 边界门并拆分职责 | 移除越权逻辑，恢复参数化输入 |
| contracts 语义污染 | High | 将 profile 开关写入 contracts 公共对象 | contract 测试失败 | 立即阻断并回退对象变更 | 回退至私有对象边界 |
| schema 演进 breaking | High | 修改旧字段语义而非新增字段 | 兼容测试失败 | 触发 PRF-GATE-08 评审门 | 回退并改为新增字段 + 迁移窗口 |
| LKG 回退不可用 | Medium | 激活失败且无法读取 LKG | 激活失败无可用快照 | 先保障拒绝不替换当前快照 | 回退至只读基线模式 |
| integration 长期缺失 | Medium | tests 顶层未接线导致集成验证缺位 | ctest -N 无 integration | 提升 PRF-BLK-06 优先级 | 阶段内仅宣称 unit/contract 就绪 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合）；部分任务需先补设计与接线后执行。
2. 原因：
   - 详细设计已明确核心接口清单、方法语义、错误码与流程时序。
   - 核心对象字段与私有边界已给出，可支撑对象级原子任务。
   - Design -> Build 映射、目录落盘建议、测试矩阵与质量门已给出。
   - 当前主要缺口是工程接线与跨子系统输入冻结，不是接口语义空白。
   - ADR 边界明确，可作为越界阻断门禁。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构。
4. 未达全量函数级的缺口：
   - profiles 模块 CMake 尚未接入；
   - overlay 输入契约未冻结；
   - schema_version 与字段必填规则未冻结；
   - integration 顶层测试接线缺失；
   - telemetry 对接 infra 接口签名未冻结。
5. 下一步建议：
   - 先执行 PRF-TODO-014、015、013 打通接线与资产冻结；
   - 并行推进 PRF-TODO-001~005 与 PRF-TODO-006~008、010；
   - 同步完成 PRF-TODO-019~021 解阻；
   - 解阻后推进 PRF-TODO-009、016、017，并由 PRF-TODO-018 收口门禁证据。
