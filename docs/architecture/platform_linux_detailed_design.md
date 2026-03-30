# DASALL platform/linux 模块详细设计（Detailed Design）

## 1. 模块概览

### 1.1 模块定位

platform/linux 属于 Platform Abstraction Layer（Layer 2），负责提供 Linux 通用平台能力实现，覆盖线程、队列、定时器、文件系统、网络与 IPC 基础抽象，并为 ARM 平台下的 HAL 子域提供受控接入点。

- 设计对象：platform/linux 子模块
- 设计阶段：Detailed Design
- 目标状态：在 x86 桌面 Linux 与 ARM Embedded Linux 上复用同一套 Linux 通用实现，通过 Profile 和适配器收敛平台差异，不分叉 Agent 主流程

### 1.2 边界与依赖方向

| 维度 | 说明 |
|---|---|
| 上游调用方 | infra、services，以及经由 services/infra 间接消费平台能力的其他模块 |
| 同模块协同 | platform/src/arm/hal、platform/src/x86 作为特化子域，受 platform/linux 提供的能力探测与工厂接入点约束 |
| 下游依赖 | Linux/POSIX 系统调用、标准库并发原语、可选第三方轻量适配库 |
| 严格边界 | platform 不依赖 runtime、cognition、tools、memory、knowledge、multi_agent 等上层业务模块 |
| contracts 策略 | platform 接口和实现细节不进入 contracts，共享 contracts 对象只作为上层语义锚点被消费，不被反向污染 |

### 1.3 来源依据

1. 架构文档：docs/architecture/DASSALL_Agent_architecture.md（3.4.6、3.7、7.4、7.5、9.2）
2. 工程蓝图：docs/architecture/DASALL_Engineering_Blueprint.md（3.11、4.2、5、6、7）
3. ADR：docs/adr/ADR-005-architecture-review-baseline.md、ADR-006、ADR-007、ADR-008
4. contracts 计划/TODO：docs/plans/DASALL_contracts冻结实施计划.md、docs/todos/contracts/DASALL_contracts冻结TODO总表.md
5. 工程规范：docs/development/DASALL_工程协作与编码规范.md
6. 代码现状：platform/CMakeLists.txt、platform/src/placeholder.cpp、platform/src/linux/（空）、platform/src/x86/（空）、platform/src/arm/hal/（空）
7. 行业参考方向：POSIX/pthread + epoll/eventfd 的能力切片模式、libuv/Boost.Asio 的事件循环统一模式、嵌入式 Linux 平台 HAL 分层模式（仅作为候选方案参考，不作为硬约束来源）

---

## 2. 约束清单

### 2.1 Must / Should / Must-Not

| Constraint ID | 来源文档 | 类型 | 约束描述 | 影响范围 |
|---|---|---|---|---|
| PLAT-LNX-C001 | 架构 3.4.6 | Must | platform/linux 必须抽象线程、锁、队列、定时器、文件、网络、IPC，并隔离 x86 Linux 与 ARM Embedded Linux 差异 | 子组件/接口 |
| PLAT-LNX-C002 | 工程蓝图 3.11 | Must | Linux 通用实现应优先落在 platform/src/linux，x86 与 ARM 特化分别收敛到 src/x86 与 src/arm/hal | 目录/依赖 |
| PLAT-LNX-C003 | 架构 3.7、蓝图 4.2 | Must-Not | platform 不得反向依赖任何上层业务模块 | 依赖关系 |
| PLAT-LNX-C004 | 蓝图 3.11、5.1 | Must | HAL 能力只在 edge_balanced、edge_minimal、factory_test 等 Profile 下启用；desktop_full/cloud_full 下可关闭 | 配置/能力裁剪 |
| PLAT-LNX-C005 | 架构 7.5、蓝图 5.1/5.2 | Must | Profile 只能裁剪能力和替换实现，不能绕过 Runtime 主控、Policy Gate 与 Audit 主链路 | 配置/发布 |
| PLAT-LNX-C006 | 架构 7.5、蓝图 5.1 | Must | 新平台接入优先补 Platform Adapter 和 Profile，而不是分叉新的 Agent 主流程 | 演进/实现策略 |
| PLAT-LNX-C007 | ADR-005 | Must | 在 contracts 冻结基线下推进平台设计，不以 platform/linux 设计反向改写跨模块共享语义 | 核心对象/接口 |
| PLAT-LNX-C008 | contracts 计划 5/6 章 | Must-Not | 不把 Linux FD、epoll、pthread、socket、HAL 驱动句柄等实现细节写入 contracts 共享语义对象 | contracts 对齐 |
| PLAT-LNX-C009 | contracts InterfaceCatalog | Must-Not | platform 接口不纳入 contracts stage-5 稳定共享接口目录，应保留在 platform 模块内部边界 | 接口治理 |
| PLAT-LNX-C010 | ADR-006 | Must-Not | platform/linux 不承担语义上下文组装或 Prompt 渲染职责，只处理 OS/硬件能力与资源访问 | 职责边界 |
| PLAT-LNX-C011 | ADR-007 | Must-Not | platform/linux 不承担恢复决策与重试裁定职责，只返回可判定平台错误并暴露恢复所需事实 | 错误语义/流程 |
| PLAT-LNX-C012 | ADR-008 | Must-Not | platform/linux 不接管全局任务生命周期或多 Agent 编排，只作为受控能力提供方 | 职责边界 |
| PLAT-LNX-C013 | 工程规范 3.4/3.6 | Must | 模块边界公共接口必须返回可判定成功/失败结果，不依赖隐式异常表达业务失败 | 核心接口 |
| PLAT-LNX-C014 | 工程规范 3.2/4.2 | Must | 对外头文件放在 platform/include 下，文件名使用 PascalCase，头文件使用 #pragma once | 目录/命名 |
| PLAT-LNX-C015 | 工程规范 3.7、蓝图 7 | Must | 新增公共平台接口必须同步补 unit 或 integration 测试，并支持 profile 兼容性验证 | 测试 |
| PLAT-LNX-C016 | 架构 9.2 | Must | platform/linux 初始化必须在 Profile Bind / Strategy Inject 之后、Service Init 之前完成 | 启动流程 |
| PLAT-LNX-C017 | 蓝图 4.2 | Must-Not | cognition 不得直接依赖 platform；tools 也不应绕过 services 直接耦合平台细节 | 相邻模块依赖方向 |
| PLAT-LNX-C018 | 蓝图 6、7 | Should | 平台接口应按 IThread / ITimer / IQueue / IFileSystem / INetwork / IIPC 与 linux 实现类分层组织 | 子组件/目录 |
| PLAT-LNX-C019 | 行业参考补充 | Should | Linux 通用层优先采用能力切片 + 工厂注册，而不是大一统 God Object 或全局事件循环侵入式设计 | 组件结构 |
| PLAT-LNX-C020 | 行业参考补充 | Should | 失败恢复优先使用显式错误码、超时与降级探测，不在平台层隐式无限重试 | 异常恢复 |

