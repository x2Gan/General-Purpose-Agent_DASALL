# DASALL platform 子系统 linux 组件专项 TODO

最近更新时间：2026-03-27  
阶段：Detailed Design -> Special TODO  
适用范围：platform/linux

## 1. 文档头

本文档严格基于以下输入生成：

1. docs/architecture/platform_linux_detailed_design.md
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
21. 代码与构建现状：platform/CMakeLists.txt、platform/src/placeholder.cpp、platform/src/linux/、platform/src/arm/hal/、tests/CMakeLists.txt、tests/unit/CMakeLists.txt、build-ci/

生成原则：

1. 不改写已冻结 ADR 结论。
2. 不越过 platform/linux 边界扩张到无关模块。
3. 讨论类事项不作为 Done-ready Build 任务。
4. 每项任务必须包含代码目标、测试目标、验收命令。
5. 设计证据不足处先列 Blocked 与补设计前置项，不伪造实现任务。

## 2. 子系统目标与范围

### 2.1 组件目标

1. 落地 Linux 通用平台抽象，覆盖线程、定时器、队列、文件、网络、IPC 与受控 HAL 桥接。
2. 将平台差异收敛到工厂与能力表，支撑 desktop_full/cloud_full 与 edge_* profile 的一致主流程。
3. 输出可判定平台错误与健康事实，供上层映射 ErrorInfo/Observation，不反向污染 contracts。
4. 建立 platform/linux 的可构建、可测试、可门禁执行路径。

### 2.2 范围边界

纳入范围：

1. platform/linux 的接口、数据结构、错误语义、初始化流程、异常流程与配置模型。
2. platform/linux 的 CMake 接线、unit/integration 测试注册与验收门禁。
3. 阻塞项、解阻条件、风险与回退策略。

不纳入范围：

1. runtime/cognition 的业务重试、重规划和主控裁定逻辑。
2. contracts 共享对象字段扩写或 platform 接口外溢到 contracts 目录。
3. 真实 ARM HAL 驱动实现（GPIO/UART/I2C/SPI/CAN），本轮仅保留桥接点与桩。

## 3. 输入依据与约束清单

### 3.1 约束清单（Step 1 输出）

| ID | 来源 | 类型 | 约束内容 | 对 TODO 的影响 |
|---|---|---|---|---|
| PLAT-TC001 | 架构 3.4.6；蓝图 3.11 | Must | platform/linux 必须抽象线程/锁/队列/定时器/文件/网络/IPC | 任务必须覆盖 6 组核心接口与对应实现 |
| PLAT-TC002 | 架构 3.7；蓝图 4.2 | Must | platform 不依赖上层业务模块 | 代码目标限定在 platform/tests/docs/cmake |
| PLAT-TC003 | 蓝图 4.3；ADR-005 | Must | contracts 冻结基线不被 platform 反向改写 | 错误与能力事实仅映射，不进入 contracts 扩写 |
| PLAT-TC004 | 蓝图 5.1；架构 7.5 | Must | Profile 只能裁剪能力与替换实现，不得绕过主控链路 | 工厂与能力表任务必须覆盖 profile 差异 |
| PLAT-TC005 | ADR-006 | Must-Not | platform 不承担上下文装配与 Prompt 渲染 | 禁止产生 cognition/llm 语义任务 |
| PLAT-TC006 | ADR-007 | Must-Not | platform 不做恢复决策与重试裁定 | 异常任务只返回平台事实与错误码 |
| PLAT-TC007 | ADR-008 | Must | platform 不拥有多 Agent 调度主权 | 禁止在任务中引入调度状态机逻辑 |
| PLAT-TC008 | 工程规范 3.2 | Must | 公共头文件在 platform/include，命名与 include 规范一致 | 接口任务必须显式落盘头文件并可编译 |
| PLAT-TC009 | 工程规范 3.6 | Must | 失败不可吞错，必须可判定与可观测 | 错误任务必须绑定映射测试或失败路径测试 |
| PLAT-TC010 | 工程规范 3.7 | Should | 公共接口应配套 unit/integration 测试 | 接口与 provider 任务必须绑定测试出口 |
| PLAT-TC011 | 架构 9.2；platform 设计 6.7 | Must | 平台初始化在 Profile Bind 后、Service Init 前完成 | 工厂与集成任务必须验证启动顺序 |
| PLAT-TC012 | platform 设计 6.4 | Must-Not | 上层观测仅通过快照/事件接口，不 include Linux 私有实现 | 健康与事件任务需提供边界守卫测试 |
| PLAT-TC013 | platform 设计 6.9；11.1 | Must | 存在未冻结设计项（HAL 真实接口、epoll fallback、profile 注入键） | 必须设置 Blocked 与补设计前置任务 |

### 3.2 代码现状证据

| 证据对象 | 当前状态 | 结论 |
|---|---|---|
| platform/CMakeLists.txt | 仅编译 platform/src/placeholder.cpp 到 dasall_platform | platform/linux 实现尚未接入构建 |
| platform/include/ | 仅存在空的 hal/ 目录 | 公共接口与对象头文件未落盘 |
| platform/src/linux/ | 空目录 | linux provider 与工厂实现缺失 |
| platform/src/arm/hal/ | 空目录 | HAL 接入点与桩缺失 |
| tests/unit/CMakeLists.txt | 仅 runtime/cognition/llm/tools/memory/knowledge 子目录 | platform unit 测试未注册 |
| tests/CMakeLists.txt | 仅 mocks/unit/contract，未接入 integration 顶层 | platform integration 门禁当前不可执行 |
| build-ci/ | 已存在可用构建目录与 ctest 入口 | 可复用统一命令作为验收基线 |

## 4. 粒度可行性评估

### 4.1 粒度结论

结论：可直接生成并执行 L3/L2 混合专项 TODO。  
当前最小可执行粒度：函数/接口/数据结构级（L3 为主，受阻项降为 L0 Blocked）。

证据：

1. 详细设计已给出核心接口与方法语义：IThread/ITimer/IQueue/IFileSystem/INetwork/IIPC、ILinuxPlatformBundleFactory（设计 6.6）。
2. 已给出核心对象字段：PlatformInitConfig、PlatformCapabilitySet、PlatformError、ThreadOptions、TimerSpec、QueueOptions、SocketEndpoint、PlatformHealthSnapshot（设计 6.5）。
3. 已给出初始化主流程、运行期流程、异常恢复原则（设计 6.7/6.8）。
4. 已给出配置项与 profile 默认策略（设计 6.9）。
5. 已给出 Design->Build 映射、目录落盘建议、测试矩阵与 Gate（设计 7/8/9）。
6. 仍有阻塞：HAL 真实接口冻结、tests integration 顶层接线、profile 键注入路径统一（设计 11.1 + 代码现状）。

### 4.2 粒度可行性评估表（Step 2 输出）

