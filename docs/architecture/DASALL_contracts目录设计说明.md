# DASALL contracts 目录设计说明

## 1. 文档目的

本文档用于解释 DASALL 为什么将跨模块交互对象单独放置在 `contracts/` 目录，而不是分散在各子系统目录下；同时说明为什么 `contracts/` 当前以头文件定义为主，且 `src/` 目录不承载具体实现。

本文档面向以下读者：

1. 后续进行子系统深化设计的开发人员。
2. 需要理解 contracts 与模块边界关系的评审人员。
3. 需要面向 ARM Embedded Linux、边缘节点和长期运行场景做架构收敛的工程人员。

---

## 2. 结论摘要

1. `contracts/` 的本质不是功能模块，而是系统级共享契约层。
2. 该目录承载的是跨模块稳定语义边界，而不是某个子系统内部实现细节。
3. 因此它必须独立于 `runtime/`、`memory/`、`llm/`、`tools/`、`cognition/`、`multi_agent/` 等具体子系统。
4. `contracts/` 当前以头文件为主、`src/` 无具体实现，是刻意保持“低耦合、低依赖、可裁剪、可验证”的设计结果。

---

## 3. 设计依据

### 3.1 架构依据

根据 [docs/architecture/DASSALL_Agent_architecture.md](docs/architecture/DASSALL_Agent_architecture.md#L528-L575)：

1. Runtime、Memory、Tool、Multi-Agent 之间必须冻结一组跨模块共享的核心对象。
2. 如果不冻结，每个子系统都会定义自己的输入输出，最终导致编排层无法稳定工作。
3. 主流程和异常流程必须使用同一套契约。

根据 [docs/architecture/DASSALL_Agent_architecture.md](docs/architecture/DASSALL_Agent_architecture.md#L2028-L2060)：

1. `contracts` 不依赖任何业务模块。
2. `cognition` 只能依赖 `contracts` 和抽象接口。
3. 不同 Profile 下 contracts 对象必须保持一致，避免因裁剪导致接口漂移。

### 3.2 工程蓝图依据

根据 [docs/architecture/DASALL_Engineering_Blueprint.md](docs/architecture/DASALL_Engineering_Blueprint.md#L287-L315)：

1. `contracts/` 对应“全局契约，无分层归属”。
2. 该目录只包含跨模块共享的核心数据对象定义。
3. 该目录不依赖任何其他业务模块。
4. 该目录不包含可执行逻辑，只定义数据结构与接口枚举。
5. 每个子目录冻结一类契约，修改必须全局同步。

---

## 4. 为什么不放在对应子系统目录

### 4.1 因为这些对象不是单一模块私有对象

`contracts/` 中的大部分对象不是“某个模块内部要怎么实现”的私有数据结构，而是“多个模块之间如何协作”的公共语言。例如：

1. `ContextPacket` 由 memory 侧装配，但被 cognition、prompt、runtime 共同消费。
2. `Observation` 统一折叠工具输出、检索输出、人工反馈、子 Agent 输出，不属于 tools 私有域。
3. `Checkpoint` 由 runtime 生产，但恢复、记忆、结果汇聚都可能引用。
4. `WorkerTask` 服务于多 Agent 协同，但 runtime、调度、测试桩、结果汇聚都需要理解它。

如果这些对象被放在某个子系统目录下，团队会天然将其误认为该子系统私有模型，进而削弱“共享契约”的边界意识。

### 4.2 因为放回模块目录会破坏依赖方向

如果将跨模块对象放回对应目录，就会诱发以下错误依赖：

1. `cognition/` 为了拿 `ContextPacket` 去依赖 `memory/`。
2. `runtime/` 为了拿 `WorkerTask` 去依赖 `multi_agent/`。
3. `tests/` 为了拿公共对象去 include 多个业务模块。

这与 “contracts 不依赖任何模块，其他模块依赖 contracts” 的设计原则相冲突。

### 4.3 因为独立目录能降低对象漂移风险

一旦对象放在模块目录里，模块开发者最容易把它当作“本模块结构体”顺手修改，例如：

1. runtime 增加内部状态字段。
2. llm 增加 provider 私有参数。
3. tools 增加执行统计字段。
4. memory 增加内部缓存标记。

独立的 `contracts/` 目录在组织上先声明：这些对象不是模块私有扩展空间，而是共享边界资产，必须经过统一评审与全局同步。

### 4.4 因为独立目录才能支撑统一评审与统一 gate

当前 contracts freeze 工作包已经形成：

1. 设计说明文档
2. 对象定义头文件
3. guards / catalogs
4. contract tests
5. CI gate

如果对象分散在各模块目录，这条冻结链路将难以统一收敛，评审与兼容性控制会显著复杂化。

---

## 5. 为什么 `src/` 当前没有具体实现

### 5.1 因为 contracts 解决的是“边界定义”，不是“模块行为实现”

`contracts/` 当前主要承载以下内容：

1. 稳定数据对象定义
2. 枚举与错误码
3. 边界守卫
4. 兼容性与版本演进规则
5. 冻结里程碑 Gate 的可执行表达

这些都属于“契约层逻辑”，不是业务模块实现。

真正的子系统实现应在：

1. `memory/`：上下文装配、记忆写回、压缩策略
2. `llm/`：模型路由、调用链路、Prompt 治理
3. `tools/`：工具治理、执行、补偿
4. `runtime/`：主循环、状态机、恢复控制
5. `cognition/`：理解、规划、推理、反思
6. `multi_agent/`：协同调度、结果汇聚、失败回收

### 5.2 header-only 是为了保持 contracts 的低耦合特性

当前 contracts 基本保持 header-only 的收益包括：

1. 任何模块只需 include 对象定义，而不被迫链接业务实现。
2. 不会因为引用一个公共对象而把上层模块实现链进来。
3. 更利于 profile 裁剪和跨平台构建。
4. 更利于未来从语义契约过渡到 wire contract / schema / codec 层。

### 5.3 未来并非绝对不能有 `src/`

如果后续需要以下能力，contracts 附近可能出现少量实现：

1. 统一 schema 导出或 IDL 生成支持。
2. wire-level codec / migration helper。
3. 非平凡但仍属于契约层的 canonical normalization。

但这些实现仍应遵循两个约束：

1. 只能服务于契约层，不承载业务模块行为。
2. 不得反向依赖任何具体业务模块。

---

## 6. 从嵌入式工程视角看独立 contracts 的必要性

### 6.1 降低链接耦合与镜像膨胀风险

对 ARM Embedded Linux 和边缘节点而言，公共对象定义不应强迫引入额外实现依赖。独立的 header-only contracts 有利于：

1. 模块最小依赖编译。
2. Profile 精准裁剪。
3. 减少镜像不必要膨胀。

### 6.2 保持对象层与实现层解耦

嵌入式场景常见以下实现替换：

1. 本地轻量实现
2. 局域网模型实现
3. 云端回退实现
4. 精简工具链实现

这些实现可以变化，但跨模块对象边界不应随平台变化而漂移。独立 contracts 层正是这种稳定性的承载点。

### 6.3 支撑长期运行与 OTA 风险控制

在长期运行系统中，升级时需要明确判断：

1. 变的是对象边界，还是模块实现。
2. 是兼容扩展，还是 breaking change。
3. 是否会影响 checkpoint、恢复、消息流转与多模块协同。

独立 contracts 层有利于将“边界变化”与“实现变化”分离管理。

---

## 7. 从 Agent 行业视角看独立 contracts 的必要性

Agent 系统不是单次 request-response 系统，而是长链路状态流转系统。一次任务通常会经过：

1. `AgentRequest`
2. `GoalContract`
3. `ContextPacket`
4. `Observation`
5. `ObservationDigest`
6. `BeliefState`
7. `Checkpoint`
8. `ReflectionDecision`
9. `RecoveryOutcome`
10. `AgentResult`

如果没有独立的 contracts 层，这些中间对象很容易被不同子系统各自定义，最终导致：

1. 编排语言不统一。
2. 中间对象语义漂移。
3. 多 Agent 与恢复链路难以稳定扩展。

因此，独立 contracts 不是“目录偏好”，而是 Agent Runtime 的统一语义总线设计。

---

## 8. 边界与非目标

`contracts/` 应该承载：

1. 跨模块共享对象
2. 跨模块共享枚举和错误码
3. 兼容性、边界、版本演进与 Gate 规则

`contracts/` 不应该承载：

1. 子系统具体实现
2. 平台适配逻辑
3. 执行副作用逻辑
4. 调度算法与模型调用细节
5. 持久化、网络、OS、驱动实现

---

## 9. 结论

1. `contracts/` 必须独立存在，因为它承载的是系统级共享契约，而不是模块私有结构。
2. 当前 `src/` 无具体实现是合理设计，不是缺失。
3. 这种组织方式同时服务于架构边界稳定性、嵌入式裁剪、Agent 编排统一性和长期演进治理。