### 2.2 约束抽取结论

- Must：Linux 通用能力必须可复用、可裁剪、可测试，并在启动链路上位于 Profile 注入之后。
- Should：接口按能力切片，Linux 通用实现与 ARM HAL 特化分层，优先通过工厂与能力表做装配。
- Must-Not：不得侵入上层主控/认知职责，不得把实现细节回写到 contracts，不得通过平台分叉主流程。

---

## 3. 现状与缺口

### 3.1 现状判定

- platform 模块当前仅有静态库占位：platform/CMakeLists.txt 只编译 platform/src/placeholder.cpp。
- platform/src/linux、platform/src/x86、platform/src/arm/hal 目录均为空，尚未形成 Linux 通用层或特化层实现。
- platform/include 下未落地工程蓝图中约定的 public headers，当前仅见空的 hal 目录占位。
- contracts 已完成较大范围冻结，但 InterfaceCatalog 明确排除了 platform 接口，说明平台边界应在本模块内收敛，而非外溢到 contracts。

### 3.2 现状-目标差距表

| 设计目标 | 当前状态 | 差距描述 | 风险等级 | 修复优先级 |
|---|---|---|---|---|
| 提供 IThread / ITimer / IQueue 接口与 Linux 实现 | 缺失 | 无基础并发原语，上层后续实现将被迫各自封装线程与超时 | High | P0 |
| 提供 IFileSystem / INetwork / IIPC 抽象与 Linux 适配 | 缺失 | 文件、网络、Unix Socket/Pipe 能力没有统一入口 | High | P0 |
| 形成 Linux 通用层与 x86/arm 特化分层 | 缺失 | 目录存在但无实现，未来极易把 profile 差异散落到业务层 | High | P0 |
| 形成平台错误模型与上层映射约定 | 缺失 | 上层无法稳定把平台失败映射到 ErrorInfo/Observation | High | P0 |
| 形成平台初始化与能力探测流程 | 缺失 | 启动顺序、HAL 可用性和降级路径无法验证 | High | P0 |
| 形成可观测与健康快照接口 | 缺失 | 发生队列拥塞、socket 断开、权限失败时不可诊断 | Medium | P1 |
| 形成 Profile 对齐配置模型 | 缺失 | 无法体现 desktop_full 与 edge_balanced 的差异能力集 | Medium | P1 |
| 形成 unit/integration/failure injection 基线 | 缺失 | 模块难以在后续接入前建立回归门禁 | High | P0 |

### 3.3 风险冲突识别

1. 边界冲突风险：若把日志、审计、恢复决策直接做进 platform/linux，会违反 platform 与 infra/runtime 的职责边界。
2. 语义重复风险：若在平台层重新定义 ErrorInfo、Observation 或跨模块请求对象，会与 contracts 已冻结对象重复。
3. 依赖反转风险：若 runtime、tools、cognition 直接 include Linux 具体实现类，将破坏蓝图中的依赖禁止规则。
4. 实现失控风险：若采用单体式 LinuxPlatformManager，把线程、文件、网络、IPC、HAL 和事件循环全部揉在一个对象中，会快速演化成 God Object。

---

## 4. 候选方案对比

### 4.1 候选方案描述

- 方案 A：单体式 LinuxPlatformManager
- 方案 B：能力切片适配器 + LinuxPlatformFactory + CapabilityRegistry
- 方案 C：统一事件循环驱动平台层（event-loop-first）

### 4.2 候选方案对比矩阵

| 方案名 | 架构匹配度 | ADR匹配度 | 工程复杂度 | 风险 | 结论 |
|---|---|---|---|---|---|
| 方案 A 单体式 LinuxPlatformManager | 中 | 中 | 低 | 快速形成 God Object，难以按 Profile 裁剪，测试粒度粗 | 淘汰：不利于长期演进与模块边界收敛 |
| 方案 B 能力切片适配器 + 工厂注册 | 高 | 高 | 中 | 初期文件数较多，需要明确工厂与能力表边界 | 保留并采纳：最符合蓝图目录、Profile 与可测试性要求 |
| 方案 C 统一事件循环驱动平台层 | 中 | 中 | 高 | 容易把 runtime 调度语义带入平台层，并引入额外第三方依赖与线程模型耦合 | 淘汰：当前阶段复杂度过高，且有职责穿透风险 |

### 4.3 候选方案补充说明

1. 方案 A 的优势是初期上手快，但会把文件、网络、队列、计时器与 HAL 能力耦在一个对象里，后续任何 profile 差异都需要修改同一对象，违背单一职责与可裁剪性。
2. 方案 B 按蓝图接口分类切出独立 provider，并用 LinuxPlatformFactory 统一装配，既符合 src/linux、src/x86、src/arm/hal 的目录意图，也便于做 unit/failure injection。
3. 方案 C 适合高吞吐 I/O 系统，但 DASALL 当前平台层只是 OS/硬件差异屏蔽层，不应提前承担统一事件循环和全局调度职责。

---

## 5. 决策结论

### 5.1 最终选型

选择方案 B：能力切片适配器 + LinuxPlatformFactory + CapabilityRegistry。

### 5.2 选型依据

1. 与架构一致：满足平台层“抽象 OS/线程/文件/网络/IPC/硬件接口”的职责定义，并保持 platform 不反向依赖业务模块。
2. 与工程蓝图一致：天然映射 IThread、ITimer、IQueue、IFileSystem、INetwork、IIPC 这组接口分类，以及 src/linux、src/x86、src/arm/hal 的目录规划。
3. 与 ADR 一致：不改写 ADR-006/007/008 中对上下文、恢复和主控边界的裁定，平台仅提供受控能力面。
4. 与 contracts 冻结一致：platform 接口不进入 contracts；平台错误和能力探测结果只作为上层可消费事实，不改写共享语义对象。

### 5.3 放弃其他方案原因

1. 放弃方案 A：虽然起步快，但无法支撑 Profile 裁剪、可测试性和后续 x86/ARM 分层演进。
2. 放弃方案 C：会把事件循环与调度控制提前压入平台层，复杂度高且存在职责越界。

---

## 6. 详细设计

### 6.1 职责边界

platform/linux 负责：

1. 提供 Linux 通用平台接口实现，包括并发、定时、文件、网络和 IPC。
2. 在初始化阶段完成内核能力探测、资源约束校验和可选 HAL 可用性判断。
3. 通过能力表和工厂输出稳定平台实例，供上层以抽象接口消费。
4. 将 Linux/POSIX 失败转换为可判定的平台错误与恢复事实。

platform/linux 不负责：