| 设计对象 | 设计锚点 | 当前粒度等级 | 已具备证据 | 缺失证据 | TODO 拆解策略 |
|---|---|---|---|---|---|
| PlatformInitConfig | 6.5 | L3 | 字段与来源约束明确 | 键名注入入口统一细节 | 直接拆数据结构任务 |
| PlatformCapabilitySet | 6.5/6.7 | L3 | enabled/disabled/degraded + reason 语义明确 | reason_code 规范化表未落盘 | 直接拆数据结构任务 |
| PlatformError/PlatformResult | 6.5/6.6/6.8 | L3 | 错误分类与返回约束明确 | ErrorInfo 映射评审结论未冻结 | 直接拆错误模型 + 评审门 |
| IThread | 6.6 | L3 | create/join/request_stop 语义与错误语义明确 | 句柄生命周期细节待测试固化 | 直接拆接口任务 |
| ITimer | 6.6 | L3 | start_once/start_periodic/cancel 语义明确 | 漂移阈值默认值评审待补 | 直接拆接口任务 |
| IQueue | 6.6 | L3 | create/push/pop/close 与错误语义明确 | item 抽象策略（字节或对象）未冻结 | 先接口冻结后实现 |
| IFileSystem | 6.6 | L3 | read_file/write_atomic/ensure_directory/stat 明确 | 原子写覆盖策略边界细节 | 直接拆接口任务 |
| INetwork | 6.6/6.9 | L3 | connect/send/receive/shutdown 与超时语义明确 | epoll->poll fallback 触发条件 | 直接拆接口任务 + 补设计前置 |
| IIPC | 6.6/6.9 | L3 | listen/accept/connect/send/receive/close 明确 | framing 语义由上层承接边界待补 | 直接拆接口任务 |
| LinuxPlatformFactory | 6.2/6.3/6.7 | L3 | create(config) 与装配流程明确 | profile 配置键统一未冻结 | 直接拆实现任务 |
| CapabilityRegistry | 6.2/6.3/6.7 | L3 | 能力状态记录规则明确 | 状态持久周期与导出频率待补 | 直接拆实现任务 |
| HalAvailabilityBridge | 6.2/6.8/11.1 | L2 | 桥接点职责明确、可先空实现 | 真实 ARM HAL 接口未冻结 | 先做桥接与 stub，真实驱动 Blocked |
| tests/integration/platform/linux | 8.1/9.1 + 现状 | L0 | 用例方向与门禁已给出 | tests 顶层 integration 未接线 | 先解阻测试拓扑再推进 |

## 5. Design -> TODO 映射表

### 5.1 映射总表（Step 3 输出）

| Design 项 | 设计锚点 | TODO 类型 | 对应任务 ID | 映射说明 |
|---|---|---|---|---|
| 冻结平台初始化与能力对象 | 6.5 | 数据结构 | PLAT-LNX-TODO-001、002 | 先固化对象字段与边界，降低实现分歧 |
| 冻结平台错误模型 | 6.5/6.8 | 异常与错误处理 | PLAT-LNX-TODO-003 | 先统一错误表达，再推进 provider 失败路径 |
| 冻结六类平台公共接口 | 6.6 | 接口定义 | PLAT-LNX-TODO-004~009 | 一接口一任务，保证原子性与可评审性 |
| 建立初始化与能力探测链路 | 6.2/6.3/6.7 | 生命周期与初始化 | PLAT-LNX-TODO-010、011 | 工厂与能力注册分拆，防止任务过大 |
| 落地并发/IO/IPC provider | 6.2/6.6/6.8 | 适配器/桥接 | PLAT-LNX-TODO-012~017 | 每个 provider 独立实现和验收 |
| 建立 HAL 受控桥接 | 6.2/6.8/11.1 | 配置与 Profile 裁剪 | PLAT-LNX-TODO-018 | 只实现桥接与桩，不承诺真实驱动 |
| 接入测试发现性与集成门禁 | 8.1/9.1/9.2 | 测试与门禁 | PLAT-LNX-TODO-019~021 | 先注册后验收，避免空命令 |
| 收敛交付证据与门禁回写 | 9.2/11.1 | 文档/交付证据回写 | PLAT-LNX-TODO-022 | 以命令证据回写 gate 与阻塞状态 |
| 未决设计项补完 | 11.1 | 补设计前置 | PLAT-LNX-TODO-023~025 | 将缺口显式前置，禁止伪造实现 |

### 5.2 映射覆盖性检查

| 类型 | 是否覆盖 | 任务 ID |
|---|---|---|
| 接口定义类任务 | 是 | PLAT-LNX-TODO-004~009 |
| 数据结构定义类任务 | 是 | PLAT-LNX-TODO-001~003 |
| 生命周期与初始化类任务 | 是 | PLAT-LNX-TODO-010~011 |
| 适配器/桥接类任务 | 是 | PLAT-LNX-TODO-012~018 |
| 异常与错误处理类任务 | 是 | PLAT-LNX-TODO-003、012~018 |
| 配置与 Profile 裁剪类任务 | 是 | PLAT-LNX-TODO-010、018、025 |
| 测试与门禁类任务 | 是 | PLAT-LNX-TODO-019~021 |
| 文档/交付证据回写类任务 | 是 | PLAT-LNX-TODO-022 |

## 6. 原子任务清单

### 6.1 原子任务表（Step 4 输出）