1. 不负责 Runtime 主状态机推进、重试预算裁定、replan 或 degrade 业务决策。
2. 不负责 infra 的日志持久化、指标导出、审计存储；只暴露可观测钩子和快照事实。
3. 不负责上层 services 语义建模，不把 Linux FD、socket、epoll、驱动句柄暴露为 contracts 共享字段。
4. 不直接实现 ARM HAL 驱动细节，只提供受控接入点与不可用时的 NotSupported 语义。

### 6.2 子组件清单

| 子组件 | 职责 |
|---|---|
| LinuxPlatformFactory | 读取已合并的 Profile/部署配置，探测能力并装配 Linux 平台实例 |
| CapabilityRegistry | 记录当前平台启用的并发、文件、网络、IPC、HAL 能力与降级状态 |
| PosixThreadProvider | 提供线程创建、命名、join、取消协作与线程本地上下文桥接能力 |
| PosixTimerProvider | 提供一次性/周期定时器与取消语义，封装 clock 与等待机制 |
| BlockingQueueProvider | 提供有界/无界阻塞队列与超时语义，作为平台内部和上层异步缓冲基元 |
| LinuxFileSystemProvider | 封装路径归一化、原子写入、目录创建、权限检查与文件锁 |
| LinuxNetworkProvider | 封装 TCP/UDP 客户端连接、超时、重连前置探测与 I/O 错误分类 |
| UnixIpcProvider | 封装 Unix Domain Socket / Pipe 的创建、监听、收发和断连检测 |
| HalAvailabilityBridge | 在需要 HAL 的 Profile 下桥接 src/arm/hal 子域；在非 HAL Profile 下返回 NotSupported |
| PlatformHealthCollector | 暴露队列水位、定时器漂移、FD 使用量、IPC/网络错误计数等快照事实 |

### 6.3 子组件输入/输出

| 子组件 | 输入来源 | 输出去向 | 语义契约 |
|---|---|---|---|
| LinuxPlatformFactory | Boot 阶段注入的已合并配置、target_platform、profile 名称 | LinuxPlatformBundle、CapabilityRegistry | 失败时返回 PlatformError，不隐式降级为随机默认行为 |
| CapabilityRegistry | 工厂探测结果、HAL 可用性、特性开关 | 上层查询、诊断输出、测试断言 | 记录 enabled/disabled/degraded 状态及原因 |
| PosixThreadProvider | ThreadOptions、回调、deadline/timeout 基元 | ThreadHandle、PlatformError | 只返回平台控制结果，不表达业务重试语义 |
| PosixTimerProvider | TimerSpec、回调、取消请求 | TimerHandle、DriftStats | 周期漂移作为事实暴露，不在平台层做恢复裁定 |
| BlockingQueueProvider | QueueOptions、push/pop 请求 | QueueHandle、QueueStats | 超时、关闭、容量耗尽需可判定 |
| LinuxFileSystemProvider | FileOpenOptions、PathSpec、Buffer | FileResult、FileMetadata | 输出原子写入、权限失败、空间不足等事实 |
| LinuxNetworkProvider | SocketEndpoint、ConnectOptions、I/O 请求 | ConnectionHandle、NetworkStats | 连接失败、超时、断连、重置要能分类 |
| UnixIpcProvider | IpcEndpoint、ListenOptions、Payload | IpcChannel、IpcStats | 路径冲突、peer disconnect、payload 超限可判定 |
| HalAvailabilityBridge | capability.require_hal、target_platform | OptionalHalProvider、NotSupported | desktop/cloud profile 默认关闭 HAL 且不视为故障 |
| PlatformHealthCollector | 子 provider 运行统计 | PlatformHealthSnapshot、PlatformEvent | 只暴露事实和阈值状态，不负责写日志/发告警 |

### 6.4 子组件依赖关系

- LinuxPlatformFactory -> CapabilityRegistry
- LinuxPlatformFactory -> PosixThreadProvider / PosixTimerProvider / BlockingQueueProvider / LinuxFileSystemProvider / LinuxNetworkProvider / UnixIpcProvider
- LinuxPlatformFactory -> HalAvailabilityBridge
- 各 provider -> PlatformHealthCollector（汇聚统计与快照）
- HalAvailabilityBridge -> platform/src/arm/hal 具体实现或空实现桩

依赖约束：

1. linux 通用层不得依赖 runtime、infra、services 具体实现。
2. x86 与 arm/hal 特化只能通过 linux 工厂与能力注册表接入，不直接向上暴露新的平行接口面。
3. 任何上层若需观测平台状态，只能通过平台暴露的快照/事件接口消费，不得 include Linux 私有实现。

### 6.5 核心对象与 contracts 对齐关系

| 核心对象 | 关键字段 | 约束 | 与 contracts 的对齐方式 |
|---|---|---|---|
| PlatformInitConfig | target_platform, profile_name, enable_hal, queue_defaults, io_timeouts | 由 Boot 阶段注入，platform 不自行加载配置文件 | 仅消费 profile/runtime budget 派生出的原始配置值，不进入 contracts |
| PlatformCapabilitySet | thread, timer, queue, filesystem, network, ipc, hal 状态 | 每项能力记录 enabled/disabled/degraded + reason | 上层可据此决定降级路径，但能力表本身不入 contracts |
| PlatformError | code, category, retryable_hint, syscall_name, errno_value, detail | 只表达平台事实，不表达业务失败归因 | 上层将其映射到 ErrorInfo/Observation；platform 不直接产出 contracts ErrorInfo |
| ThreadOptions | name, stack_size_kb, detach_policy, affinity_hint | affinity_hint 为建议值，失败不默认致命 | 与 contracts 无直接对齐 |
| TimerSpec | mode, interval_ms, initial_delay_ms, clock_kind | 周期误差通过 stats 暴露 | 与 Checkpoint/RuntimeBudget 仅通过 timeout 数值间接对齐 |
| QueueOptions | capacity, overflow_policy, shutdown_policy | overflow_policy 仅是平台内部策略，不升格为 contracts 字段；选择规则遵循 docs/development/InfraConcurrencyPolicy.md | 与上层 budget 仅通过容量/超时值消费 |
| SocketEndpoint | host/path, port, transport, abstract_namespace | 统一描述网络/IPC 端点 | 与 services 请求对象分离，不进入共享 contracts |
| PlatformHealthSnapshot | queue_depths, timer_drift_ms, fd_in_use, io_failures, hal_state | 供 infra/diagnostics 或 tests 消费 | 作为诊断事实，不作为 contracts 共享语义 |

### 6.6 核心接口语义定义

1. IThread
   - create_thread(options, entry): 创建线程并返回 ThreadHandle
   - join_thread(handle, timeout_ms): 等待结束
   - request_stop(handle): 协作式停止请求
   - 错误语义：InvalidArgument、Timeout、ResourceExhausted、InternalFailure

2. ITimer
   - start_once(spec, callback): 启动一次性定时器
   - start_periodic(spec, callback): 启动周期定时器
   - cancel(timer_id): 取消定时器
   - 错误语义：InvalidArgument、Timeout、Cancelled、InternalFailure

3. IQueue
   - create_queue(options)
   - push(queue_id, item, timeout_ms)
   - pop(queue_id, timeout_ms)
   - close(queue_id)
   - 错误语义：Timeout、QueueClosed、ResourceExhausted

4. IFileSystem
   - read_file(path, deadline_ms)
   - write_atomic(path, bytes, options)
   - ensure_directory(path)
   - stat(path)
   - 错误语义：NotFound、PermissionDenied、NoSpace、IOFailure

5. INetwork
   - connect(endpoint, options)
   - send(handle, buffer, deadline_ms)
   - receive(handle, deadline_ms)
   - shutdown(handle)
   - 错误语义：Timeout、Disconnected、ConnectionRefused、WouldBlock

6. IIPC
   - listen(endpoint, options)
   - accept(listener, deadline_ms)
   - connect(endpoint, options)
   - send/receive/close
   - 错误语义：AddressInUse、PathTooLong、PeerClosed、PayloadTooLarge

#### 6.6.1 HAL 最小探测接口冻结（2026-03-27）

本轮仅冻结 availability probe，不冻结 GPIO/UART/I2C/SPI/CAN 等真实驱动接口。

最小接口清单：

1. HalAvailabilityBridge::probe_hal_availability(config):
   - 输入：PlatformInitConfig（含 target_platform、profile_name、enable_hal 及相关默认超时/队列基线）
   - 输出：HalProbeResult
   - 语义：
     - desktop_full/cloud_full 且 enable_hal=false：返回 DisabledByProfile（非故障）
     - edge_* 或 require_hal=true 且仅 Stub 可用：返回 DegradedStubOnly（可判定降级）
     - require_hal=true 且探测失败：返回 RequiredButUnavailable（初始化失败）

2. HalProbeResult（platform/include/hal/HalProbe.h）
   - 冻结字段：availability_state、reason_code、detail
   - 冻结边界：仅表达可用性事实，不承载真实驱动句柄或设备寄存器语义

3. HalStub（platform/src/arm/hal/HalStub.cpp）
   - 角色：在真实驱动缺失时提供可判定 fallback 目标
   - 约束：仅用于可用性探测与降级语义，不对外宣称真实外设控制能力

回链：PLAT-LNX-TODO-018、PLAT-LNX-TODO-023。

7. ILinuxPlatformBundleFactory（模块内部或受限对外）
   - create(config): 返回聚合后的 LinuxPlatformBundle
   - 语义：单次启动时构造能力集合；不在运行中隐式重建全部 provider

接口前置条件：

1. 所有 provider 必须经 LinuxPlatformFactory 成功创建并注册后才能被上层消费。
2. 所有 timeout/deadline 必须由调用方显式传入或使用平台默认值，禁止隐式无限等待。

接口后置条件：

1. 所有副作用操作必须返回可判定结果；失败时至少提供 PlatformError.code 与 detail。
2. 所有 provider 必须更新对应的 health/stats 计数，便于诊断与测试断言。

### 6.7 主流程时序

#### 6.7.1 平台初始化主流程

1. Boot 阶段完成 Config Load 和 Profile Bind。
2. apps/bootstrap 将已合并的 PlatformInitConfig 传入 LinuxPlatformFactory。
3. LinuxPlatformFactory 探测内核/文件系统/网络/IPC 基础能力，并根据 target_platform 判定是否需要启用 HalAvailabilityBridge。
4. CapabilityRegistry 记录各能力的 enabled、disabled 或 degraded 状态。
5. 工厂创建各 provider，并组装为 LinuxPlatformBundle。
6. 若必需能力缺失，工厂返回失败并阻断 Service Init；若非必需能力缺失，则保留 degraded 状态并返回 bundle。
7. 上层在 Service Init 阶段按接口注入需要的平台能力。

#### 6.7.2 运行期典型能力调用流程

1. 上游模块通过平台抽象接口发起线程、队列、文件、网络或 IPC 请求。
2. 对应 provider 校验参数、deadline 和能力状态。
3. provider 调用 Linux/POSIX 原语执行操作。
4. 成功时返回 handle/result，并更新 stats。
5. 失败时返回 PlatformError，同时写入 PlatformHealthCollector 的错误事实。
6. 上层将 PlatformError 映射到 ErrorInfo/Observation，并由 runtime/infra 决定后续恢复或审计动作。

### 6.8 异常与恢复时序

| 异常分类 | 检测点 | 恢复动作 | 失败兜底 |
|---|---|---|---|
| 能力缺失（如 HAL 不可用） | 初始化探测 | 对非必需能力标记 disabled/degraded，并保留原因 | 必需能力缺失则 Platform Init 失败；非必需能力由上层按 profile 降级 |
| 资源耗尽（线程、FD、缓冲） | create/open/connect/push | 返回 ResourceExhausted 并更新 health snapshot | 上层决定降级、限流或失败收敛；平台不做无限重试 |
| 超时 | join/pop/connect/send/receive | 返回 Timeout，保留操作上下文与 syscall 信息 | 上层 RecoveryManager 决定 retry/replan，平台仅提供事实 |
| 连接断开/peer 关闭 | network/ipc I/O | 标记 handle 失效，更新 disconnect 计数 | 调用方重新建立连接；平台不自动回放业务消息 |
| 文件权限或空间不足 | ensure_directory/write_atomic | 返回 PermissionDenied/NoSpace | 上层 services 或 infra 决定切换路径、降级或失败 |
| 定时器回调超时或漂移扩大 | timer tick 统计 | 累加 drift 指标并支持取消/重建定时器 | 若漂移超过阈值，仅标记 degraded，由上层决定是否重启组件 |
| 队列关闭或溢出 | push/pop/close | 明确返回 QueueClosed/ResourceExhausted | 调用方进行背压或切换同步路径 |

恢复原则：

1. 平台层只执行局部、无业务语义的清理动作，如关闭句柄、清空无效 listener、取消 timer。
2. 一切涉及业务重试、补偿、重规划和 SafeMode 裁定的动作都上移给 runtime 或 services。

### 6.9 配置项与默认策略