| ID | 状态 | 任务 | 来源依据 | 设计锚点 | 粒度等级 | 代码目标 | 目标函数/接口/数据结构 | 测试目标 | 验收命令 | 前置依赖 | 阻塞项 | 解阻条件 | 交付物 | 完成判定 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| PLAT-LNX-TODO-001 | Done | 定义 PlatformInitConfig 数据结构头文件 | platform 设计 6.5；工程规范 3.2 | 6.5 PlatformInitConfig | L3 | platform/include/linux/PlatformInitConfig.h | target_platform/profile_name/enable_hal/queue_defaults/io_timeouts | unit：字段编译与默认值断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform | 无 | 无 | 无 | 头文件、编译记录；2026-03-26 已新增 PlatformInitConfig.h、PlatformInitConfigTest，并执行构建与定向 unit 验证 | 仅当字段集合与设计一致且 dasall_platform 构建通过时完成 |
| PLAT-LNX-TODO-002 | Done | 定义 PlatformCapabilitySet 数据结构头文件 | platform 设计 6.5/6.7 | 6.5 PlatformCapabilitySet | L3 | platform/include/linux/LinuxPlatformCapabilities.h | thread/timer/queue/filesystem/network/ipc/hal 状态与 reason | unit：状态枚举与 reason 字段断言 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform | PLAT-LNX-TODO-001 | 无 | 无 | 头文件、编译记录；2026-03-27 已新增 LinuxPlatformCapabilities.h、LinuxPlatformCapabilitiesTest，并执行构建与定向 unit 验证 | 仅当能力项与三态语义完整且可编译时完成 |
| PLAT-LNX-TODO-003 | Done | 定义 PlatformError 与 PlatformResult 头文件 | platform 设计 6.5/6.8；工程规范 3.6 | 6.5 PlatformError；6.8 异常分类 | L3 | platform/include/PlatformError.h、platform/include/PlatformResult.h | code/category/retryable_hint/syscall_name/errno_value/detail | unit：PlatformErrorMappingTest（或同等映射单测） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure | 无 | PLAT-LNX-BLK-04 | ErrorInfo 映射评审通过 | 头文件、映射测试；2026-03-27 已新增 PlatformError.h/PlatformResult.h、PlatformErrorMappingTest，并以一级失败域映射锚点完成 BLK-04 最小解阻 | 仅当错误字段可判定且映射测试通过（或保持 Blocked）时完成 |
| PLAT-LNX-TODO-004 | Done | 定义 IThread 接口头文件 | platform 设计 6.6 | 6.6 IThread | L3 | platform/include/IThread.h | create_thread/join_thread/request_stop | unit：InterfaceSurfaceTest 覆盖 IThread | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | 无 | 无 | 接口头文件、接口单测；2026-03-27 已新增 IThread.h、InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当方法语义与错误语义对齐且接口测试通过时完成 |
| PLAT-LNX-TODO-005 | Done | 定义 ITimer 接口头文件 | platform 设计 6.6 | 6.6 ITimer | L3 | platform/include/ITimer.h | start_once/start_periodic/cancel | unit：InterfaceSurfaceTest 覆盖 ITimer | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | 无 | 无 | 接口头文件、接口单测；2026-03-27 已新增 ITimer.h、扩展 InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当方法语义、错误语义与超时约束可二值判定时完成 |
| PLAT-LNX-TODO-006 | Done | 定义 IQueue 接口头文件 | platform 设计 6.6 | 6.6 IQueue | L3 | platform/include/IQueue.h | create_queue/push/pop/close | unit：InterfaceSurfaceTest 覆盖 IQueue | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | 无 | 无 | 接口头文件、接口单测；2026-03-27 已新增 IQueue.h、扩展 InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当 QueueClosed/ResourceExhausted 语义可测试断言时完成 |
| PLAT-LNX-TODO-007 | Done | 定义 IFileSystem 接口头文件 | platform 设计 6.6 | 6.6 IFileSystem | L3 | platform/include/IFileSystem.h | read_file/write_atomic/ensure_directory/stat | unit：InterfaceSurfaceTest 覆盖 IFileSystem | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | 无 | 无 | 接口头文件、接口单测；2026-03-27 已新增 IFileSystem.h、扩展 InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当 NotFound/PermissionDenied/NoSpace 语义可判定时完成 |
| PLAT-LNX-TODO-008 | Done | 定义 INetwork 接口头文件 | platform 设计 6.6/6.9 | 6.6 INetwork | L3 | platform/include/INetwork.h | connect/send/receive/shutdown | unit：InterfaceSurfaceTest 覆盖 INetwork | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | PLAT-LNX-BLK-03（仅影响 provider 实现，接口层已最小解阻） | fallback 策略补设计通过评审（影响 PLAT-LNX-TODO-016） | 接口头文件、接口单测；2026-03-27 已新增 INetwork.h、扩展 InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当接口语义冻结且 fallback 边界不冲突时完成 |
| PLAT-LNX-TODO-009 | Done | 定义 IIPC 接口头文件 | platform 设计 6.6/6.9 | 6.6 IIPC | L3 | platform/include/IIPC.h | listen/accept/connect/send/receive/close | unit：InterfaceSurfaceTest 覆盖 IIPC | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | PLAT-LNX-TODO-003 | 无 | 无 | 接口头文件、接口单测；2026-03-27 已新增 IIPC.h、扩展 PlatformErrorCode（PeerClosed/PayloadTooLarge）、扩展 InterfaceSurfaceTest 并完成定向构建与单测验证 | 仅当 AddressInUse/PeerClosed/PayloadTooLarge 路径可判定时完成 |
| PLAT-LNX-TODO-010 | Done | 实现 LinuxPlatformFactory create(config) 骨架 | platform 设计 6.2/6.3/6.7；架构 9.2 | 6.7 初始化主流程 | L3 | platform/include/linux/LinuxPlatformFactory.h、platform/src/linux/LinuxPlatformFactory.cpp | create(config) | unit：LinuxPlatformFactoryTest（初始化顺序、必需能力缺失阻断） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && cmake --build build-ci --target dasall_linux_platform_factory_unit_test && ctest --test-dir build-ci -R LinuxPlatformFactoryTest --output-on-failure | PLAT-LNX-TODO-001~009 | PLAT-LNX-BLK-02（已按“工厂仅接受结构体输入”最小解阻） | profile 注入键与输入结构统一 | 工厂代码、单测；2026-03-27 已新增 LinuxPlatformFactory.h/.cpp 与 LinuxPlatformFactoryTest，验证 ProfileBound 先于 ReadyForServiceInit，且必需能力缺失触发阻断 | 仅当初始化顺序符合 Profile Bind 后置条件且失败路径可判定时完成 |
| PLAT-LNX-TODO-011 | Not Started | 实现 CapabilityRegistry 状态登记骨架 | platform 设计 6.2/6.3/6.7 | 6.3 CapabilityRegistry | L3 | platform/src/linux/CapabilityRegistry.cpp | set_capability/get_capability/snapshot | unit：LinuxPlatformFactoryTest 或独立 CapabilityRegistryTest | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R "LinuxPlatformFactoryTest|CapabilityRegistryTest" --output-on-failure | PLAT-LNX-TODO-002、010 | 无 | 无 | 实现代码、单测 | 仅当 enabled/disabled/degraded 与 reason 可重复断言时完成 |
| PLAT-LNX-TODO-012 | Not Started | 实现 PosixThreadProvider 骨架 | platform 设计 6.2/6.6 | 6.2 PosixThreadProvider；6.6 IThread | L3 | platform/src/linux/PosixThreadProvider.cpp | create_thread/join_thread/request_stop | unit：PosixThreadProviderTest（create/join/timeout） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PosixThreadProviderTest --output-on-failure | PLAT-LNX-TODO-004、003 | 无 | 无 | 实现代码、单测 | 仅当 ResourceExhausted/Timeout 路径均可复现并断言通过时完成 |
| PLAT-LNX-TODO-013 | Not Started | 实现 PosixTimerProvider 骨架 | platform 设计 6.2/6.6/6.8 | 6.2 PosixTimerProvider；6.6 ITimer | L3 | platform/src/linux/PosixTimerProvider.cpp | start_once/start_periodic/cancel | unit：PosixTimerProviderTest（周期/取消/漂移） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PosixTimerProviderTest --output-on-failure | PLAT-LNX-TODO-005、003 | 无 | 无 | 实现代码、单测 | 仅当 Cancelled 与漂移计数行为可二值断言时完成 |
| PLAT-LNX-TODO-014 | Not Started | 实现 BlockingQueueProvider 骨架 | platform 设计 6.2/6.6/6.8 | 6.2 BlockingQueueProvider；6.6 IQueue | L3 | platform/src/linux/BlockingQueueProvider.cpp | create_queue/push/pop/close | unit：BlockingQueueProviderTest（overflow/close/timeout） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R BlockingQueueProviderTest --output-on-failure | PLAT-LNX-TODO-006、003 | 无 | 无 | 实现代码、单测 | 仅当 QueueClosed 与 ResourceExhausted 路径都可判定时完成 |
| PLAT-LNX-TODO-015 | Not Started | 实现 LinuxFileSystemProvider 骨架 | platform 设计 6.2/6.6/6.8 | 6.2 LinuxFileSystemProvider；6.6 IFileSystem | L3 | platform/src/linux/LinuxFileSystemProvider.cpp | read_file/write_atomic/ensure_directory/stat | unit：LinuxFileSystemProviderTest（NotFound/PermissionDenied/NoSpace） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R LinuxFileSystemProviderTest --output-on-failure | PLAT-LNX-TODO-007、003 | 无 | 无 | 实现代码、单测 | 仅当原子写与权限/空间失败路径可重复验证时完成 |
| PLAT-LNX-TODO-016 | Not Started | 实现 LinuxNetworkProvider 骨架 | platform 设计 6.2/6.6/6.8/6.9 | 6.2 LinuxNetworkProvider；6.9 connect_timeout/io_timeout | L3 | platform/src/linux/LinuxNetworkProvider.cpp | connect/send/receive/shutdown | unit：LinuxNetworkProviderTest（Timeout/Disconnected/ConnectionRefused） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R LinuxNetworkProviderTest --output-on-failure | PLAT-LNX-TODO-008、003 | PLAT-LNX-BLK-03 | fallback 触发条件冻结 | 实现代码、单测或阻塞记录 | 仅当超时与断连语义可稳定复现且不隐式重试时完成 |
| PLAT-LNX-TODO-017 | Not Started | 实现 UnixIpcProvider 骨架 | platform 设计 6.2/6.6/6.8 | 6.2 UnixIpcProvider；6.6 IIPC | L3 | platform/src/linux/UnixIpcProvider.cpp | listen/accept/connect/send/receive/close | unit：UnixIpcProviderTest（AddressInUse/PeerClosed/PayloadTooLarge） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R UnixIpcProviderTest --output-on-failure | PLAT-LNX-TODO-009、003 | 无 | 无 | 实现代码、单测 | 仅当 IPC 关键错误路径与关闭语义全部可判定时完成 |
| PLAT-LNX-TODO-018 | Not Started | 实现 HalAvailabilityBridge 与 HalStub 桩 | platform 设计 6.2/6.8/11.1；蓝图 5.1 | 6.2 HalAvailabilityBridge；8.1 HalStub.cpp | L2 | platform/src/linux/HalAvailabilityBridge.cpp、platform/src/arm/hal/HalStub.cpp | probe_hal_availability（桥接语义） | unit：HalAvailabilityBridgeTest（desktop 关闭、edge 可判定） | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R HalAvailabilityBridgeTest --output-on-failure | PLAT-LNX-TODO-010、011 | PLAT-LNX-BLK-01 | HAL 最小接口冻结或确认仅 stub 交付 | 实现代码、单测或阻塞记录 | 仅当 desktop/edge 两档行为都可二值判定时完成 |
| PLAT-LNX-TODO-019 | Not Started | 注册 platform unit 测试目录与目标 | platform 设计 8.1/9.1；工程规范 3.7 | 8.1 目录建议；9.1 测试矩阵 | L2 | tests/unit/CMakeLists.txt、tests/unit/platform/linux/ | unit：InterfaceSurface/Factory/Provider 测试发现性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_unit_tests && ctest --test-dir build-ci -N -R "Platform|Linux" | PLAT-LNX-TODO-004~018 | 无 | 无 | CMake 变更、测试注册记录 | 仅当 ctest -N 可发现新增 platform unit 测试时完成 |
| PLAT-LNX-TODO-020 | Blocked | 注册 platform integration 测试目录与目标 | platform 设计 8.1/9.1；代码现状 | 9.1 Integration 覆盖 | L0 | tests/CMakeLists.txt、tests/integration/platform/linux/ | integration：LinuxPlatformBootstrapIntegrationTest 发现性 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R LinuxPlatformBootstrapIntegrationTest | PLAT-LNX-TODO-019 | PLAT-LNX-BLK-05 | tests 顶层 integration 接线冻结 | CMake 改动或阻塞记录 | 仅当 integration 用例可被 ctest 发现后解阻 |
| PLAT-LNX-TODO-021 | Blocked | 验证平台初始化集成路径 | platform 设计 6.7/9.1/9.2；架构 9.2 | 6.7 初始化步骤 1~7 | L2 | tests/integration/platform/linux/LinuxPlatformBootstrapIntegrationTest.cpp | integration：desktop_full 与 edge_balanced 主/降级路径 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -R LinuxPlatformBootstrapIntegrationTest --output-on-failure | PLAT-LNX-TODO-010、011、018、020 | PLAT-LNX-BLK-02、PLAT-LNX-BLK-05 | profile 注入键统一 + integration 接线完成 | 集成测试与执行记录 | 仅当两档 profile 初始化结果均可二值断言时完成 |
| PLAT-LNX-TODO-022 | Not Started | 回写 platform/linux 门禁与交付证据 | platform 设计 9.2/11.1 | 9.2 Gate；11.1 阻塞管理 | L2 | docs/todos/DASALL_platform_linux组件专项TODO.md | process test：门禁结果、阻塞变更、回退触发记录 | ctest --test-dir build-ci -N && ctest --test-dir build-ci --output-on-failure -L unit && ctest --test-dir build-ci --output-on-failure -R LinuxPlatformBootstrapIntegrationTest | PLAT-LNX-TODO-019~021 | 无 | 无 | 更新后的 TODO 文档证据段 | 仅当每个 Gate 都有通过/失败结论与命令证据时完成 |
| PLAT-LNX-TODO-023 | Not Started | 补齐 HAL 最小接口设计前置 | platform 设计 11.1 | 11.1 HAL 真实接口未冻结 | L0 | docs/architecture/platform_linux_detailed_design.md | Hal 最小桥接接口（方法、返回码、能力探测边界） | process test：设计评审门 | rg -n "HAL|HalAvailabilityBridge|HalStub|接口" docs/architecture/platform_linux_detailed_design.md | 无 | 无 | 评审通过并回链 PLAT-LNX-TODO-018 | 设计补丁、评审记录、回链记录 | 仅当 HAL 最小接口清单冻结且评审通过时完成 |
| PLAT-LNX-TODO-024 | Not Started | 补齐网络 fallback 策略前置设计 | platform 设计 6.9/11.1 | 6.9 enable_epoll；11.1 未决项 | L0 | docs/architecture/platform_linux_detailed_design.md | epoll/eventfd 失败触发 poll/select 的条件与错误语义 | process test：设计评审门 | rg -n "enable_epoll|poll|select|fallback" docs/architecture/platform_linux_detailed_design.md | 无 | 无 | 评审通过并回链 PLAT-LNX-TODO-008、016 | 设计补丁、评审记录、回链记录 | 仅当 fallback 条件可判定且不引入隐式重试时完成 |
| PLAT-LNX-TODO-025 | Not Started | 补齐 profile 注入键与入口设计前置 | platform 设计 6.9/11.1；架构 7.5 | 6.9 配置项；11.1 注入路径未统一 | L0 | docs/architecture/platform_linux_detailed_design.md、profiles/ | platform.linux.* 键名、注入入口、覆盖层级矩阵 | process test：配置评审门 | rg -n "platform.linux|profile|覆盖层级|注入" docs/architecture/platform_linux_detailed_design.md profiles -g "**/*" | 无 | 无 | 评审通过并回链 PLAT-LNX-TODO-010、021 | 设计补丁、评审记录、回链记录 | 仅当键名/入口/覆盖优先级三项冻结时完成 |