| 配置项 | 默认值 | 覆盖层级 | 说明 |
|---|---|---|---|
| platform.linux.enable_epoll | true | 默认/Profile/部署 | 是否启用 epoll/eventfd 优先实现；失败可回退 poll/select |
| platform.linux.require_hal | false | Profile/部署 | 是否将 HAL 视为必需能力 |
| platform.linux.thread.default_stack_kb | 512 | 默认/Profile | 默认线程栈大小 |
| platform.linux.thread.max_threads | 64 | Profile/部署 | 平台层允许创建的线程上限 |
| platform.linux.queue.default_capacity | 1024 | 默认/Profile | 队列默认容量 |
| platform.linux.queue.overflow_policy | reject | Profile/部署 | reject 或 block；建议平台默认 reject，例外规则遵循 docs/development/InfraConcurrencyPolicy.md |
| platform.linux.timer.min_granularity_ms | 10 | Profile | 最小定时粒度 |
| platform.linux.fs.atomic_write_tmp_suffix | .tmp | 默认/部署 | 原子写入临时文件后缀 |
| platform.linux.fs.root_prefix | /var/lib/dasall | Profile/部署 | 受控文件系统根前缀 |
| platform.linux.net.connect_timeout_ms | 3000 | Profile/部署 | 网络连接超时 |
| platform.linux.net.io_timeout_ms | 5000 | Profile/部署 | 网络读写超时 |
| platform.linux.ipc.socket_dir | /tmp/dasall | 部署 | Unix Socket 落点目录 |
| platform.linux.ipc.max_payload_bytes | 1048576 | Profile/部署 | IPC 消息最大尺寸 |
| platform.linux.health.sample_interval_ms | 1000 | Profile | health 快照采样周期 |
| platform.linux.diag.enable_procfs_snapshot | true | 部署/运行时 | 是否允许诊断读取 procfs 基础信息 |

默认策略：

1. desktop_full/cloud_full：HAL 默认关闭，network/ipc/full filesystem 开启。
2. edge_balanced：HAL 按需开启，线程数与队列容量受限，优先使用 Unix Socket 与局域网能力。
3. edge_minimal：保持最小线程和队列预算，禁用非必要网络与诊断能力。
4. factory_test：启用 HAL、IPC 与诊断快照，保留严格超时和错误暴露。

#### 6.9.1 epoll -> poll/select fallback 触发矩阵（冻结）

为避免隐式重试和语义漂移，LinuxNetworkProvider 首版冻结以下触发条件：

| 场景 | 触发条件 | 行为 | 错误语义 |
|---|---|---|---|
| A. 正常 epoll 路径 | `platform.linux.enable_epoll=true` 且 epoll/eventfd 初始化成功 | 使用 epoll/eventfd 后端处理 I/O | 保持原错误语义 |
| B. 初始化阶段 fallback | `enable_epoll=true` 且 `epoll_create1`/`eventfd` 失败，errno 属于 `ENOSYS`、`EINVAL`、`EMFILE`、`ENFILE` | 本次连接切换到 poll 后端，记录 degraded reason=`EpollInitFailedFallbackToPoll` | 不自动重试 connect；按当前失败或后续 I/O 结果返回 Timeout/ConnectionRefused/Disconnected |
| C. 配置显式关闭 epoll | `enable_epoll=false` | 直接使用 poll/select；不尝试 epoll | 不产生 fallback 事件 |
| D. 非冻结范围 | 运行期收到普通 I/O 错误（如 `ECONNRESET`、`EPIPE`） | 不触发后端切换，不做隐式重连 | 直接返回 Disconnected/ConnectionRefused |

冻结规则：

1. fallback 只允许发生在后端初始化阶段，不允许在业务 I/O 中动态切换后端。
2. 任意失败路径不得执行隐式重试；所有重试由上层 runtime/services 决策。
3. fallback 仅记录为能力降级事实，不改变 INetwork 接口语义与 contracts 边界。

评审记录与回链（2026-03-27）：

1. 评审结论：通过。A/B/C/D 四档触发矩阵冻结为 LinuxNetworkProvider 首版唯一后端选择规则。
2. 评审约束：禁止在运行期 I/O 错误上触发后端切换，禁止任何隐式重试。
3. 回链任务：PLAT-LNX-TODO-008（接口冻结边界）、PLAT-LNX-TODO-016（provider 实现边界）、PLAT-LNX-TODO-024（收口与证据回写）。

#### 6.9.2 profile 注入键全集、入口与覆盖优先级冻结（2026-03-27）

冻结结论：platform.linux.* 首版键名全集、注入入口和覆盖优先级在本节收敛，不再作为设计阻塞项。

键名全集（首版冻结）：

1. platform.linux.enable_epoll
2. platform.linux.require_hal
3. platform.linux.thread.default_stack_kb
4. platform.linux.thread.max_threads
5. platform.linux.queue.default_capacity
6. platform.linux.queue.overflow_policy
7. platform.linux.timer.min_granularity_ms
8. platform.linux.fs.atomic_write_tmp_suffix
9. platform.linux.fs.root_prefix
10. platform.linux.net.connect_timeout_ms
11. platform.linux.net.io_timeout_ms
12. platform.linux.ipc.socket_dir
13. platform.linux.ipc.max_payload_bytes
14. platform.linux.health.sample_interval_ms
15. platform.linux.diag.enable_procfs_snapshot

注入入口冻结：

1. 仅允许在 Boot 阶段显式构造 PlatformInitConfig 并传入 LinuxPlatformFactory::create(config)。
2. 平台层不直接读取 profile 文件，不直接解析部署配置键值。
3. integration 测试允许通过 fixture 显式构造 PlatformInitConfig，语义等同于 Boot 注入。

覆盖优先级冻结：

1. 默认值 < profile < 部署。
2. 同层冲突遵循最后写入生效，并在 bootstrap 合并阶段生成最终 PlatformInitConfig。
3. 平台层只消费合并后的结构体，不再参与优先级裁定。

PlatformInitConfig 缺失字段补充计划：

1. 现状：PlatformInitConfig 已冻结字段为 target_platform/profile_name/enable_hal/queue_defaults/io_timeouts，可支撑当前工厂与测试路径。
2. 缺口：enable_epoll、require_hal、ipc.socket_dir、ipc.max_payload_bytes、thread.default_stack_kb 等仍由上层合并后间接传入 provider，尚未全部进入结构体显式字段。
3. 补充计划：在 R-4/R-5 前以最小增量补齐 LinuxRuntimeTuning 聚合字段（或等效扩展），保证 021 集成测试对键名到结构体映射可二值断言。

回链：PLAT-LNX-TODO-010、PLAT-LNX-TODO-021、PLAT-LNX-TODO-025。

### 6.10 可观测性（日志/指标/追踪/审计）

由于 platform 不依赖 infra，platform/linux 只输出可消费的观测事实，不直接持有 infra logger/exporter：

1. 日志事实：通过 PlatformEvent 输出 category、operation、resource_ref、result_code、errno_value、latency_ms，由上层 bootstrap/infra 适配器决定如何落盘。
2. 指标事实：thread_create_total、thread_active_current、timer_drift_ms_p95、queue_overflow_total、file_io_fail_total、net_connect_fail_total、ipc_peer_closed_total。
3. 追踪关联：platform/linux 仅透传调用方提供的 trace_id/span correlation 字段，不自行创建追踪树。
4. 审计边界：对可能产生副作用的操作，仅输出 operation_class、target_path、endpoint、side_effect_kind 这类 auditable metadata，由上层决定是否写审计。

---

## 7. Design -> Build 映射（建议级）

| Design结论 | Build目标 | 映射说明 | 代码目标 | 测试目标 | 验收命令 | 依赖/阻塞 |
|---|---|---|---|---|---|---|
| 按能力切片定义平台公共接口 | 新增平台公共接口层 | 先冻结 IThread/ITimer/IQueue/IFileSystem/INetwork/IIPC 调用面，避免后续各模块各自封装 | platform/include/IThread.h, ITimer.h, IQueue.h, IFileSystem.h, INetwork.h, IIPC.h, PlatformError.h | tests/unit/platform/linux/InterfaceSurfaceTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | 阻塞：include 目录当前为空 |
| Linux 工厂负责统一装配与能力探测 | 新增 LinuxPlatformFactory 与 CapabilityRegistry | 把 profile 差异收敛到工厂与能力表，而非散落在上层 | platform/include/linux/LinuxPlatformFactory.h, LinuxPlatformCapabilities.h; platform/src/linux/LinuxPlatformFactory.cpp | tests/unit/platform/linux/LinuxPlatformFactoryTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R LinuxPlatformFactoryTest --output-on-failure | 依赖：Profile 配置键名确认 |
| 并发原语由平台统一提供 | 新增 PosixThreadProvider / PosixTimerProvider / BlockingQueueProvider | 为 infra、services 和后续 watchdog/worker 提供稳定底座 | platform/src/linux/PosixThreadProvider.cpp, PosixTimerProvider.cpp, BlockingQueueProvider.cpp | tests/unit/platform/linux/PosixThreadProviderTest.cpp, PosixTimerProviderTest.cpp, BlockingQueueProviderTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R "PosixThreadProviderTest|PosixTimerProviderTest|BlockingQueueProviderTest" --output-on-failure | 无 |
| 文件、网络、IPC 通过独立 provider 落地 | 新增 LinuxFileSystemProvider / LinuxNetworkProvider / UnixIpcProvider | 保持能力解耦，避免 God Object | platform/src/linux/LinuxFileSystemProvider.cpp, LinuxNetworkProvider.cpp, UnixIpcProvider.cpp | tests/unit/platform/linux/LinuxFileSystemProviderTest.cpp, LinuxNetworkProviderTest.cpp, UnixIpcProviderTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R "LinuxFileSystemProviderTest|LinuxNetworkProviderTest|UnixIpcProviderTest" --output-on-failure | 依赖：测试桩与临时目录能力 |
| HAL 仅通过受控桥接接入（已完成） | HalAvailabilityBridge + HalStub + HalProbe.h 已落地（2026-03-27） | 保证 desktop/cloud profile 不因 HAL 缺失失败，同时给 edge profile 留入口；最小探测接口已冻结 | platform/src/linux/HalAvailabilityBridge.cpp, platform/src/arm/hal/HalStub.cpp, platform/include/hal/HalProbe.h | tests/unit/platform/linux/HalAvailabilityBridgeTest.cpp（2026-03-27 已通过） | ctest --test-dir build-ci -R HalAvailabilityBridgeTest --output-on-failure | 最小解阻完成；真实 ARM HAL 驱动接口（GPIO/UART/I2C/SPI/CAN）留后续版本；文档补丁见专项 TODO-023 |
| 平台错误需可映射到上层 ErrorInfo | 新增 PlatformError/PlatformResult | 统一错误码和恢复事实，为上层做 contracts 映射预留锚点 | platform/include/PlatformError.h, PlatformResult.h | tests/unit/platform/linux/PlatformErrorMappingTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure | 依赖：上层 ErrorInfo 映射约定评审 |
| 观测事实通过快照与事件输出 | 新增 PlatformHealthCollector 与 PlatformEvent | 满足可诊断要求，同时不引入 infra 反向依赖 | platform/include/linux/PlatformHealthSnapshot.h; platform/src/linux/PlatformHealthCollector.cpp | tests/unit/platform/linux/PlatformHealthCollectorTest.cpp | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PlatformHealthCollectorTest --output-on-failure | 无 |
| 平台初始化必须有集成验证 | 新增初始化与降级路径集成测试 | 验证 boot 注入配置 -> 工厂装配 -> capability set 的主路径 | tests/integration/platform/linux/LinuxPlatformBootstrapIntegrationTest.cpp | integration：desktop_full 与 edge_balanced 两档 | cmake --build build-ci && ctest --test-dir build-ci -R LinuxPlatformBootstrapIntegrationTest --output-on-failure | 阻塞：tests/integration/platform/linux 目录注册（tests 顶层已就绪，见专项 TODO-020）；profile fixture 使用显式 PlatformInitConfig 结构体绕过 profile 文件解析 |
| 无法映射项：真实 ARM HAL 驱动实现 | 标记为后续版本任务 | 当前 Detailed Design 只定义接入点，不承诺本轮完成具体 GPIO/UART/I2C 驱动 | N/A | N/A | N/A | 阻塞：HAL 设备模型、sysroot、板级依赖未冻结 |

---

## 8. 实施计划与里程碑

### 8.1 目录与文件落盘建议

```text
platform/
  include/
    IThread.h
    ITimer.h
    IQueue.h
    IFileSystem.h
    INetwork.h
    IIPC.h
    PlatformError.h
    PlatformResult.h
  include/linux/
    LinuxPlatformFactory.h
    LinuxPlatformCapabilities.h
    PlatformHealthSnapshot.h
  src/linux/
    LinuxPlatformFactory.cpp
    CapabilityRegistry.cpp
    PosixThreadProvider.cpp
    PosixTimerProvider.cpp
    BlockingQueueProvider.cpp
    LinuxFileSystemProvider.cpp
    LinuxNetworkProvider.cpp
    UnixIpcProvider.cpp
    HalAvailabilityBridge.cpp
    PlatformHealthCollector.cpp
  src/arm/hal/
    HalStub.cpp
tests/
  unit/platform/linux/
  integration/platform/linux/
```

### 8.2 分阶段实施计划（最小可交付切分）