## 7. 执行顺序建议

### 7.1 串并行编排（Step 5 输出）

| 阶段 | 任务 ID | 串并行建议 | 说明 |
|---|---|---|---|
| A 对象与错误模型冻结 | PLAT-LNX-TODO-001~003 | 串行 | 先冻结对象和错误语义，作为接口/实现输入 |
| B 接口冻结 | PLAT-LNX-TODO-004~009 | 可并行 | 六类接口互相解耦，可并行推进 |
| C 初始化链路 | PLAT-LNX-TODO-010~011 | 串行 | 工厂依赖接口与对象；能力注册依赖工厂 |
| D provider 落地 | PLAT-LNX-TODO-012~018 | 可并行分组 | 并发组(012~014)与 I/O 组(015~017)并行，HAL 桥接独立 |
| E 测试接线与发现性 | PLAT-LNX-TODO-019~020 | 串行（020 当前 Blocked） | 先 unit 注册，再 integration 接线 |
| F 集成门禁 | PLAT-LNX-TODO-021 | 串行（Blocked） | 依赖 profile 注入与 integration 接线 |
| G 补设计收敛 | PLAT-LNX-TODO-023~025 | 可并行 | 作为阻塞解消输入，反哺 008/010/016/018/021 |
| H 证据回写 | PLAT-LNX-TODO-022 | 串行 | 收敛 Gate、阻塞、回退执行证据 |

### 7.2 必过门禁表

| Gate ID | 门禁名称 | 触发时机 | 通过条件 | 未通过处理 |
|---|---|---|---|---|
| PLAT-GATE-01 | 接口冻结门 | 进入 provider 实现前 | 004~009 头文件落盘并可编译 | 退回接口任务修正 |
| PLAT-GATE-02 | 错误语义门 | 进入 I/O 与 IPC 失败路径测试前 | 003 映射评审通过或阻塞结论明确 | 冻结错误域并停止扩展错误码 |
| PLAT-GATE-03 | 工厂初始化门 | 进入集成测试前 | 010/011 完成，初始化顺序符合架构 9.2 | 修复工厂流程或配置注入 |
| PLAT-GATE-04 | HAL 裁剪门 | 018 完成后 | desktop_full 下 HAL 可关闭，edge_* 路径可判定 | 保持 stub 路径并禁止宣称真实 HAL 完成 |
| PLAT-GATE-05 | 构建门 | 每批任务合入前 | cmake --build build-ci --target dasall_platform 通过 | 回滚本批改动并修复构建 |
| PLAT-GATE-06 | 测试发现性门 | 019/020 后 | ctest -N 可发现 platform unit/integration 用例 | 修复 tests CMake 接线 |
| PLAT-GATE-07 | 集成验证门 | 021 前后 | LinuxPlatformBootstrapIntegrationTest 可执行并通过 | 保持 Blocked，不进入发布前验收 |
| PLAT-GATE-08 | breaking 评审门 | 任意 public 接口签名变更前 | 评审记录含迁移窗口与回退方案 | 未评审不得推进变更 |

## 8. 阻塞项与解阻条件