| 阶段 | 目标 | 关键动作 | 完成判定 |
|---|---|---|---|
| M0 设计冻结 | 固化 platform/linux 边界、接口和能力图 | 完成本详细设计并通过评审 | 文档评审结论为 Pass |
| M1 接口骨架 | 建立 public headers 和错误模型 | 新增 IThread/ITimer/IQueue/IFileSystem/INetwork/IIPC/PlatformError | 头文件可编译且接口 surface 测试通过 |
| M2 Linux 通用并发底座 | 补齐线程、定时器、队列 provider | 新增 PosixThreadProvider/PosixTimerProvider/BlockingQueueProvider | 三类 provider 单测全部通过 |
| M3 I/O 与 IPC 底座 | 补齐文件、网络、IPC provider | 新增 LinuxFileSystemProvider/LinuxNetworkProvider/UnixIpcProvider | 单测通过且失败注入可判定 |
| M4 初始化与能力探测 | 补齐 LinuxPlatformFactory、CapabilityRegistry、HealthCollector | 完成 profile 注入与 degraded 能力路径 | 工厂集成测试通过 |
| M5 HAL 接入点与 profile 收口 | 增加 HAL bridge、desktop/edge profile 覆盖 | desktop_full 下 HAL 关闭可运行，edge_balanced 下 HAL 桥接路径可判定 | profile 兼容测试通过 |

### 8.3 原子实施任务（建议级）

| ID | 状态 | 任务描述 | 输入依据 | 代码目标 | 测试目标 | 验收命令 | 完成判定 |
|---|---|---|---|---|---|---|---|
| PLAT-LNX-T001 | Not Started | 新增平台公共接口头文件 | 蓝图 3.11/6、工程规范 3.2 | platform/include/IThread.h 等 8 个头文件 | InterfaceSurfaceTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R InterfaceSurfaceTest --output-on-failure | 头文件可编译且禁止相对路径 include |
| PLAT-LNX-T002 | Not Started | 新增 PlatformError 与 PlatformResult | 架构 3.7、工程规范 3.6 | PlatformError.h, PlatformResult.h | PlatformErrorMappingTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PlatformErrorMappingTest --output-on-failure | 错误码分类稳定且测试通过 |
| PLAT-LNX-T003 | Not Started | 补齐线程 provider 实现 | 蓝图 3.11，设计 6.2/6.6 | platform/src/linux/PosixThreadProvider.cpp | PosixThreadProviderTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PosixThreadProviderTest --output-on-failure | 创建、join、timeout、资源耗尽路径均可判定 |
| PLAT-LNX-T004 | Not Started | 补齐定时器与队列 provider 实现 | 蓝图 3.11，设计 6.2/6.8 | PosixTimerProvider.cpp, BlockingQueueProvider.cpp | PosixTimerProviderTest, BlockingQueueProviderTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R "PosixTimerProviderTest|BlockingQueueProviderTest" --output-on-failure | 周期/取消、push/pop/close 路径全部通过 |
| PLAT-LNX-T005 | Not Started | 补齐文件、网络、IPC provider 实现 | 蓝图 3.11，设计 6.2/6.8 | LinuxFileSystemProvider.cpp, LinuxNetworkProvider.cpp, UnixIpcProvider.cpp | LinuxFileSystemProviderTest, LinuxNetworkProviderTest, UnixIpcProviderTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R "LinuxFileSystemProviderTest|LinuxNetworkProviderTest|UnixIpcProviderTest" --output-on-failure | 正常与权限/断连/超时负例均通过 |
| PLAT-LNX-T006 | Not Started | 新增 Linux 工厂与能力表 | 架构 9.2、蓝图 5.2 | LinuxPlatformFactory.h/.cpp, LinuxPlatformCapabilities.h, CapabilityRegistry.cpp | LinuxPlatformFactoryTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R LinuxPlatformFactoryTest --output-on-failure | desktop/edge 两档能力表与降级理由断言通过 |
| PLAT-LNX-T007 | Not Started | 新增健康快照与事件收集 | 架构可观测性要求、设计 6.10 | PlatformHealthSnapshot.h, PlatformHealthCollector.cpp | PlatformHealthCollectorTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R PlatformHealthCollectorTest --output-on-failure | 指标快照字段完整且可序列化 |
| PLAT-LNX-T008 | Not Started | 补齐 HAL 桥接与空实现桩 | 蓝图 3.11/5.1 | HalAvailabilityBridge.cpp, platform/src/arm/hal/HalStub.cpp | HalAvailabilityBridgeTest | cmake --build build-ci --target dasall_platform && ctest --test-dir build-ci -R HalAvailabilityBridgeTest --output-on-failure | desktop_full 返回 disabled，edge profile 返回 bridge 或明确 NotSupported |
| PLAT-LNX-T009 | Not Started | 注册 unit/integration 测试目标 | 工程规范 3.7、蓝图 7 | platform/CMakeLists.txt, tests/CMakeLists 相关注册 | ctest -N 发现性检查 | cmake -S . -B build-ci -G Ninja && cmake --build build-ci && ctest --test-dir build-ci -N -R "Platform|LinuxPlatform" | 新增测试可被 ctest 发现 |
| PLAT-LNX-T010 | Not Started | 新增平台初始化集成验证 | 架构 9.2、蓝图 5.2 | tests/integration/platform/linux/LinuxPlatformBootstrapIntegrationTest.cpp | desktop_full / edge_balanced 集成用例 | cmake --build build-ci && ctest --test-dir build-ci -R LinuxPlatformBootstrapIntegrationTest --output-on-failure | 配置注入、能力探测、降级路径均通过 |

---

## 9. 测试与质量门

### 9.1 测试矩阵

| 测试层级 | 覆盖范围 | 核心用例 | 验收方式 |
|---|---|---|---|
| 单元测试 | Thread/Timer/Queue provider | 线程 join timeout、timer cancel、queue close/overflow | ctest 按 provider 粒度执行，全部通过 |
| 单元测试 | FileSystem/Network/Ipc provider | 原子写入、权限拒绝、连接超时、peer closed、payload 超限 | 正负例均通过 |
| 契约测试影响点 | 上层 ErrorInfo/Observation 映射锚点 | PlatformError.code -> 上层错误分类映射不漂移 | 通过 mapping test 与设计评审确认 |
| 集成测试 | Boot 注入配置 -> LinuxPlatformFactory -> CapabilityRegistry | desktop_full 关闭 HAL 仍可启动；edge_balanced 启用 HAL bridge | LinuxPlatformBootstrapIntegrationTest 通过 |
| 失败注入测试 | 资源耗尽/断连/权限失败/超时 | FD 上限、socket disconnect、NoSpace、QueueClosed | Failure injection 用例可稳定复现并返回预期错误码 |
| 兼容性测试 | Profile 差异 | desktop_full、edge_balanced、edge_minimal 的能力表和默认值差异 | Profile fixture 断言全部通过 |

### 9.2 Gate 建议清单