| 阻塞项 ID | 阻塞描述 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|---|
| PLAT-LNX-BLK-01 | ARM HAL 真实接口未冻结 | PLAT-LNX-TODO-018 | 冻结 HAL 最小接口与错误码边界 | 先交付 HalStub + bridge reason code | 本轮不接真实驱动 |
| PLAT-LNX-BLK-02 | profile 配置键注入路径未统一 | PLAT-LNX-TODO-021（PLAT-LNX-TODO-010 已最小解阻） | 冻结 platform.linux.* 键名与注入入口 | 在测试 fixture 固定注入结构体 | 工厂先只接受结构体输入 |
| PLAT-LNX-BLK-03 | epoll/eventfd fallback 条件未冻结 | PLAT-LNX-TODO-008、016 | 评审通过 fallback 触发矩阵 | 先限定首版行为并补设计 | 无法评审前禁止扩展回退逻辑 |
| PLAT-LNX-BLK-04 | PlatformError->ErrorInfo 映射评审未完成 | PLAT-LNX-TODO-003 | 映射表评审通过并有记录 | 先冻结 PlatformError 字段和码集 | 延后细粒度错误码扩展 |
| PLAT-LNX-BLK-05 | tests 顶层 integration 未接线 | PLAT-LNX-TODO-020、021 | tests/CMakeLists 接入 integration 并冻结标签规范 | 先完成 unit/contract 门禁 | 集成门禁保持 Blocked |

## 9. 验收与质量门

### 9.1 验收命令基线

1. 构建基线：
   - cmake -S . -B build-ci -G Ninja
   - cmake --build build-ci --target dasall_platform
2. 单元测试发现与执行基线：
   - ctest --test-dir build-ci -N
   - ctest --test-dir build-ci --output-on-failure -L unit
3. 契约影响基线（若映射测试挂到 contract 标签）：
   - ctest --test-dir build-ci --output-on-failure -L contract
4. 集成基线（解阻后执行）：
   - ctest --test-dir build-ci --output-on-failure -R LinuxPlatformBootstrapIntegrationTest

说明：

1. 在 PLAT-LNX-BLK-05 解阻前，integration 不作为必过基线。
2. 所有 Build-ready 任务都绑定至少 1 条构建命令与 1 条测试命令。

### 9.2 质量门逐项回答

1. 是否给出 Design -> TODO 映射，而不是只列任务标题：是。  
2. 是否明确当前最细可达到粒度等级：是（L3/L2，阻塞项 L0）。  
3. 是否所有任务都满足代码目标 + 测试目标 + 验收命令：是。  
4. 是否所有 Blocked 项都带有证据和解阻条件：是。  
5. 是否所有任务都具备可二值判定完成标准：是。  
6. 是否避免跨子系统范围扩张：是。  
7. 若要求函数/数据结构级任务，是否真正落到对象：是（接口方法与核心对象字段已落点）。

## 10. 风险与回退策略

| 风险 | 等级 | 触发条件 | 监测信号 | 处置策略 | 回退策略 |
|---|---|---|---|---|---|
| 接口签名 breaking 漂移 | High | 未经评审直接改 IThread/IQueue/INetwork 等 public 接口 | 接口测试失败或消费方编译失败 | 触发 PLAT-GATE-08，停止合入 | 回退到上一个接口冻结版本 |
| 平台边界被破坏 | High | platform include 上层实现或引入业务语义 | include 审计出现 runtime/cognition 依赖 | 代码评审阻断并拆分职责 | 移除越界依赖，恢复纯平台事实输出 |
| Profile 差异散落主流程 | High | 业务层新增 target_platform 分支 | runtime/cognition 出现平台条件分支 | 强制改回工厂/能力表注入 | 回退该分支改动并补能力注册 |
| fallback 逻辑不可验证 | Medium | 未冻结 epoll->poll 条件即落地复杂回退 | 测试不稳定、行为漂移 | 先补设计再实现 | 保留单后端实现并标记 Blocked |
| HAL 路径与 Linux 通用层耦死 | Medium | src/linux 直接依赖 src/arm/hal 细节 | 跨目录 include 增多 | 桥接接口隔离 + stub 优先 | 仅保留 HalStub，停用真实驱动接入 |
| 集成门禁长期缺失 | Medium | tests integration 顶层迟迟不接线 | ctest -N 无 integration 条目 | 先完成 tests/CMake 接线任务 | 集成任务维持 Blocked，不宣称完成 |

## 11. 可行性结论

1. 结论：可直接生成并执行函数/数据结构级专项 TODO（L3/L2 混合），并行推进实现；集成相关任务受阻塞项约束。
2. 原因：
   - 详细设计已明确核心接口、对象字段、主异常流程与错误语义（6.5/6.6/6.7/6.8）。
   - 已提供配置模型与 profile 默认策略，可支撑配置与裁剪类任务（6.9）。
   - 已提供 Design->Build 映射、目录建议、测试矩阵与 Gate（7/8/9）。
   - 代码现状明确显示缺口位置，可形成可执行任务与阻塞门。
   - ADR 与蓝图边界清晰，可防止越界与 contracts 污染。
3. 当前最小可执行粒度：函数 / 接口 / 数据结构。
4. 未达全量函数级的缺口：HAL 真实接口冻结、fallback 触发矩阵、profile 注入键入口统一、integration 顶层接线。
5. 下一步建议：优先并行完成 PLAT-LNX-TODO-023~025 解阻，再推进 018/020/021，最后执行 022 做门禁证据收口。

## 12. 本轮执行记录（2026-03-26 / PLAT-LNX-TODO-001）

### 12.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-001。
2. 可执行性依据：无前置依赖、无阻塞项、范围限定在单个数据结构头文件与最小 unit 验证，可在一轮提交内完成。

### 12.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformInitConfig 稳定字段为 target_platform、profile_name、enable_hal、queue_defaults、io_timeouts。
2. docs/architecture/platform_linux_detailed_design.md 6.7 明确该对象由 Boot 阶段在 Profile Bind 后注入 LinuxPlatformFactory，platform 不自行加载配置文件。
3. docs/architecture/platform_linux_detailed_design.md 6.9 明确首版默认值基线：desktop_full/cloud_full 默认关闭 HAL，queue 默认容量 1024、overflow_policy 为 reject，network connect/io timeout 默认分别为 3000/5000 ms。

外部参考：

1. OpenTelemetry Common Specification 要求跨组件公共属性集合保持显式、唯一且可判定，空值与默认值不能依赖隐式猜测；本任务据此把 PlatformInitConfig 保持为最小显式配置对象，并增加一致性检查方法，避免后续工厂初始化阶段吞掉非法输入。

D 结论：

1. Design -> Build 映射：新增 platform/include/linux/PlatformInitConfig.h，内聚 target_platform/profile_name/enable_hal 以及 queue_defaults/io_timeouts 两个子结构；通过 has_consistent_values() 固化最小负例边界。
2. Build 三件套：
   - 代码目标：新增 platform/include/linux/PlatformInitConfig.h。
   - 测试目标：新增 PlatformInitConfigTest，覆盖默认值正例以及空 profile、零容量、负超时负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_init_config_unit_test && ctest --test-dir build-ci -R PlatformInitConfigTest --output-on-failure。
3. D Gate：PASS。

### 12.3 Build 交付与证据

交付物：

1. platform/include/linux/PlatformInitConfig.h：新增 PlatformInitConfig、QueueDefaults、IoTimeouts 以及一致性检查入口。
2. tests/unit/platform/linux/PlatformInitConfigTest.cpp：覆盖默认值正例和非法 profile/queue/timeout 负例。
3. tests/unit/CMakeLists.txt、tests/unit/platform/CMakeLists.txt、tests/unit/platform/linux/CMakeLists.txt：接入 platform unit 测试最小注册路径。
4. docs/todos/deliverables/PLAT-LNX-TODO-001-PlatformInitConfig设计收敛.md：补齐 D 阶段设计收敛与 Design->Build 映射。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_init_config_unit_test：通过，ninja: no work to do.
3. ctest --test-dir build-ci -R PlatformInitConfigTest --output-on-failure：通过，1/1 tests passed。
4. ctest --test-dir build-ci -N -R PlatformInitConfigTest：通过，发现 1 个测试。

Build 合规复核：

1. 代码注释：本轮数据结构与测试命名已自解释，未增加冗余注释。
2. 正负例覆盖：PlatformInitConfigTest 包含 1 组默认值正例和 3 组非法输入负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R PlatformInitConfigTest 验证新增测试可发现；完整平台测试矩阵仍属于 PLAT-LNX-TODO-019 范围。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：本轮提交范围限定为 PlatformInitConfig、对应 unit 验证与证据文档。
6. 环境恢复：CMake Tools 当前未解析出可用 build target，本轮按仓库既有 build-ci 命令链路完成验证。

## 13. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-002）

### 13.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-002。
2. 可执行性依据：仅依赖已完成的 PLAT-LNX-TODO-001，当前没有 blocker，范围收敛于单个数据结构头文件与最小 unit 验证。

### 13.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformCapabilitySet 覆盖 thread、timer、queue、filesystem、network、ipc、hal 七项能力，并为每项记录 enabled、disabled、degraded 与 reason。
2. docs/architecture/platform_linux_detailed_design.md 6.7 明确 CapabilityRegistry 在工厂探测后登记能力状态，上层据此执行降级路径。
3. docs/architecture/platform_linux_detailed_design.md 6.8 明确 HAL 缺失、定时漂移等异常在平台层只表现为 disabled 或 degraded 事实，不承担上层恢复决策。
4. docs/architecture/platform_linux_detailed_design.md 11.1 仍未冻结独立 reason_code 规范，因此本轮只冻结状态三态和 reason 文本承载。

外部参考：

1. Kubernetes Pod Conditions 将 status 与 machine-readable reason 分离，使状态机可以被自动化判断而不必提前冻结完整错误码域；本任务据此采用显式状态枚举加 reason 字段，保证能力表既可测试又不越权扩张到错误模型。

D 结论：

1. Design -> Build 映射：新增 platform/include/linux/LinuxPlatformCapabilities.h，冻结 PlatformCapabilityState、PlatformCapability、PlatformCapabilitySet；通过 has_consistent_values() 固化状态与 reason 的一致性规则。
2. Build 三件套：
   - 代码目标：新增 platform/include/linux/LinuxPlatformCapabilities.h。
   - 测试目标：新增 LinuxPlatformCapabilitiesTest，覆盖默认 disabled/not-probed 正例、mixed enabled/degraded/disabled 正例和 reason 缺失/越界负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_linux_platform_capabilities_unit_test && ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest && ctest --test-dir build-ci -R LinuxPlatformCapabilitiesTest --output-on-failure。
3. D Gate：PASS。

### 13.3 Build 交付与证据

交付物：

1. platform/include/linux/LinuxPlatformCapabilities.h：新增能力三态枚举、单项能力对象和七项能力聚合对象。
2. tests/unit/platform/linux/LinuxPlatformCapabilitiesTest.cpp：覆盖默认值、mixed state 和状态/reason 负例。
3. tests/unit/platform/linux/CMakeLists.txt：接入 LinuxPlatformCapabilitiesTest。
4. docs/todos/deliverables/PLAT-LNX-TODO-002-LinuxPlatformCapabilities设计收敛.md：补齐 D 阶段设计收敛与 Design->Build 映射。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_linux_platform_capabilities_unit_test：通过。
3. ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest：通过，发现 1 个测试。
4. ctest --test-dir build-ci -R LinuxPlatformCapabilitiesTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：本轮数据结构与 helper 命名已自解释，未增加冗余注释。
2. 正负例覆盖：LinuxPlatformCapabilitiesTest 包含默认值正例、mixed state 正例和 3 组状态/reason 负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R LinuxPlatformCapabilitiesTest 验证新增测试可发现，完整平台注册矩阵仍属于 PLAT-LNX-TODO-019 范围。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：本轮提交范围限定为 LinuxPlatformCapabilities、对应 unit 验证与证据文档。
6. 环境恢复：本轮继续使用仓库既有 build-ci 命令链路完成验证，未依赖当前不可用的 CMake Tools target 解析。

## 14. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-003）

### 14.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-003。
2. 可执行性依据：无前置依赖；PLAT-LNX-BLK-04 可通过“冻结最小 category 映射锚点 + 单测”在本轮内最小解阻。

### 14.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 PlatformError 字段为 code/category/retryable_hint/syscall_name/errno_value/detail。
2. docs/architecture/platform_linux_detailed_design.md 6.8 明确平台失败必须可判定，不允许吞错。
3. docs/architecture/platform_linux_detailed_design.md 7 映射表要求新增 PlatformError.h、PlatformResult.h 与 PlatformErrorMappingTest。
4. docs/architecture/platform_linux_detailed_design.md 11.1 指出 ErrorInfo 映射评审未完成，允许先冻结最小码集与映射锚点。

外部参考：

1. OpenTelemetry Common Specification 对可观测字段 machine-readable 稳定性的要求，支持本轮先冻结平台私有错误类别及最小映射锚点，再由后续评审扩展细粒度语义。

D 结论：

1. Design -> Build 映射：新增 platform/include/PlatformError.h、platform/include/PlatformResult.h；通过 map_platform_error_category_to_contracts 固化最小一级失败域映射。
2. Build 三件套：
   - 代码目标：新增 PlatformError/PlatformResult 头文件。
   - 测试目标：新增 PlatformErrorMappingTest，覆盖映射正例、字段一致性负例和 result 互斥负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_error_mapping_unit_test && ctest --test-dir build-ci -N -R PlatformErrorMappingTest && ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure。
3. D Gate：PASS。

### 14.3 Build 交付与证据

交付物：

1. platform/include/PlatformError.h：新增 PlatformErrorCode、PlatformErrorCategory、PlatformError 及最小 category 映射函数。
2. platform/include/PlatformResult.h：新增 PlatformResult 模板，冻结 success/failure 互斥约束。
3. tests/unit/platform/linux/PlatformErrorMappingTest.cpp：覆盖映射与字段/结果一致性正负例。
4. tests/unit/platform/linux/CMakeLists.txt：接入 PlatformErrorMappingTest。
5. docs/todos/deliverables/PLAT-LNX-TODO-003-PlatformError设计收敛.md：补齐 D 阶段设计收敛与 BLK-04 最小解阻证据。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_error_mapping_unit_test：通过，ninja: no work to do.
3. ctest --test-dir build-ci -N -R PlatformErrorMappingTest：通过，发现 1 个测试。
4. ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：类型命名与字段含义自解释，未增加冗余注释。
2. 正负例覆盖：PlatformErrorMappingTest 包含映射正例、字段一致性负例和 result 互斥负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R PlatformErrorMappingTest 验证新增测试可发现，完整平台注册矩阵仍属于 PLAT-LNX-TODO-019 范围。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：本轮提交范围限定为 PlatformError/PlatformResult、对应 unit 验证与证据文档。
6. 环境恢复：前台终端回传异常，本轮通过后台终端执行+输出回读完成验收，不影响结果可追溯性。

## 15. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-004）

### 15.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-004。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成，当前无 blocker，范围收敛于单一接口头文件与最小 unit 验证。

### 15.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 ThreadOptions 字段为 name、stack_size_kb、detach_policy、affinity_hint。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IThread 接口包含 create_thread/join_thread/request_stop。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IThread 错误语义应对齐 InvalidArgument、Timeout、ResourceExhausted、InternalFailure。
4. docs/architecture/platform_linux_detailed_design.md 7 映射表要求通过 InterfaceSurfaceTest 固化接口调用面。

外部参考：

1. C++ Core Guidelines 对抽象接口稳定性的实践建议：接口基类应聚焦最小契约并通过显式返回类型承载错误通道。本轮据此将 IThread 定义为纯抽象接口，并复用 PlatformResult 统一错误语义入口。