1. Gate-PLAT-01：平台公共接口新增或变更时必须同步更新 unit test。
2. Gate-PLAT-02：任何 provider 失败都必须返回 PlatformError，不允许直接吞错或仅靠日志表达。
3. Gate-PLAT-03：desktop_full/cloud_full 构建下 HAL 缺失不能导致启动失败。
4. Gate-PLAT-04：edge_balanced/edge_minimal 下 require_hal=true 时，HAL 缺失必须显式失败并给出 reason。
5. Gate-PLAT-05：任何平台特化差异必须通过 CapabilityRegistry 或工厂注入实现，不允许在 runtime/cognition 代码中新增平台分支。
6. Gate-PLAT-06：集成测试必须覆盖“能力启用”和“能力降级”两条路径。

---

## 10. 兼容性与演进评估（建议级）

| breaking risk | 影响消费者 | 迁移路径 | 灰度策略 | 扩展预留 |
|---|---|---|---|---|
| Low | infra、services、未来直接消费 platform 接口的模块 | 先引入新接口与工厂，后续逐步把零散 POSIX 调用替换为平台抽象 | 先在 desktop_full 与 simulator/cli 链路验证，再扩展到 edge_balanced | 预留 io_uring、x86 特化、真实 HAL provider |

### 10.1 风险分级说明

- 当前 breaking risk 评为 Low，因为 platform/linux 目前几乎没有实际消费者，且本设计不修改已冻结 contracts。
- 若后续直接在多模块中散落 Linux 私有实现 include，再回收为平台抽象时，breaking risk 会升到 Medium。

### 10.2 兼容迁移路径

1. V1：落地平台公共接口和 Linux provider，建立工厂与能力表。
2. V1.1：在 infra、services 中逐步替换零散 OS 访问点，禁止新增直接 POSIX 调用。
3. V2：接入 x86 特化与真实 ARM HAL provider，但保持 IThread/IFileSystem/INetwork 等调用面不变。

### 10.3 后续扩展预留点

1. x86 特化网络或 SIMD 设备访问实现。
2. ARM HAL 真实 GPIO/UART/I2C/SPI/CAN provider。
3. 更高性能的 I/O 后端，如 io_uring，但保持接口语义不变。
4. 更细粒度的资源预算与 health snapshot 指标。

---

## 11. 风险、阻塞与回退（建议级）

### 11.1 阻塞管理表

| 阻塞项 | 影响任务 | 解阻条件 | 最小解阻动作 | 回退策略 |
|---|---|---|---|---|
| 已解阻：platform/include 公共接口与 unit 测试均已落盘（专项 TODO-001~019 已完成 2026-03-27） | 无活跃影响任务 | 已完成 | 2026-03-27 全部接口落盘并可编译 | N/A |
| tests/integration/platform/linux 目录未注册（tests 顶层 add_subdirectory(integration) 已就绪） | 专项 TODO-020（T010 集成测试） | 新增 tests/integration/platform/linux/CMakeLists.txt 并注册 integration 标签目标 | 先补 platform/linux 集成子目录注册，不需要改顶层 CMake | 用例发现性验证前不宣称集成门禁就绪 |
| HAL 最小探测接口已冻结并完成文档补票（HalProbe.h + HalStub + HalAvailabilityBridge；见 6.6.1，2026-03-27） | 后续 edge 真实驱动任务（不阻塞本轮） | 冻结 GPIO/UART/I2C/SPI/CAN 驱动接口（后续版本） | 维持 probe_hal_availability + HalProbeResult 最小接口；真实驱动单列后续版本任务 | 本轮只交付最小探测桥接；真实驱动留后续版本 |
| Profile 键名全集与注入入口已冻结（见 6.9.2，2026-03-27） | 专项 TODO-021（仍受集成目录注册/工厂 HAL 接线影响） | 完成 R-4 工厂 HAL 接线 + R-3 integration 目录注册 | 已固定默认<profile<部署优先级与 Boot 显式注入；允许 fixture 等价注入 | 在字段扩展完成前，保持“工厂只接受结构体输入”并禁止平台层直接解析键值 |
| 上层 ErrorInfo 映射规范已最小解阻（PlatformError.code 集与一级 category 映射已冻结 2026-03-27）；细粒度评审待完成 | 后续细粒度错误码扩展任务 | 评审完整 PlatformError -> ErrorInfo 映射表 | 已冻结 PlatformError.code 集与一级映射锚点（PlatformErrorMappingTest 通过） | 限制错误码数量；延后更多细分直至评审通过 |

### 11.2 风险清单

| 风险 | 等级 | 触发条件 | 监测信号 | 处置策略 |
|---|---|---|---|---|
| 平台层过度抽象 | High | 一次性引入大而全调度器或通用事件总线 | 单文件/单类膨胀、跨子域 include 增多 | 坚持能力切片，每次 PR 只落一个 provider |
| 直接 POSIX 调用在其他模块蔓延 | High | 上层为了赶进度绕过平台接口 | grep 到 runtime/services 中新增 socket/pthread 直接调用 | 在 code review 中禁止并补平台能力 |
| Profile 差异散落到业务层 | High | runtime/cognition 出现 target_platform 条件分支 | 业务模块新增平台宏判断 | 统一把差异收敛到 CapabilityRegistry 与工厂 |
| HAL 路径和 Linux 通用层耦死 | Medium | linux 通用代码直接 include 具体驱动头 | src/linux 依赖 src/arm/hal 实现细节 | 使用桥接接口或 stub 隔离 |
| 失败路径不可重复验证 | Medium | 缺少 failure injection 或临时目录/假 socket fixture | CI 上偶发失败或无法复现 | 先补测试桩，再扩实现 |

### 11.3 回退策略

1. 若 LinuxPlatformFactory 边界不稳定，回退为仅暴露接口和空 provider，不推进 I/O 实现。
2. 若网络/IPC provider 在本轮复杂度过高，允许先保留 NotImplemented/NotSupported 空实现，但接口和错误模型必须先冻结。
3. 若 HAL 桥接未准备好，desktop/cloud profile 先完成 Linux 通用能力闭环，edge profile 暂时以明确 Blocked 标记处理。

---

## 12. 未决问题与后续任务

### 12.1 未决问题

1. IQueue 是否需要模板化接口，还是使用字节缓冲/消息对象统一抽象。
2. LinuxNetworkProvider 首版是否只覆盖 client 侧，还是同步提供 server/listen 能力。
3. UnixIpcProvider 与 services 未来远程控制执行链路之间，谁拥有消息 framing 语义。
4. PlatformEvent 的最小字段集合是否需要在 docs/architecture 下再补一份观测约定。

### 12.2 后续任务建议

1. 发起 platform/linux 设计评审，重点确认接口粒度、工厂边界与 HAL 桥接策略。
2. 先按 PLAT-LNX-T001~T004 完成接口与并发底座，再进入文件/网络/IPC provider。
3. 在 tests 目录建立 platform/linux 的 unit/integration 分组，提前把 Gate-PLAT-01~06 接到 CI。
4. 在后续 platform/x86 与 platform/arm/hal 详细设计中，只补特化实现，不回改 Linux 通用层职责定义。