D 结论：

1. Design -> Build 映射：新增 platform/include/IThread.h，冻结 ThreadOptions/ThreadHandle 与 IThread 三方法签名。
2. Build 三件套：
   - 代码目标：新增 IThread 接口头文件。
   - 测试目标：新增 InterfaceSurfaceTest，覆盖签名稳定性与 ThreadOptions/ThreadHandle 正负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -N -R InterfaceSurfaceTest && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS。

### 15.3 Build 交付与证据

交付物：

1. platform/include/IThread.h：新增 ThreadDetachPolicy、ThreadOptions、ThreadHandle、ThreadJoinResult、IThread 接口定义。
2. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：新增 IThread surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。
3. tests/unit/platform/linux/CMakeLists.txt：接入 InterfaceSurfaceTest。
4. docs/todos/deliverables/PLAT-LNX-TODO-004-IThread设计收敛.md：补齐 D 阶段设计收敛与 Design->Build 映射。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -N -R InterfaceSurfaceTest：通过，发现 1 个测试。
4. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 包含 ThreadOptions/ThreadHandle 默认值正例与非法输入负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 验证新增测试可发现。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：本轮提交范围限定为 IThread 接口、对应 unit 验证与证据文档。
6. 环境恢复：沿用仓库 build-ci 命令链路完成验证，无额外环境阻塞。

## 16. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-005）

### 16.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-005。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成，当前无 blocker，范围收敛于单一接口头文件与最小 unit 验证。

### 16.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 TimerSpec 字段为 mode、interval_ms、initial_delay_ms、clock_kind。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 ITimer 接口包含 start_once/start_periodic/cancel。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 ITimer 错误语义应对齐 InvalidArgument、Timeout、Cancelled、InternalFailure。
4. docs/architecture/platform_linux_detailed_design.md 6.2/6.5 明确 PosixTimerProvider 输出 TimerHandle、DriftStats，周期误差只作为平台事实暴露。

外部参考：

1. libuv timer 文档将 timer 分为 single-shot 与 repeating 两类，并允许 timeout=0 在下一次事件循环触发；本轮据此保留 start_once/start_periodic 双入口，并允许 initial_delay_ms=0 的立即触发语义。
2. Linux timerfd 文档明确 interval=0 为一次性、非零 interval 为周期性，同时时钟源必须显式选择；本轮据此在 TimerSpec 中冻结 mode、interval_ms、clock_kind，并把漂移事实保留给 provider 输出对象。

D 结论：

1. Design -> Build 映射：新增 platform/include/ITimer.h，冻结 TimerSpec/TimerHandle/TimerDriftStats/TimerCancelResult 与 ITimer 三方法签名。
2. Build 三件套：
   - 代码目标：新增 ITimer 接口头文件。
   - 测试目标：扩展 InterfaceSurfaceTest，覆盖签名稳定性、TimerSpec 默认值正例、周期零 interval 与非法 drift 负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -N -R InterfaceSurfaceTest && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS。

### 16.3 Build 交付与证据

交付物：

1. platform/include/ITimer.h：新增 TimerMode、TimerClockKind、TimerSpec、TimerHandle、TimerDriftStats、TimerCancelResult、ITimer 接口定义。
2. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：扩展 ITimer surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。
3. docs/todos/deliverables/PLAT-LNX-TODO-005-ITimer设计收敛.md：补齐 D 阶段设计收敛与 Design->Build 映射。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -N -R InterfaceSurfaceTest：通过，发现 1 个测试。
4. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 增补 TimerSpec 默认值正例，以及周期零 interval、非法 drift、零句柄负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 复核新增测试可发现。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：预期提交范围限定为 ITimer 接口、对应 unit 验证与证据文档。
6. 环境恢复：沿用仓库 build-ci 命令链路执行验证。

## 20. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-009）

### 20.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-009。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成，当前无 blocker，范围收敛于单一接口头文件、PlatformErrorCode 扩展与最小 unit 验证。

### 20.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IIPC 接口包含 listen/accept/connect/send/receive/close。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IIPC 错误语义应对齐 AddressInUse、PathTooLong（可由 InvalidArgument 承接）、PeerClosed、PayloadTooLarge。
3. docs/architecture/platform_linux_detailed_design.md 6.9 明确 platform.linux.ipc.socket_dir 默认 /tmp/dasall、max_payload_bytes 默认 1048576；本轮据此设置 ListenOptions 默认值。
4. TODO-009 完成判定：AddressInUse/PeerClosed/PayloadTooLarge 路径可判定。PeerClosed 和 PayloadTooLarge 在 PlatformErrorCode 中缺失，需补充以确保错误路径可判定。

外部参考：

1. Linux Unix Domain Socket 文档确认 bind(2) 在路径已存在时返回 EADDRINUSE，对应 AddressInUse；send(2) 在消息超过 SO_SNDBUF 时返回 EMSGSIZE，对应 PayloadTooLarge；read(2) 返回 0 表示 peer 关闭，对应 PeerClosed。
2. POSIX listen(2) 文档明确 backlog 参数的最小有效值为 1；本轮据此在 ListenOptions.has_consistent_values() 中拒绝零 backlog。

D 结论：

1. Design -> Build 映射：
   - 扩展 platform/include/PlatformError.h：补充 PeerClosed 与 PayloadTooLarge 错误码。
   - 新增 platform/include/IIPC.h：冻结 IpcEndpoint、ListenOptions、IpcListenerHandle、IpcChannelHandle、IpcPayload、IpcSendResult、IpcReceiveResult 与 IIPC 六方法签名。
2. Build 三件套：
   - 代码目标：新增 IIPC 接口头文件 + 补充 PlatformErrorCode 两个 IPC 错误码。
   - 测试目标：扩展 InterfaceSurfaceTest，覆盖签名稳定性、IpcEndpoint/ListenOptions 默认值正例与非法输入负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS。

### 20.3 Build 交付与证据

交付物：

1. platform/include/PlatformError.h：补充 PeerClosed、PayloadTooLarge 至 PlatformErrorCode 枚举。
2. platform/include/IIPC.h：新增 IpcEndpoint、ListenOptions、IpcListenerHandle、IpcChannelHandle、IpcPayload、IpcSendResult、IpcReceiveResult、IIPC 接口定义。
3. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：扩展 IIPC surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。
4. ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure：通过，1/1 tests passed（既有测试不受 ErrorCode 扩展影响）。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 增补 IpcEndpoint/ListenOptions 默认值正例，以及 empty-socket-path、zero-backlog、zero-max-payload 负例，以及 peer-closed-with-data 不一致负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 复核新增测试可发现。
4. TODO 证据回写：已回写主任务状态与本节执行记录。
5. 提交隔离：本轮提交范围限定为 IIPC 接口、PlatformErrorCode 扩展、对应 unit 验证扩展与 TODO 回写。
6. 环境恢复：沿用仓库 build-ci 命令链路执行验证。

## 19. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-008）

### 19.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-008。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成；PLAT-LNX-BLK-03（epoll/poll fallback）属于 provider 实现层面，不阻塞接口签名冻结；按最小解阻原则先冻结接口，provider 实现保持 Blocked。

### 19.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.6 明确 INetwork 接口包含 connect/send/receive/shutdown。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 INetwork 错误语义应对齐 Timeout、Disconnected、ConnectionRefused、WouldBlock（已在 PlatformErrorCode 枚举中覆盖）。
3. docs/architecture/platform_linux_detailed_design.md 6.5 明确 SocketEndpoint 字段为 host/path、port、transport、abstract_namespace；本轮面向 Linux 通用网络场景，冻结 host/port/transport 三字段。
4. docs/architecture/platform_linux_detailed_design.md 6.9 明确 platform.linux.net.connect_timeout_ms 默认 3000、io_timeout_ms 默认 5000；本轮据此设置 ConnectOptions 默认值。

外部参考：

1. POSIX connect(2)/send(2)/recv(2)/shutdown(2) 文档确认四方法语义稳定；本轮接口签名与 POSIX 原语一一对应，避免过度抽象。
2. PLAT-LNX-BLK-03 的 epoll->poll fallback 仅影响 LinuxNetworkProvider 内部策略，不影响调用方可见接口；因此接口可在 blocker 未解阻时先行冻结。

D 结论：

1. Design -> Build 映射：新增 platform/include/INetwork.h，冻结 NetworkTransport、SocketEndpoint、ConnectOptions、ConnectionHandle、NetworkBuffer、NetworkSendResult、NetworkReceiveResult 与 INetwork 四方法签名。
2. Build 三件套：
   - 代码目标：新增 INetwork 接口头文件。
   - 测试目标：扩展 InterfaceSurfaceTest，覆盖签名稳定性、SocketEndpoint/ConnectOptions 默认值正例与非法输入负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS（接口层解阻，provider 实现仍 Blocked 于 BLK-03）。

### 19.3 Build 交付与证据

交付物：

1. platform/include/INetwork.h：新增 NetworkTransport、SocketEndpoint、ConnectOptions、ConnectionHandle、NetworkBuffer、NetworkSendResult、NetworkReceiveResult、INetwork 接口定义。
2. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：扩展 INetwork surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 增补 ConnectOptions 默认值正例，以及 empty-host、zero-port 负例，以及 peer-closed-with-data 不一致负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 复核新增测试可发现。
4. TODO 证据回写：已回写主任务状态（并注明 BLK-03 影响范围仅限 provider 实现）与本节执行记录。
5. 提交隔离：本轮提交范围限定为 INetwork 接口、对应 unit 验证扩展与 TODO 回写。
6. 环境恢复：沿用仓库 build-ci 命令链路执行验证。

## 17. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-006）

### 17.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-006。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成，当前无 blocker，范围收敛于单一接口头文件与最小 unit 验证。

### 17.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.5 明确 QueueOptions 字段为 capacity、overflow_policy、shutdown_policy。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IQueue 接口包含 create_queue/push/pop/close。
3. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IQueue 错误语义应对齐 Timeout、QueueClosed、ResourceExhausted。
4. docs/architecture/platform_linux_detailed_design.md 6.2/6.5 明确 BlockingQueueProvider 输出 QueueHandle、QueueStats，且超时/关闭/容量耗尽需可判定。

外部参考：

1. Linux POSIX message queue 文档将 queue descriptor 生命周期与 send/receive/close 分离，本轮据此保留 QueueHandle + create/push/pop/close 的接口面。
2. Linux POSIX message queue 文档强调队列容量受系统资源上限约束，本轮据此固定 QueueOptions.capacity 与 overflow_policy，并保持 ResourceExhausted 错误语义入口。

D 结论：

1. Design -> Build 映射：新增 platform/include/IQueue.h，冻结 QueueOptions/QueueHandle/QueuePushResult/QueuePopResult/QueueCloseResult 与 IQueue 四方法签名。
2. Build 三件套：
   - 代码目标：新增 IQueue 接口头文件。
   - 测试目标：扩展 InterfaceSurfaceTest，覆盖签名稳定性、QueueOptions 默认值正例、零容量与不一致 pop 结果负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -N -R InterfaceSurfaceTest && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS。

### 17.3 Build 交付与证据

交付物：

1. platform/include/IQueue.h：新增 QueueOverflowPolicy、QueueShutdownPolicy、QueueOptions、QueueHandle、QueuePushResult、QueuePopResult、QueueCloseResult、IQueue 接口定义。
2. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：扩展 IQueue surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。
3. docs/todos/deliverables/PLAT-LNX-TODO-006-IQueue设计收敛.md：补齐 D 阶段设计收敛与 Design->Build 映射。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -N -R InterfaceSurfaceTest：通过，发现 1 个测试。
4. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 增补 QueueOptions 默认值正例，以及零容量、不一致 pop 结果、零句柄负例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 复核新增测试可发现。
4. TODO 证据回写：已回写主任务状态、本节执行记录和 D 阶段交付文档路径。
5. 提交隔离：预期提交范围限定为 IQueue 接口、对应 unit 验证与证据文档。
6. 环境恢复：沿用仓库 build-ci 命令链路执行验证。

## 18. 本轮执行记录（2026-03-27 / PLAT-LNX-TODO-007）

### 18.1 选中任务

1. 本轮任务：PLAT-LNX-TODO-007。
2. 可执行性依据：前置依赖 PLAT-LNX-TODO-003 已完成，当前无 blocker，范围收敛于单一接口头文件与最小 unit 验证。

### 18.2 研究与 Design 结论

本地证据：

1. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IFileSystem 接口包含 read_file/write_atomic/ensure_directory/stat。
2. docs/architecture/platform_linux_detailed_design.md 6.6 明确 IFileSystem 错误语义应对齐 NotFound、PermissionDenied、NoSpace、IOFailure（已在 PlatformErrorCode 枚举中覆盖）。
3. docs/architecture/platform_linux_detailed_design.md 6.2/6.3 明确 LinuxFileSystemProvider 输入为 FileOpenOptions、PathSpec、Buffer，输出为 FileResult、FileMetadata，并要求原子写入、权限失败、空间不足等事实可判定。
4. docs/architecture/platform_linux_detailed_design.md 6.9 明确 platform.linux.fs.atomic_write_tmp_suffix 默认为 ".tmp"，platform.linux.fs.root_prefix 为 /var/lib/dasall。

外部参考：

1. POSIX rename(2) 文档明确在同一文件系统内 rename 是原子操作；本轮据此把临时文件后缀固化到 FileWriteOptions.tmp_suffix，以支撑 LinuxFileSystemProvider 的原子写逻辑。
2. POSIX stat(2) 文档明确 struct stat 各字段语义；本轮据此冻结 FileStatResult 的 exists/is_regular_file/is_directory/size_bytes/last_modified_ms 字段，并固化相互排斥约束。

D 结论：

1. Design -> Build 映射：新增 platform/include/IFileSystem.h，冻结 FileWriteMode、FileWriteOptions、FileStatResult、FileBuffer 与 IFileSystem 四方法签名。
2. Build 三件套：
   - 代码目标：新增 IFileSystem 接口头文件。
   - 测试目标：扩展 InterfaceSurfaceTest，覆盖签名稳定性、FileWriteOptions 默认值正例、空 tmp_suffix 与 FileStatResult 各约束违反负例。
   - 验收命令：cmake -S . -B build-ci -G Ninja && cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure。
3. D Gate：PASS。

### 18.3 Build 交付与证据

交付物：

1. platform/include/IFileSystem.h：新增 FileWriteMode、FileWriteOptions、FileStatResult、FileBuffer、IFileSystem 接口定义。
2. tests/unit/platform/linux/InterfaceSurfaceTest.cpp：扩展 IFileSystem surface 测试，覆盖默认值正例、一致性负例与方法签名静态断言。

验收结果：

1. cmake -S . -B build-ci -G Ninja：通过。
2. cmake --build build-ci --target dasall_platform dasall_platform_interface_surface_unit_test：通过。
3. ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure：通过，1/1 tests passed。

Build 合规复核：

1. 代码注释：接口与对象命名自解释，未增加冗余注释。
2. 正负例覆盖：InterfaceSurfaceTest 增补 FileWriteOptions 默认值正例，以及空 tmp_suffix、nonexistent-with-type、both-types、nonexistent-with-size 负例，以及有效文件与目录正例。
3. 测试发现性：已通过 ctest --test-dir build-ci -N -R InterfaceSurfaceTest 复核新增测试可发现。
4. TODO 证据回写：已回写主任务状态与本节执行记录。
5. 提交隔离：本轮提交范围限定为 IFileSystem 接口、对应 unit 验证扩展与 TODO 回写。
6. 环境恢复：沿用仓库 build-ci 命令链路执行验证。
